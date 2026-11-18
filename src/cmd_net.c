/*
 * cmd_net.c - TCP remote shell server/client (netserv, netcli)
 *
 * The server listens on TCP, authenticates optionally with a shared token,
 * executes each received command line with the shell's own parser/executor,
 * captures stdout/stderr, and returns them with the exit status.
 *
 * Wire format (network byte order):
 *   request : "MSH1" | token_len | command_len | token | command
 *   response: status | stdout_len | stderr_len | stdout | stderr
 */
#include "myshell.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NET_DEFAULT_PORT   9090
#define NET_MAX_TOKEN      256
#define NET_MAX_COMMAND    4096
#define NET_MAX_OUTPUT     (1024 * 1024)
#define NET_MAX_CLIENTS    32
#define NET_DEFAULT_TIMEOUT 10

/* ------------------------------------------------------------------ */
/* Protocol helpers                                                   */
/* ------------------------------------------------------------------ */

static int send_all(int fd, const void *buf, size_t len)
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

static int recv_all(int fd, void *buf, size_t len)
{
    char *p = buf;

    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
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

static int send_u32(int fd, uint32_t value)
{
    uint32_t net = htonl(value);
    return send_all(fd, &net, sizeof(net));
}

static int recv_u32(int fd, uint32_t *value)
{
    uint32_t net;

    if (recv_all(fd, &net, sizeof(net)) < 0)
        return -1;
    *value = ntohl(net);
    return 0;
}

static int read_request(int fd, char *token, size_t token_size,
                        char *command, size_t command_size)
{
    char magic[4];
    uint32_t token_len, command_len;

    if (recv_all(fd, magic, sizeof(magic)) < 0)
        return -1;
    if (memcmp(magic, "MSH1", 4) != 0)
        return -2;
    if (recv_u32(fd, &token_len) < 0 || recv_u32(fd, &command_len) < 0)
        return -1;
    if (token_len >= token_size || command_len >= command_size)
        return -3;
    if (recv_all(fd, token, token_len) < 0)
        return -1;
    token[token_len] = '\0';
    if (recv_all(fd, command, command_len) < 0)
        return -1;
    command[command_len] = '\0';
    return 0;
}

static int send_request(int fd, const char *token, const char *command)
{
    size_t token_len = strlen(token);
    size_t command_len = strlen(command);

    if (token_len > NET_MAX_TOKEN || command_len > NET_MAX_COMMAND)
        return -1;
    if (send_all(fd, "MSH1", 4) < 0)
        return -1;
    if (send_u32(fd, (uint32_t)token_len) < 0 ||
        send_u32(fd, (uint32_t)command_len) < 0)
        return -1;
    if (token_len > 0 && send_all(fd, token, token_len) < 0)
        return -1;
    if (command_len > 0 && send_all(fd, command, command_len) < 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Output buffers                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    int truncated;
} net_buf_t;

static void nb_append(net_buf_t *buf, const char *data, size_t len)
{
    size_t take;

    if (buf->len >= NET_MAX_OUTPUT) {
        if (len > 0)
            buf->truncated = 1;
        return;
    }
    take = len;
    if (take > NET_MAX_OUTPUT - buf->len) {
        take = NET_MAX_OUTPUT - buf->len;
        buf->truncated = 1;
    }
    if (take == 0)
        return;
    buf->data = msh_realloc(buf->data, buf->len + take);
    memcpy(buf->data + buf->len, data, take);
    buf->len += take;
}

static void nb_free(net_buf_t *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->truncated = 0;
}

static int send_response(int fd, int status, const net_buf_t *out,
                         const net_buf_t *err)
{
    if (send_u32(fd, (uint32_t)status) < 0 ||
        send_u32(fd, (uint32_t)out->len) < 0 ||
        send_u32(fd, (uint32_t)err->len) < 0)
        return -1;
    if (out->len > 0 && send_all(fd, out->data, out->len) < 0)
        return -1;
    if (err->len > 0 && send_all(fd, err->data, err->len) < 0)
        return -1;
    return 0;
}

static int send_error_response(int fd, int status, const char *message)
{
    net_buf_t out = { 0 };
    net_buf_t err = { 0 };
    int rc;

    nb_append(&err, message, strlen(message));
    rc = send_response(fd, status, &out, &err);
    nb_free(&out);
    nb_free(&err);
    return rc;
}

static int recv_response(int fd, int *status)
{
    uint32_t value, out_len, err_len;
    char *out = NULL;
    char *err = NULL;
    int rc = -1;

    if (recv_u32(fd, &value) < 0 || recv_u32(fd, &out_len) < 0 ||
        recv_u32(fd, &err_len) < 0)
        return -1;
    if (out_len > NET_MAX_OUTPUT || err_len > NET_MAX_OUTPUT) {
        msh_error("netcli: response too large");
        return -1;
    }

    out = msh_malloc((size_t)out_len + 1);
    err = msh_malloc((size_t)err_len + 1);
    if (out_len > 0 && recv_all(fd, out, out_len) < 0)
        goto done;
    if (err_len > 0 && recv_all(fd, err, err_len) < 0)
        goto done;
    out[out_len] = '\0';
    err[err_len] = '\0';
    if (out_len > 0)
        fwrite(out, 1, out_len, stdout);
    if (err_len > 0)
        fwrite(err, 1, err_len, stderr);
    fflush(stdout);
    fflush(stderr);
    *status = (int)value;
    rc = 0;

done:
    free(out);
    free(err);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Remote command execution                                           */
/* ------------------------------------------------------------------ */

static int has_background(const char *command)
{
    int in_single = 0;
    int in_double = 0;
    size_t i;

    for (i = 0; command[i] != '\0'; ++i) {
        char c = command[i];

        if (c == '\\' && !in_single) {
            if (command[i + 1] != '\0')
                ++i;
            continue;
        }
        if (in_single) {
            if (c == '\'')
                in_single = 0;
            continue;
        }
        if (in_double) {
            if (c == '"')
                in_double = 0;
            continue;
        }
        if (c == '\'') {
            in_single = 1;
        } else if (c == '"') {
            in_double = 1;
        } else if (c == '&') {
            if (command[i + 1] == '>')
                ++i;                /* &> or &>> redirect */
            else if (i > 0 && command[i - 1] == '>')
                ;                   /* >& is left for the parser */
            else
                return 1;
        }
    }
    return 0;
}

static int capture_command(const char *command, shell_state_t *sh,
                           int timeout, net_buf_t *out, net_buf_t *err,
                           int *status)
{
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    pid_t pid;
    pid_t waited;
    int fds[2];
    int open_fds = 2;
    int child_status = 0;
    int timed_out = 0;
    time_t deadline;

    *status = 1;
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        nb_append(err, "netserv: pipe failed\n", 21);
        goto fail;
    }

    pid = fork();
    if (pid < 0) {
        nb_append(err, "netserv: fork failed\n", 20);
        goto fail;
    }

    if (pid == 0) {
        shell_state_t remote;
        int devnull;
        int rc;

        (void)setpgid(0, 0);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(err_pipe[1], STDERR_FILENO) < 0)
            _exit(127);
        devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        reset_child_signals();
        __fpurge(stdin);

        remote = *sh;
        remote.interactive = 0;
        remote.should_exit = 0;
        remote.exit_status = 0;
        remote.last_status = 0;
        remote.last_bg_pid = 0;
        remote.shell_pid = getpid();
        remote.shell_pgid = getpgrp();
        remote.terminal_fd = -1;
        remote.next_job_id = 1;
        memset(remote.jobs, 0, sizeof(remote.jobs));

        rc = shell_execute_line(command, &remote);
        fflush(stdout);
        fflush(stderr);
        _exit(rc & 0xff);
    }

    close(out_pipe[1]);
    out_pipe[1] = -1;
    close(err_pipe[1]);
    err_pipe[1] = -1;
    (void)setpgid(pid, pid);

    fds[0] = out_pipe[0];
    fds[1] = err_pipe[0];
    deadline = time(NULL) + timeout;

    while (open_fds > 0) {
        struct pollfd pollfds[2];
        int ready;
        int i;

        for (i = 0; i < 2; ++i) {
            pollfds[i].fd = fds[i];
            pollfds[i].events = POLLIN | POLLHUP | POLLERR;
            pollfds[i].revents = 0;
        }
        ready = poll(pollfds, 2, 100);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            timed_out = 1;
            (void)kill(-pid, SIGKILL);
            break;
        }
        if (ready == 0) {
            if (time(NULL) >= deadline) {
                timed_out = 1;
                (void)kill(-pid, SIGKILL);
                break;
            }
            continue;
        }
        for (i = 0; i < 2; ++i) {
            char chunk[8192];
            ssize_t n;

            if (fds[i] < 0 || pollfds[i].revents == 0)
                continue;
            n = read(fds[i], chunk, sizeof(chunk));
            if (n > 0) {
                nb_append(i == 0 ? out : err, chunk, (size_t)n);
            } else if (n == 0) {
                close(fds[i]);
                fds[i] = -1;
                --open_fds;
            } else if (errno != EINTR) {
                close(fds[i]);
                fds[i] = -1;
                --open_fds;
            }
        }
    }

    if (fds[0] >= 0)
        close(fds[0]);
    if (fds[1] >= 0)
        close(fds[1]);

    do {
        waited = waitpid(pid, &child_status, 0);
    } while (waited < 0 && errno == EINTR);

    if (timed_out) {
        *status = 124;
        nb_append(err, "netserv: command timed out\n", 27);
    } else if (waited < 0) {
        *status = 1;
        nb_append(err, "netserv: waitpid failed\n", 24);
    } else if (WIFEXITED(child_status)) {
        *status = WEXITSTATUS(child_status);
    } else if (WIFSIGNALED(child_status)) {
        *status = 128 + WTERMSIG(child_status);
    } else {
        *status = 1;
    }

    if (out->truncated)
        nb_append(err, "netserv: stdout truncated\n", 27);
    if (err->truncated)
        nb_append(err, "netserv: stderr truncated\n", 27);

    return 0;

fail:
    if (out_pipe[0] >= 0)
        close(out_pipe[0]);
    if (out_pipe[1] >= 0)
        close(out_pipe[1]);
    if (err_pipe[0] >= 0)
        close(err_pipe[0]);
    if (err_pipe[1] >= 0)
        close(err_pipe[1]);
    *status = 1;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Server                                                             */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t server_stop = 0;
static volatile sig_atomic_t active_clients = 0;

static void server_terminate(int sig)
{
    (void)sig;
    server_stop = 1;
}

static void server_reap(int sig)
{
    int saved_errno = errno;

    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        if (active_clients > 0)
            --active_clients;
    }
    errno = saved_errno;
}

