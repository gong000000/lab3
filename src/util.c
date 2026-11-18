/*
 * util.c - allocation, diagnostics, and filesystem helpers
 */
#include "myshell.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Allocation wrappers                                                */
/* ------------------------------------------------------------------ */

static void out_of_memory(void)
{
    fputs("myshell: out of memory\n", stderr);
    exit(1);
}

void *msh_malloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL)
        out_of_memory();
    return p;
}

void *msh_calloc(size_t nmemb, size_t size)
{
    void *p = calloc(nmemb ? nmemb : 1, size ? size : 1);
    if (p == NULL)
        out_of_memory();
    return p;
}

void *msh_realloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (p == NULL)
        out_of_memory();
    return p;
}

char *msh_strdup(const char *s)
{
    char *p;

    if (s == NULL)
        s = "";
    p = strdup(s);
    if (p == NULL)
        out_of_memory();
    return p;
}

char *msh_strndup(const char *s, size_t n)
{
    char *p = strndup(s, n);
    if (p == NULL)
        out_of_memory();
    return p;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void msh_error(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", MSH_NAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void msh_perror(const char *context)
{
    fprintf(stderr, "%s: %s: %s\n", MSH_NAME, context, strerror(errno));
}

void msh_usage(const char *usage)
{
    fprintf(stderr, "%s: usage: %s\n", MSH_NAME, usage);
}

int msh_confirm(const char *fmt, ...)
{
    char buf[32];
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(" ", stderr);
    fflush(stderr);

    if (fgets(buf, sizeof(buf), stdin) == NULL)
        return 0;
    return buf[0] == 'y' || buf[0] == 'Y';
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                       */
/* ------------------------------------------------------------------ */

char *msh_join_path(const char *dir, const char *name)
{
    size_t dlen, nlen;
    int need_slash;
    char *out;

    if (dir == NULL || dir[0] == '\0')
        return msh_strdup(name == NULL ? "" : name);
    if (name == NULL || name[0] == '\0')
        return msh_strdup(dir);

    dlen = strlen(dir);
    nlen = strlen(name);
    need_slash = dir[dlen - 1] != '/';

    out = msh_malloc(dlen + (size_t)need_slash + nlen + 1);
    memcpy(out, dir, dlen);
    if (need_slash)
        out[dlen++] = '/';
    memcpy(out + dlen, name, nlen + 1);
    return out;
}

char *msh_basename_dup(const char *path)
{
    const char *end;
    const char *start;

    if (path == NULL || path[0] == '\0')
        return msh_strdup(".");

    end = path + strlen(path);
    while (end > path && end[-1] == '/')
        --end;
    if (end == path)                 /* path was all slashes */
        return msh_strdup("/");

    start = end;
    while (start > path && start[-1] != '/')
        --start;
    return msh_strndup(start, (size_t)(end - start));
}

char *msh_dirname_dup(const char *path)
{
    const char *end;
    const char *slash;

    if (path == NULL || path[0] == '\0')
        return msh_strdup(".");

    end = path + strlen(path);
    while (end > path && end[-1] == '/')
        --end;
    if (end == path)
        return msh_strdup("/");

    slash = end;
    while (slash > path && slash[-1] != '/')
        --slash;
    if (slash == path)
        return msh_strdup(".");
    while (slash > path && slash[-1] == '/')
        --slash;
    if (slash == path)
        return msh_strdup("/");
    return msh_strndup(path, (size_t)(slash - path));
}

int msh_path_exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

int msh_is_directory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ------------------------------------------------------------------ */
/* Low-level I/O                                                      */
/* ------------------------------------------------------------------ */

int msh_write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* File copy / removal                                                */
/* ------------------------------------------------------------------ */

int msh_copy_file_contents(const char *src, const char *dst, mode_t mode,
                           int force)
{
    int in = -1, out = -1, rc = -1;
    char *buf = NULL;
    struct stat st;

    in = open(src, O_RDONLY);
    if (in < 0)
        goto done;
    if (fstat(in, &st) < 0)
        goto done;

    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (out < 0 && force && errno == EACCES) {
        if (unlink(dst) == 0)
            out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    }
    if (out < 0)
        goto done;

    buf = msh_malloc(MSH_IO_BUFSIZE);
    for (;;) {
        ssize_t n = read(in, buf, MSH_IO_BUFSIZE);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            goto done;
        }
        if (n == 0)
            break;
        if (msh_write_all(out, buf, (size_t)n) < 0)
            goto done;
    }

    if (fchmod(out, mode & 07777) < 0)
        goto done;

    rc = 0;

done:
    if (rc < 0)
        msh_perror(dst);
    free(buf);
    if (in >= 0)
        close(in);
    if (out >= 0 && close(out) < 0 && rc == 0) {
        msh_perror(dst);
        rc = -1;
    }
    return rc;
}

static int copy_symlink(const char *src, const char *dst, int preserve)
{
    struct stat st;
    char *target;
    ssize_t n;
    size_t size;

    if (lstat(src, &st) < 0) {
        msh_perror(src);
        return -1;
    }
    size = st.st_size > 0 ? (size_t)st.st_size + 1 : 256;
    target = msh_malloc(size);
    n = readlink(src, target, size - 1);
    if (n < 0) {
        msh_perror(src);
        free(target);
        return -1;
    }
    target[n] = '\0';
    if (symlink(target, dst) < 0) {
        msh_perror(dst);
        free(target);
        return -1;
    }
    if (preserve)
        (void)utimensat(AT_FDCWD, dst, NULL, AT_SYMLINK_NOFOLLOW);
    free(target);
    return 0;
}

int msh_copy_tree(const char *src, const char *dst, int preserve)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    int rc = 0;
    mode_t mode;

    if (lstat(src, &st) < 0) {
        msh_perror(src);
        return -1;
    }

    if (S_ISLNK(st.st_mode))
        return copy_symlink(src, dst, preserve);

    if (S_ISDIR(st.st_mode)) {
        mode = st.st_mode & 07777;
        if (mkdir(dst, mode) < 0 && errno != EEXIST) {
            msh_perror(dst);
            return -1;
        }
        dir = opendir(src);
        if (dir == NULL) {
            msh_perror(src);
            return -1;
        }
        while ((entry = readdir(dir)) != NULL) {
            char *s, *d;

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;
            s = msh_join_path(src, entry->d_name);
            d = msh_join_path(dst, entry->d_name);
            if (msh_copy_tree(s, d, preserve) < 0)
                rc = -1;
            free(s);
            free(d);
        }
        closedir(dir);
        if (chmod(dst, mode) < 0) {
            msh_perror(dst);
            rc = -1;
        }
        if (preserve) {
            struct timespec times[2] = { st.st_atim, st.st_mtim };
            (void)utimensat(AT_FDCWD, dst, times, 0);
        }
        return rc;
    }

    if (S_ISREG(st.st_mode)) {
        if (msh_copy_file_contents(src, dst, st.st_mode, 1) < 0)
            return -1;
        if (preserve) {
            struct timespec times[2] = { st.st_atim, st.st_mtim };
            (void)utimensat(AT_FDCWD, dst, times, 0);
        }
        return 0;
    }

    msh_error("%s: unsupported file type", src);
    return -1;
}

int msh_remove_tree(const char *path)
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    int rc = 0;

    if (lstat(path, &st) < 0) {
        msh_perror(path);
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        dir = opendir(path);
        if (dir == NULL) {
            msh_perror(path);
            return -1;
        }
        while ((entry = readdir(dir)) != NULL) {
            char *child;

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;
            child = msh_join_path(path, entry->d_name);
            if (msh_remove_tree(child) < 0)
                rc = -1;
            free(child);
        }
        closedir(dir);
        if (rmdir(path) < 0) {
            msh_perror(path);
            rc = -1;
        }
        return rc;
    }

    if (unlink(path) < 0) {
        msh_perror(path);
        return -1;
    }
    return 0;
}

