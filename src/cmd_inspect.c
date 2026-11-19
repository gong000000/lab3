/*
 * cmd_inspect.c - inspect the shell and other processes through /proc.
 *
 * Usage: inspect [-f|-m|-n|-j] [PID]
 *
 * The command makes the "transparent shell" idea concrete: the same shell
 * that runs a command can show its process identity, open file descriptors,
 * memory map, namespaces, resource limits, and background jobs.
 */
#include "myshell.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_limits(void)
{
    struct rlimit rl;
    struct {
        int resource;
        const char *name;
    } items[] = {
        { RLIMIT_NOFILE, "NOFILE" },
        { RLIMIT_NPROC, "NPROC" },
        { RLIMIT_AS, "AS" },
        { RLIMIT_STACK, "STACK" },
        { RLIMIT_CORE, "CORE" },
    };
    size_t i;

    printf("limits:\n");
    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        if (getrlimit(items[i].resource, &rl) < 0)
            continue;
        printf("  %-7s soft=", items[i].name);
        if (rl.rlim_cur == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%llu", (unsigned long long)rl.rlim_cur);
        printf(" hard=");
        if (rl.rlim_max == RLIM_INFINITY)
            printf("unlimited");
        else
            printf("%llu", (unsigned long long)rl.rlim_max);
        putchar('\n');
    }
}

static void print_shell_identity(shell_state_t *sh)
{
    char cwd[PATH_MAX];
    char host[256];
    mode_t mask;

    printf("shell:\n");
    printf("  pid=%d ppid=%d pgid=%d sid=%d\n", (int)getpid(),
           (int)getppid(), (int)getpgrp(), (int)getsid(0));
    printf("  uid=%d euid=%d gid=%d egid=%d\n", (int)getuid(), (int)geteuid(),
           (int)getgid(), (int)getegid());
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("  cwd=%s\n", cwd);
    if (gethostname(host, sizeof(host)) == 0)
        printf("  hostname=%s\n", host);
    mask = umask(0);
    (void)umask(mask);
    printf("  umask=0%03o\n", (unsigned)mask);
    printf("  foreground_jobs=%d\n", sh->interactive ? 1 : 0);
}

static void print_fds(pid_t pid)
{
    char path[64];
    DIR *dir;
    struct dirent *entry;

    snprintf(path, sizeof(path), "/proc/%d/fd", (int)pid);
    dir = opendir(path);
    if (dir == NULL) {
        printf("fds: unavailable (%s)\n", strerror(errno));
        return;
    }
    printf("fds (%s):\n", path);
    while ((entry = readdir(dir)) != NULL) {
        char link[PATH_MAX];
        char full[PATH_MAX];
        ssize_t n;

        if (entry->d_name[0] == '.')
            continue;
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        n = readlink(full, link, sizeof(link) - 1);
        if (n < 0)
            continue;
        link[n] = '\0';
        printf("  %-3s -> %s\n", entry->d_name, link);
    }
    closedir(dir);
}

static void print_status(pid_t pid)
{
    char path[64];
    FILE *fp;
    char line[512];

    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("status: unavailable (%s)\n", strerror(errno));
        return;
    }
    printf("status (%s):\n", path);
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "Name:", 5) == 0 ||
            strncmp(line, "State:", 6) == 0 ||
            strncmp(line, "PPid:", 5) == 0 ||
            strncmp(line, "Threads:", 8) == 0 ||
            strncmp(line, "VmRSS:", 6) == 0 ||
            strncmp(line, "VmSize:", 7) == 0 ||
            strncmp(line, "Uid:", 4) == 0 ||
            strncmp(line, "Gid:", 4) == 0) {
            printf("  %s", line);
        }
    }
    fclose(fp);
}

static void print_maps(pid_t pid)
{
    char path[64];
    FILE *fp;
    char line[PATH_MAX + 128];
    unsigned long total_kb = 0;
    int count = 0;
    int shown = 0;

    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("maps: unavailable (%s)\n", strerror(errno));
        return;
    }
    printf("maps (%s):\n", path);
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long start;
        unsigned long end;
        unsigned long offset;
        char perms[8];
        char rest[PATH_MAX + 64];

        ++count;
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %[^\n]", &start, &end,
                   perms, &offset, rest) >= 4)
            total_kb += (end - start) / 1024;
        if (shown < 10) {
            printf("  %s", line);
            ++shown;
        }
    }
    fclose(fp);
    printf("  ... %d regions, %lu KB virtual\n", count, total_kb);
}

static void print_namespaces(pid_t pid)
{
    static const char *names[] = {
        "mnt", "pid", "user", "uts", "net", "ipc", "cgroup"
    };
    size_t i;

    printf("namespaces:\n");
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char path[128];
        char link[256];
        ssize_t n;

        snprintf(path, sizeof(path), "/proc/%d/ns/%s", (int)pid, names[i]);
        n = readlink(path, link, sizeof(link) - 1);
        if (n < 0) {
            printf("  %-7s unavailable\n", names[i]);
            continue;
        }
        link[n] = '\0';
        printf("  %-7s %s\n", names[i], link);
    }
}

static void print_jobs(shell_state_t *sh)
{
    int i;
    int any = 0;

    printf("jobs:\n");
    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        job_t *job = &sh->jobs[i];

        if (!job->used)
            continue;
        any = 1;
        printf("  [%d] pgid=%d %s status=%d %s\n", job->id, (int)job->pgid,
               job->running ? "running" : "done", job->status,
               job->command != NULL ? job->command : "");
    }
    if (!any)
        printf("  (none)\n");
}

int cmd_inspect(int argc, char **argv, shell_state_t *sh)
{
    int do_fds = 0;
    int do_maps = 0;
    int do_ns = 0;
    int do_jobs = 0;
    int do_status = 0;
    int any_option = 0;
    pid_t pid = getpid();
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "-f") == 0) {
            do_fds = any_option = 1;
        } else if (strcmp(arg, "-m") == 0) {
            do_maps = any_option = 1;
        } else if (strcmp(arg, "-n") == 0) {
            do_ns = any_option = 1;
        } else if (strcmp(arg, "-j") == 0) {
            do_jobs = any_option = 1;
        } else if (strcmp(arg, "-s") == 0) {
            do_status = any_option = 1;
        } else if (arg[0] == '-') {
            msh_usage("inspect [-f|-m|-n|-j|-s] [PID]");
            return 2;
        } else {
            char *end;
            long value = strtol(arg, &end, 10);

            if (*end != '\0' || value <= 0 || value > INT_MAX) {
                msh_error("inspect: invalid pid: %s", arg);
                return 2;
            }
            pid = (pid_t)value;
        }
    }

    if (!any_option) {
        do_status = do_fds = do_maps = do_ns = 1;
        if (pid == getpid())
            do_jobs = 1;
    }
    if (pid == getpid())
        print_shell_identity(sh);
    if (do_status)
        print_status(pid);
    if (do_fds)
        print_fds(pid);
    if (do_maps)
        print_maps(pid);
    if (do_ns)
        print_namespaces(pid);
    if (do_jobs)
        print_jobs(sh);
    if (pid == getpid())
        print_limits();
    return 0;
}