static void handle_client(int fd, shell_state_t *sh, const char *token,
                          int timeout)
{
    while (!server_stop) {
        char request_token[NET_MAX_TOKEN + 1];
        char command[NET_MAX_COMMAND + 1];
        net_buf_t out = { 0 };
        net_buf_t err = { 0 };
        int status = 0;
        int rc = read_request(fd, request_token, sizeof(request_token),
                              command, sizeof(command));

        if (rc < 0) {
            if (rc == -2 || rc == -3)
                (void)send_error_response(fd, 400, "netserv: bad request\n");
            break;
        }

        if (token[0] != '\0' && strcmp(request_token, token) != 0) {
            (void)send_error_response(fd, 403,
                                      "netserv: authentication failed\n");
            break;
        }
        if (command[0] == '\0') {
            (void)send_error_response(fd, 400, "netserv: empty command\n");
            continue;
        }
        if (has_background(command)) {
            (void)send_error_response(fd, 400,
                                      "netserv: background jobs are not "
                                      "supported remotely\n");
            continue;
        }

        (void)capture_command(command, sh, timeout, &out, &err, &status);
        if (send_response(fd, status, &out, &err) < 0) {
            nb_free(&out);
            nb_free(&err);
            break;
        }
        nb_free(&out);
        nb_free(&err);
    }
}