int msh_mkdir_p(const char *path, mode_t mode)
{
    char *copy;
    char *p;
    int rc = 0;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    copy = msh_strdup(path);
    for (p = copy;; ++p) {
        if (*p == '/' || *p == '\0') {
            char saved = *p;
            *p = '\0';
            if (copy[0] != '\0') {
                if (mkdir(copy, mode) < 0 && errno != EEXIST) {
                    msh_perror(copy);
                    rc = -1;
                    *p = saved;
                    break;
                }
                if (errno == EEXIST && !msh_is_directory(copy)) {
                    msh_error("%s: not a directory", copy);
                    rc = -1;
                    *p = saved;
                    break;
                }
            }
            *p = saved;
            if (saved == '\0')
                break;
        }
    }
    free(copy);
    return rc;
}

int msh_parse_octal(const char *text, mode_t *mode)
{
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0')
        return -1;
    errno = 0;
    value = strtol(text, &end, 8);
    if (errno != 0 || end == text || *end != '\0' || value < 0 ||
        value > 07777)
        return -1;
    *mode = (mode_t)value;
    return 0;
}

void msh_format_mode(mode_t mode, char out[11])
{
    if (S_ISDIR(mode))
        out[0] = 'd';
    else if (S_ISLNK(mode))
        out[0] = 'l';
    else if (S_ISCHR(mode))
        out[0] = 'c';
    else if (S_ISBLK(mode))
        out[0] = 'b';
    else if (S_ISFIFO(mode))
        out[0] = 'p';
    else if (S_ISSOCK(mode))
        out[0] = 's';
    else
        out[0] = '-';

    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';

    if (mode & S_ISUID)
        out[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID)
        out[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX)
        out[9] = (mode & S_IXOTH) ? 't' : 'T';

    out[10] = '\0';
}

long long msh_file_size(const struct stat *st)
{
    return (long long)st->st_size;
}

void msh_format_size(long long size, char *out, size_t out_size)
{
    static const char units[] = "BKMGTPE";
    double value = (double)size;
    int unit = 0;

    while (value >= 1024.0 && unit < 6) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        snprintf(out, out_size, "%lld", size);
    else
        snprintf(out, out_size, "%.1f%c", value, units[unit]);
}
