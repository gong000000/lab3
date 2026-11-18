/*
 * cmd_serve.c - built-in HTTP file server (GET/HEAD only).
 *
 * Security: the requested path is resolved with realpath() and must remain
 * inside the document root.  ".." segments are rejected.
 */
#include "myshell.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HTTP_MAX_REQUEST 8192
#define HTTP_MAX_PATH    4096

static volatile sig_atomic_t http_stop = 0;

static void http_terminate(int sig)
{
    (void)sig;
    http_stop = 1;
}

static int send_all_fd(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void url_decode(char *text)
{
    char *src = text;
    char *dst = text;

    while (*src != '\0') {
        if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int hi = src[1] >= '0' && src[1] <= '9' ? src[1] - '0'
                     : src[1] >= 'a' && src[1] <= 'f' ? src[1] - 'a' + 10
                     : src[1] >= 'A' && src[1] <= 'F' ? src[1] - 'A' + 10
                     : -1;
            int lo = src[2] >= '0' && src[2] <= '9' ? src[2] - '0'
                     : src[2] >= 'a' && src[2] <= 'f' ? src[2] - 'a' + 10
                     : src[2] >= 'A' && src[2] <= 'F' ? src[2] - 'A' + 10
                     : -1;
            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void html_escape(const char *text, char *out, size_t size)
{
    size_t used = 0;

    while (*text != '\0' && used + 1 < size) {
        const char *rep = NULL;
        switch (*text) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        default: break;
        }
        if (rep != NULL) {
            size_t len = strlen(rep);
            if (used + len >= size)
                break;
            memcpy(out + used, rep, len);
            used += len;
            ++text;
        } else {
            out[used++] = *text++;
        }
    }
    out[used] = '\0';
}

static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');

    if (dot == NULL)
        return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0)
        return "text/css";
    if (strcasecmp(dot, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0)
        return "text/plain; charset=utf-8";
    if (strcasecmp(dot, ".png") == 0)
        return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(dot, ".pdf") == 0)
        return "application/pdf";
    if (strcasecmp(dot, ".json") == 0)
        return "application/json";
    return "application/octet-stream";
}

static int send_error(int fd, int code, const char *reason)
{
    char body[256];
    char header[512];
    int n;
    int blen;

    blen = snprintf(body, sizeof(body),
                    "<html><body><h1>%d %s</h1></body></html>\n",
                    code, reason);
    n = snprintf(header, sizeof(header),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n\r\n",
                 code, reason, blen);
    if (send_all_fd(fd, header, (size_t)n) < 0)
        return -1;
    return send_all_fd(fd, body, (size_t)blen);
}

static int path_has_dotdot(const char *path)
{
    const char *p = path;

    while (*p != '\0') {
        const char *slash;
        size_t len;

        while (*p == '/')
            ++p;
        slash = strchr(p, '/');
        len = slash != NULL ? (size_t)(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 1;
        if (slash == NULL)
            break;
        p = slash + 1;
    }
    return 0;
}

static int within_root(const char *root, const char *path)
{
    size_t n = strlen(root);

    if (strcmp(root, "/") == 0)
        return path[0] == '/';
    return strncmp(path, root, n) == 0 &&
           (path[n] == '/' || path[n] == '\0');
}

static int serve_directory(int fd, const char *real_path, const char *url_path)
{
    DIR *dir;
    struct dirent *entry;
    char *body = NULL;
    size_t body_len = 0;
    FILE *fp;
    char header[512];
    int n;
    size_t url_len = strlen(url_path);

    if (url_len == 0 || url_path[url_len - 1] != '/') {
        char location[HTTP_MAX_PATH + 16];
        snprintf(location, sizeof(location), "Location: %s/\r\n", url_path);
        n = snprintf(header, sizeof(header),
                     "HTTP/1.1 301 Moved Permanently\r\n%s"
                     "Content-Length: 0\r\nConnection: close\r\n\r\n",
                     location);
        return send_all_fd(fd, header, (size_t)n);
    }

    fp = open_memstream(&body, &body_len);
    if (fp == NULL)
        return send_error(fd, 500, "Internal Server Error");
    fprintf(fp, "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<title>Index of %s</title></head><body><h1>Index of %s</h1>"
                "<pre>\n", url_path, url_path);
    dir = opendir(real_path);
    if (dir == NULL) {
        fclose(fp);
        free(body);
        return send_error(fd, 403, "Forbidden");
    }
    if (strcmp(url_path, "/") != 0)
        fputs("<a href=\"../\">../</a>\n", fp);
    while ((entry = readdir(dir)) != NULL) {
        char escaped[1024];
        char href[1024];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        html_escape(entry->d_name, escaped, sizeof(escaped));
        html_escape(entry->d_name, href, sizeof(href));
        fprintf(fp, "<a href=\"%s\">%s</a>\n", href, escaped);
    }
    closedir(dir);
    fputs("</pre></body></html>\n", fp);
    fclose(fp);

    n = snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n\r\n", body_len);
    if (send_all_fd(fd, header, (size_t)n) < 0 ||
        send_all_fd(fd, body, body_len) < 0) {
        free(body);
        return -1;
    }
    free(body);
    return 0;
}

static int serve_file(int fd, const char *real_path, const char *url_path,
                      int head_only)
{
    int in;
    struct stat st;
    char header[512];
    int n;
    char buf[65536];

    in = open(real_path, O_RDONLY);
    if (in < 0)
        return send_error(fd, 404, "Not Found");
    if (fstat(in, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(in);
        return send_error(fd, 404, "Not Found");
    }
    (void)url_path;
    n = snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %lld\r\n"
                 "Connection: close\r\n\r\n",
                 mime_type(real_path), (long long)st.st_size);
    if (send_all_fd(fd, header, (size_t)n) < 0) {
        close(in);
        return -1;
    }
    if (!head_only) {
        for (;;) {
            ssize_t got = read(in, buf, sizeof(buf));

            if (got < 0) {
                if (errno == EINTR)
                    continue;
                close(in);
                return -1;
            }
            if (got == 0)
                break;
            if (send_all_fd(fd, buf, (size_t)got) < 0) {
                close(in);
                return -1;
            }
        }
    }
    close(in);
    return 0;
}

static void http_client(int fd, const char *root_real)
{
    char request[HTTP_MAX_REQUEST + 1];
    size_t used = 0;
    char method[16];
    char raw_path[HTTP_MAX_PATH];
    char *query;
    char *rel;
    char *joined;
    char *real_path;
    struct stat st;
    int head_only = 0;

    for (;;) {
        ssize_t n;

        if (used >= HTTP_MAX_REQUEST)
            break;
        n = recv(fd, request + used, HTTP_MAX_REQUEST - used, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (n == 0)
            return;
        used += (size_t)n;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL ||
            strstr(request, "\n\n") != NULL)
            break;
    }
    request[used] = '\0';

    if (sscanf(request, "%15s %4095s", method, raw_path) != 2) {
        send_error(fd, 400, "Bad Request");
        return;
    }
    if (strcasecmp(method, "HEAD") == 0) {
        head_only = 1;
    } else if (strcasecmp(method, "GET") != 0) {
        send_error(fd, 501, "Not Implemented");
        return;
    }

    query = strchr(raw_path, '?');
    if (query != NULL)
        *query = '\0';
    url_decode(raw_path);

    if (raw_path[0] != '/') {
        send_error(fd, 400, "Bad Request");
        return;
    }
    rel = raw_path + 1;
    if (path_has_dotdot(rel)) {
        send_error(fd, 403, "Forbidden");
        return;
    }

    joined = msh_join_path(root_real, rel);
    real_path = realpath(joined, NULL);
    free(joined);
    if (real_path == NULL) {
        send_error(fd, 404, "Not Found");
        return;
    }
    if (!within_root(root_real, real_path)) {
        free(real_path);
        send_error(fd, 403, "Forbidden");
        return;
    }

    if (stat(real_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        (void)serve_directory(fd, real_path, raw_path);
    } else if (stat(real_path, &st) == 0 && S_ISREG(st.st_mode)) {
        (void)serve_file(fd, real_path, raw_path, head_only);
    } else {
        send_error(fd, 404, "Not Found");
    }
    free(real_path);
}

static int parse_port(const char *text, int *port)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 ||
        value > 65535)
        return -1;
    *port = (int)value;
    return 0;
}