static int is_loopback_address(const char *address)
{
    struct in_addr addr;

    if (strcmp(address, "localhost") == 0)
        return 1;
    if (inet_pton(AF_INET, address, &addr) == 1)
        return (ntohl(addr.s_addr) >> 24) == 127;
    return 0;
}

static int parse_number(const char *text, int min, int max, int *value)
{
    char *end = NULL;
    long number;

    errno = 0;
    number = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        number < min || number > max)
        return -1;
    *value = (int)number;
    return 0;
}

int cmd_netserv(int argc, char **argv, shell_state_t *sh)
{
    const char *address = "127.0.0.1";
    const char *token = "";
    int port = NET_DEFAULT_PORT;
    int timeout = NET_DEFAULT_TIMEOUT;
    int once = 0;
    int max_clients = 0;
    int accepted = 0;
    int i;
    int listen_fd = -1;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char port_text[16];

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }
        if (strcmp(arg, "-1") == 0 || strcmp(arg, "--once") == 0) {
            once = 1;
        } else if (strcmp(arg, "-n") == 0) {
            if (++i >= argc ||
                parse_number(argv[i], 1, 1024, &max_clients) < 0) {
                msh_error("netserv: invalid connection limit");
                return 2;
            }
        } else if (strcmp(arg, "-p") == 0) {
            if (++i >= argc || parse_number(argv[i], 0, 65535, &port) < 0) {
                msh_error("netserv: invalid port");
                return 2;
            }
        } else if (strcmp(arg, "-a") == 0) {
            if (++i >= argc) {
                msh_error("netserv: -a requires an address");
                return 2;
            }
            address = argv[i];
        } else if (strcmp(arg, "-s") == 0) {
            if (++i >= argc || strlen(argv[i]) > NET_MAX_TOKEN) {
                msh_error("netserv: invalid token");
                return 2;
            }
            token = argv[i];
        } else if (strcmp(arg, "-t") == 0) {
            if (++i >= argc || parse_number(argv[i], 1, 3600, &timeout) < 0) {
                msh_error("netserv: invalid timeout");
                return 2;
            }
        } else {
            msh_error("netserv: invalid option: %s", arg);
            msh_usage("netserv [-p port] [-a address] [-s token] "
                      "[-t seconds] [-n count] [-1]");
            return 2;
        }
    }
    if (i != argc) {
        msh_usage("netserv [-p port] [-a address] [-s token] "
                  "[-t seconds] [-n count] [-1]");
        return 2;
    }

    if (!is_loopback_address(address) && token[0] == '\0') {
        msh_error("netserv: a token (-s) is required for a non-loopback "
                  "address");
        return 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_text, sizeof(port_text), "%d", port);

    if (getaddrinfo(address, port_text, &hints, &result) != 0) {
        msh_error("netserv: cannot resolve %s", address);
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
        msh_perror("netserv: bind/listen");
        return 1;
    }

    {
        struct sockaddr_in actual;
        socklen_t actual_len = sizeof(actual);

        if (getsockname(listen_fd, (struct sockaddr *)&actual,
                        &actual_len) == 0)
            port = ntohs(actual.sin_port);
    }
    printf("netserv: listening on %s:%d%s%s%s\n", address, port,
           token[0] != '\0' ? " (auth)" : "",
           once ? " (once)" : "",
           max_clients > 0 ? " (limited)" : "");
    fflush(stdout);

    {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = server_terminate;
        (void)sigaction(SIGINT, &sa, NULL);
        (void)sigaction(SIGTERM, &sa, NULL);
        (void)sigaction(SIGHUP, &sa, NULL);
        (void)signal(SIGPIPE, SIG_IGN);
        if (!once && max_clients == 0) {
            sa.sa_handler = server_reap;
            (void)sigaction(SIGCHLD, &sa, NULL);
        }
    }

    while (!server_stop) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd;
        pid_t pid;

        client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            if (!server_stop)
                msh_perror("netserv: accept");
            break;
        }

        if (!once && active_clients >= NET_MAX_CLIENTS) {
            close(client_fd);
            continue;
        }

        pid = fork();
        if (pid < 0) {
            msh_perror("netserv: fork");
            close(client_fd);
            continue;
        }
        if (pid == 0) {
            close(listen_fd);
            (void)signal(SIGCHLD, SIG_DFL);
            (void)signal(SIGPIPE, SIG_IGN);
            handle_client(client_fd, sh, token, timeout);
            close(client_fd);
            _exit(0);
        }

        close(client_fd);
        ++active_clients;
        ++accepted;
        if (once) {
            int status;

            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
            break;
        }
        if (max_clients > 0 && accepted >= max_clients)
            break;
    }

    if (max_clients > 0 && !once) {
        for (;;) {
            pid_t waited = waitpid(-1, NULL, 0);

            if (waited > 0)
                continue;
            if (waited < 0 && errno == EINTR)
                continue;
            break;
        }
    }

    close(listen_fd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Client                                                             */
/* ------------------------------------------------------------------ */

static int connect_tcp(const char *host, int port, int timeout)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char port_text[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);

    if (getaddrinfo(host, port_text, &hints, &result) != 0) {
        msh_error("netcli: cannot resolve %s", host);
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd < 0) {
        msh_error("netcli: cannot connect to %s:%d", host, port);
        return -1;
    }

    {
        struct timeval tv;
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    return fd;
}

static char *join_command(int argc, char **argv, int start)
{
    size_t total = 1;
    int i;
    char *command;
    char *p;

    for (i = start; i < argc; ++i)
        total += strlen(argv[i]) + 1;
    command = msh_malloc(total);
    p = command;
    for (i = start; i < argc; ++i) {
        size_t len = strlen(argv[i]);

        if (i > start)
            *p++ = ' ';
        memcpy(p, argv[i], len);
        p += len;
    }
    *p = '\0';
    return command;
}

static int is_number(const char *text)
{
    if (*text == '\0')
        return 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9')
            return 0;
        ++text;
    }
    return 1;
}