int cmd_serve(int argc, char **argv, shell_state_t *sh)
{
    const char *address = "127.0.0.1";
    const char *docroot = ".";
    int port = 8080;
    int once = 0;
    int i;
    int listen_fd = -1;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char port_text[16];
    char *root_real;

    (void)sh;
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "-1") == 0) {
            once = 1;
        } else if (strcmp(arg, "-p") == 0) {
            if (++i >= argc || parse_port(argv[i], &port) < 0) {
                msh_error("serve: invalid port");
                return 2;
            }
        } else if (strcmp(arg, "-d") == 0) {
            if (++i >= argc) {
                msh_error("serve: -d requires a directory");
                return 2;
            }
            docroot = argv[i];
        } else if (strcmp(arg, "-a") == 0) {
            if (++i >= argc) {
                msh_error("serve: -a requires an address");
                return 2;
            }
            address = argv[i];
        } else {
            msh_error("serve: invalid option: %s", arg);
            msh_usage("serve [-p port] [-a address] [-d directory] [-1]");
            return 2;
        }
    }

    root_real = realpath(docroot, NULL);
    if (root_real == NULL) {
        msh_perror(docroot);
        return 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if (getaddrinfo(address, port_text, &hints, &result) != 0) {
        msh_error("serve: cannot resolve %s", address);
        free(root_real);
        return 1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        int one = 1;

        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd < 0)
            continue;
        (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                         &one, sizeof(one));
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0 &&
            listen(listen_fd, 16) == 0)
            break;
        close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(result);

    if (listen_fd < 0) {
        msh_perror("serve: bind/listen");
        free(root_real);
        return 1;
    }

    {
        struct sockaddr_in actual;
        socklen_t actual_len = sizeof(actual);

        if (getsockname(listen_fd, (struct sockaddr *)&actual,
                        &actual_len) == 0)
            port = ntohs(actual.sin_port);
    }
    printf("serve: http://%s:%d/ (root: %s)%s\n", address, port, root_real,
           once ? " (once)" : "");
    fflush(stdout);

    {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = http_terminate;
        (void)sigaction(SIGINT, &sa, NULL);
        (void)sigaction(SIGTERM, &sa, NULL);
        (void)signal(SIGPIPE, SIG_IGN);
    }

    while (!http_stop) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd = accept(listen_fd, (struct sockaddr *)&peer,
                               &peer_len);
        pid_t pid;

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            if (!http_stop)
                msh_perror("serve: accept");
            break;
        }
        pid = fork();
        if (pid < 0) {
            msh_perror("serve: fork");
            close(client_fd);
            continue;
        }
        if (pid == 0) {
            close(listen_fd);
            http_client(client_fd, root_real);
            close(client_fd);
            _exit(0);
        }
        close(client_fd);
        if (once) {
            int status;

            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
            break;
        }
        (void)signal(SIGCHLD, SIG_IGN);
    }

    close(listen_fd);
    free(root_real);
    return 0;
}