int cmd_netcli(int argc, char **argv, shell_state_t *sh)
{
    int interactive = 0;
    int port = NET_DEFAULT_PORT;
    int timeout = NET_DEFAULT_TIMEOUT;
    const char *token = "";
    const char *host = NULL;
    char *command = NULL;
    int i;
    int fd;
    int status = 0;

    (void)sh;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }
        if (strcmp(arg, "-i") == 0) {
            interactive = 1;
        } else if (strcmp(arg, "-p") == 0) {
            if (++i >= argc || parse_number(argv[i], 1, 65535, &port) < 0) {
                msh_error("netcli: invalid port");
                return 2;
            }
        } else if (strcmp(arg, "-s") == 0) {
            if (++i >= argc || strlen(argv[i]) > NET_MAX_TOKEN) {
                msh_error("netcli: invalid token");
                return 2;
            }
            token = argv[i];
        } else if (strcmp(arg, "-t") == 0) {
            if (++i >= argc || parse_number(argv[i], 1, 3600, &timeout) < 0) {
                msh_error("netcli: invalid timeout");
                return 2;
            }
        } else if (arg[0] == '-' && arg[1] != '\0') {
            msh_error("netcli: invalid option: %s", arg);
            msh_usage("netcli [-p port] [-s token] [-t seconds] [-i] "
                      "host [command ...]");
            return 2;
        } else {
            break;
        }
    }

    if (i >= argc) {
        msh_usage("netcli [-p port] [-s token] [-t seconds] [-i] "
                  "host [command ...]");
        return 2;
    }
    host = argv[i++];

    if (i < argc && is_number(argv[i]) &&
        (interactive || i + 1 < argc)) {
        int value;

        if (parse_number(argv[i], 1, 65535, &value) == 0)
            port = value;
        ++i;
    }

    if (interactive) {
        if (i < argc) {
            msh_error("netcli: -i does not take a command");
            return 2;
        }
    } else {
        if (i >= argc) {
            msh_usage("netcli [-p port] [-s token] [-t seconds] "
                      "host command ...");
            return 2;
        }
        command = join_command(argc, argv, i);
    }

    fd = connect_tcp(host, port, timeout);
    if (fd < 0) {
        free(command);
        return 1;
    }

    if (interactive) {
        char *line = NULL;
        size_t cap = 0;
        int is_tty = isatty(STDIN_FILENO);

        for (;;) {
            ssize_t n;

            if (is_tty) {
                fputs("netcli> ", stdout);
                fflush(stdout);
            }
            n = getline(&line, &cap, stdin);
            if (n < 0)
                break;
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = '\0';
            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
                break;
            if (line[0] == '\0')
                continue;
            if (send_request(fd, token, line) < 0 ||
                recv_response(fd, &status) < 0) {
                msh_error("netcli: connection lost");
                break;
            }
        }
        free(line);
        status = 0;
    } else {
        if (send_request(fd, token, command) < 0 ||
            recv_response(fd, &status) < 0) {
            msh_error("netcli: request failed");
            status = 1;
        }
    }

    close(fd);
    free(command);
    return status & 0xff;
}
