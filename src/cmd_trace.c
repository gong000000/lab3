/*
 * cmd_trace.c - ptrace-based system call tracer (x86_64).
 *
 * Usage: trace [-c] [-f] [-e NAME[,NAME...]] command [args...]
 *
 * The tracer uses PTRACE_TRACEME in the child, then single-steps through
 * system call entry and exit stops.  -f follows fork/vfork/clone children,
 * -c prints a call summary, and -e restricts output to the named syscalls.
 */
#include "myshell.h"
#include "trace_syscalls.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

#define TRACE_MAX_PIDS 256
#define TRACE_MAX_NSYS 512

typedef struct {
    int used;
    pid_t pid;
    int entered;
    long nr;
    unsigned long args[6];
    char string_arg[512];
} tracee_t;

typedef struct {
    char **names;
    int count;
} filter_t;

typedef struct {
    const char *name;
    unsigned long long count;
} summary_t;

static const char *trace_name(long nr)
{
    if (nr >= 0 && nr < TRACE_MAX_NSYS && trace_syscall_names[nr] != NULL)
        return trace_syscall_names[nr];
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Filters                                                            */
/* ------------------------------------------------------------------ */

static void filter_add(filter_t *filter, const char *name, size_t len)
{
    char *copy;

    if (len == 0)
        return;
    if (len >= 6 && strncmp(name, "trace=", 6) == 0) {
        name += 6;
        len -= 6;
    }
    copy = msh_strndup(name, len);
    filter->names = msh_realloc(filter->names,
                                (size_t)(filter->count + 1) *
                                sizeof(char *));
    filter->names[filter->count++] = copy;
}

static void filter_parse(filter_t *filter, const char *spec)
{
    const char *start = spec;
    const char *p = spec;

    if (strncmp(p, "trace=", 6) == 0)
        start = p = spec + 6;
    for (;;) {
        if (*p == ',' || *p == '\0') {
            filter_add(filter, start, (size_t)(p - start));
            if (*p == '\0')
                break;
            start = p + 1;
        }
        ++p;
    }
}

static int filter_match(const filter_t *filter, const char *name)
{
    int i;

    if (filter->count == 0)
        return 1;
    for (i = 0; i < filter->count; ++i) {
        if (strcmp(filter->names[i], name) == 0)
            return 1;
    }
    return 0;
}

static void filter_free(filter_t *filter)
{
    int i;

    for (i = 0; i < filter->count; ++i)
        free(filter->names[i]);
    free(filter->names);
    memset(filter, 0, sizeof(*filter));
}

/* ------------------------------------------------------------------ */
/* Argument formatting                                                */
/* ------------------------------------------------------------------ */

static int syscall_argc(long nr)
{
    switch (nr) {
    case SYS_getpid: case SYS_getppid: case SYS_getuid: case SYS_geteuid:
    case SYS_getgid: case SYS_getegid: case SYS_gettid: case SYS_setsid:
    case SYS_fork: case SYS_vfork: case SYS_pause: case SYS_sync:
    case SYS_sched_yield: case SYS_restart_syscall:
        return 0;
    case SYS_close: case SYS_dup: case SYS_brk: case SYS_chdir:
    case SYS_chroot: case SYS_exit: case SYS_exit_group: case SYS_setuid:
    case SYS_setgid: case SYS_umask: case SYS_unlink: case SYS_rmdir:
    case SYS_fsync: case SYS_fdatasync: case SYS_pipe: case SYS_getpgrp:
    case SYS_setpgid: case SYS_times: case SYS_uname: case SYS_set_tid_address:
        return 1;
    case SYS_stat: case SYS_fstat: case SYS_lstat: case SYS_access:
    case SYS_rename: case SYS_mkdir: case SYS_kill: case SYS_dup2:
    case SYS_getdents: case SYS_chmod: case SYS_fchmod: case SYS_symlink:
    case SYS_readlink: case SYS_munmap: case SYS_ftruncate: case SYS_truncate:
    case SYS_getcwd: case SYS_lseek: case SYS_getpgid: case SYS_getrlimit:
    case SYS_setrlimit: case SYS_getrusage: case SYS_setpriority:
    case SYS_getpriority: case SYS_statfs: case SYS_fstatfs: case SYS_socketpair:
        return 2;
    case SYS_read: case SYS_write: case SYS_open: case SYS_ioctl:
    case SYS_poll: case SYS_creat: case SYS_chown: case SYS_fchown:
    case SYS_lchown: case SYS_setsockopt: case SYS_getsockopt:
    case SYS_readv: case SYS_writev: case SYS_mprotect: case SYS_madvise:
    case SYS_dup3: case SYS_socket: case SYS_connect: case SYS_bind:
    case SYS_listen: case SYS_accept: case SYS_shutdown:
        return 3;
    case SYS_openat: case SYS_newfstatat: case SYS_unlinkat:
    case SYS_mkdirat: case SYS_faccessat: case SYS_fchmodat:
    case SYS_fchownat: case SYS_renameat: case SYS_readlinkat:
    case SYS_symlinkat: case SYS_utimensat: case SYS_epoll_wait:
    case SYS_epoll_ctl: case SYS_tgkill: case SYS_wait4:
        return 4;
    case SYS_clone: case SYS_prctl: case SYS_select: case SYS_mount:
        return 5;
    case SYS_mmap: case SYS_futex: case SYS_sendto: case SYS_recvfrom:
    case SYS_sendmsg: case SYS_recvmsg:
        return 6;
    default:
        return 3;
    }
}

static int syscall_string_arg(long nr)
{
    switch (nr) {
    case SYS_open: case SYS_stat: case SYS_lstat: case SYS_access:
    case SYS_chdir: case SYS_chmod: case SYS_chown: case SYS_unlink:
    case SYS_rmdir: case SYS_mkdir: case SYS_execve: case SYS_truncate:
    case SYS_statfs: case SYS_readlink: case SYS_symlink: case SYS_creat:
        return 0;
    case SYS_rename:
        return 0;
    case SYS_openat: case SYS_newfstatat: case SYS_unlinkat:
    case SYS_mkdirat: case SYS_faccessat: case SYS_fchmodat:
    case SYS_fchownat: case SYS_readlinkat: case SYS_symlinkat:
        return 1;
    default:
        return -1;
    }
}

static void read_child_string(pid_t pid, unsigned long address, char *buffer,
                              size_t size)
{
    size_t done = 0;

    buffer[0] = '\0';
    if (address == 0 || size == 0)
        return;
    while (done + sizeof(long) <= size) {
        long word;
        void *where = (void *)(address + done);

        errno = 0;
        word = ptrace(PTRACE_PEEKDATA, pid, where, NULL);
        if (errno != 0)
            break;
        memcpy(buffer + done, &word, sizeof(long));
        if (memchr(buffer + done, '\0', sizeof(long)) != NULL)
            break;
        done += sizeof(long);
    }
    buffer[size - 1] = '\0';
}

static void print_quoted(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    putchar('"');
    while (*p != '\0') {
        if (*p == '\\' || *p == '"') {
            putchar('\\');
            putchar(*p);
        } else if (*p >= 32 && *p < 127) {
            putchar(*p);
        } else {
            printf("\\x%02x", *p);
        }
        ++p;
    }
    putchar('"');
}

static void print_syscall(pid_t pid, int show_pid, long nr,
                          const unsigned long *args, const char *string_arg,
                          long result, int has_result)
{
    const char *name = trace_name(nr);
    int argc = syscall_argc(nr);
    int string_index = syscall_string_arg(nr);
    int i;

    if (show_pid)
        printf("[%5d] ", (int)pid);
    printf("%s(", name);
    for (i = 0; i < argc; ++i) {
        if (i > 0)
            fputs(", ", stdout);
        if (i == string_index) {
            print_quoted(string_arg != NULL ? string_arg : "");
        } else if (args[i] < 0x100000000UL) {
            printf("%lu", args[i]);
        } else {
            printf("0x%lx", args[i]);
        }
    }
    putchar(')');
    if (has_result) {
        if (result < 0 && result >= -4095)
            printf(" = -1 (%s)", strerror((int)-result));
        else
            printf(" = %ld", result);
    }
    putchar('\n');
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Tracee table                                                       */
/* ------------------------------------------------------------------ */

static int find_tracee(tracee_t *tracees, pid_t pid)
{
    int i;

    for (i = 0; i < TRACE_MAX_PIDS; ++i) {
        if (tracees[i].used && tracees[i].pid == pid)
            return i;
    }
    return -1;
}

static void add_tracee(tracee_t *tracees, pid_t pid)
{
    int i;

    if (find_tracee(tracees, pid) >= 0)
        return;
    for (i = 0; i < TRACE_MAX_PIDS; ++i) {
        if (!tracees[i].used) {
            tracees[i].used = 1;
            tracees[i].pid = pid;
            tracees[i].entered = 0;
            tracees[i].nr = 0;
            return;
        }
    }
}

static int count_tracees(tracee_t *tracees)
{
    int i;
    int count = 0;

    for (i = 0; i < TRACE_MAX_PIDS; ++i) {
        if (tracees[i].used)
            ++count;
    }
    return count;
}

static int summary_cmp(const void *left, const void *right)
{
    const summary_t *a = left;
    const summary_t *b = right;

    if (a->count != b->count)
        return a->count < b->count ? 1 : -1;
    return strcmp(a->name, b->name);
}

static void print_summary(unsigned long long *counts)
{
    summary_t items[TRACE_MAX_NSYS];
    int count = 0;
    unsigned long long total = 0;
    int i;

    for (i = 0; i < TRACE_MAX_NSYS; ++i) {
        if (counts[i] > 0) {
            items[count].name = trace_name(i);
            items[count].count = counts[i];
            total += counts[i];
            ++count;
        }
    }
    qsort(items, (size_t)count, sizeof(items[0]), summary_cmp);

    printf("\n%8s  %s\n", "calls", "syscall");
    for (i = 0; i < count; ++i)
        printf("%8llu  %-24s %5.1f%%\n", items[i].count, items[i].name,
               total > 0 ? 100.0 * (double)items[i].count / (double)total
                         : 0.0);
    printf("%8llu  %-24s\n", total, "total");
}

/* ------------------------------------------------------------------ */
/* Tracer                                                             */
/* ------------------------------------------------------------------ */

static int run_trace(char **command, int follow, int summary,
                     const filter_t *filter)
{
    tracee_t tracees[TRACE_MAX_PIDS];
    unsigned long long *counts;
    pid_t child;
    int status;
    int options;
    int root_status = 0;
    int root_done = 0;

    memset(tracees, 0, sizeof(tracees));
    counts = msh_calloc(TRACE_MAX_NSYS, sizeof(*counts));

    child = fork();
    if (child < 0) {
        msh_perror("fork");
        free(counts);
        return 1;
    }
    if (child == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            msh_perror("ptrace");
            _exit(127);
        }
        raise(SIGSTOP);
        execvp(command[0], command);
        fprintf(stderr, "%s: %s: %s\n", MSH_NAME, command[0],
                strerror(errno));
        _exit(127);
    }

    if (waitpid(child, &status, 0) < 0 || !WIFSTOPPED(status)) {
        msh_error("trace: child did not stop");
        free(counts);
        return 1;
    }

    options = PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL;
    if (follow)
        options |= PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                   PTRACE_O_TRACECLONE;
    if (ptrace(PTRACE_SETOPTIONS, child, NULL, (void *)(long)options) < 0) {
        msh_perror("ptrace SETOPTIONS");
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        free(counts);
        return 1;
    }

    add_tracee(tracees, child);
    if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) < 0) {
        msh_perror("ptrace SYSCALL");
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        free(counts);
        return 1;
    }

    while (count_tracees(tracees) > 0) {
        pid_t pid;
        int index;

        pid = waitpid(-1, &status, __WALL);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        index = find_tracee(tracees, pid);
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (pid == child) {
                root_done = 1;
                if (WIFEXITED(status))
                    root_status = WEXITSTATUS(status);
                else
                    root_status = 128 + WTERMSIG(status);
            }
            if (index >= 0)
                tracees[index].used = 0;
            continue;
        }
        if (!WIFSTOPPED(status))
            continue;

        {
            int sig = WSTOPSIG(status);
            int event = status >> 16;

            if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK ||
                event == PTRACE_EVENT_CLONE) {
                unsigned long message = 0;

                (void)ptrace(PTRACE_GETEVENTMSG, pid, NULL, &message);
                add_tracee(tracees, (pid_t)message);
                (void)ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
                continue;
            }

            if (index < 0) {
                (void)ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
                continue;
            }

            if (sig == (SIGTRAP | 0x80)) {
                struct user_regs_struct regs;
                tracee_t *t = &tracees[index];

                memset(&regs, 0, sizeof(regs));
                if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
                    (void)ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
                    continue;
                }

                if (!t->entered) {
                    const char *name;
                    int string_index;

                    t->entered = 1;
                    t->nr = (long)regs.orig_rax;
                    t->args[0] = (unsigned long)regs.rdi;
                    t->args[1] = (unsigned long)regs.rsi;
                    t->args[2] = (unsigned long)regs.rdx;
                    t->args[3] = (unsigned long)regs.r10;
                    t->args[4] = (unsigned long)regs.r8;
                    t->args[5] = (unsigned long)regs.r9;
                    t->string_arg[0] = '\0';

                    string_index = syscall_string_arg(t->nr);
                    if (string_index >= 0)
                        read_child_string(pid, t->args[string_index],
                                          t->string_arg,
                                          sizeof(t->string_arg));

                    name = trace_name(t->nr);
                    if (t->nr == SYS_exit || t->nr == SYS_exit_group) {
                        if (filter_match(filter, name)) {
                            print_syscall(pid, follow, t->nr, t->args,
                                          t->string_arg, 0, 0);
                            counts[t->nr < TRACE_MAX_NSYS ? t->nr : 0]++;
                        }
                    }
                } else {
                    const char *name = trace_name(t->nr);

                    t->entered = 0;
                    if (filter_match(filter, name)) {
                        print_syscall(pid, follow, t->nr, t->args,
                                      t->string_arg, (long)regs.rax, 1);
                        counts[t->nr < TRACE_MAX_NSYS ? t->nr : 0]++;
                    }
                }
                (void)ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
            } else {
                int deliver = sig;

                /* SIGTRAP after exec and the initial SIGSTOP of a newly
                 * attached child are tracer artifacts, not real signals. */
                if (sig == SIGSTOP || sig == SIGTRAP)
                    deliver = 0;
                (void)ptrace(PTRACE_SYSCALL, pid, NULL,
                             (void *)(long)deliver);
            }
        }
    }

    if (summary)
        print_summary(counts);
    free(counts);
    return root_done ? root_status : 0;
}

/* ------------------------------------------------------------------ */
/* Builtin entry point                                                */
/* ------------------------------------------------------------------ */

int cmd_trace(int argc, char **argv, shell_state_t *sh)
{
    int follow = 0;
    int summary = 0;
    filter_t filter = { 0 };
    int i = 1;
    int rc;

    (void)sh;
    while (i < argc) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }
        if (arg[0] != '-' || arg[1] == '\0')
            break;
        if (strcmp(arg, "-c") == 0) {
            summary = 1;
        } else if (strcmp(arg, "-f") == 0) {
            follow = 1;
        } else if (strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                msh_usage("trace [-c] [-f] [-e NAME[,NAME...]] command ...");
                filter_free(&filter);
                return 2;
            }
            filter_parse(&filter, argv[++i]);
        } else if (strncmp(arg, "-e", 2) == 0) {
            filter_parse(&filter, arg + 2);
        } else {
            msh_usage("trace [-c] [-f] [-e NAME[,NAME...]] command ...");
            filter_free(&filter);
            return 2;
        }
        ++i;
    }

    if (i >= argc) {
        msh_usage("trace [-c] [-f] [-e NAME[,NAME...]] command ...");
        filter_free(&filter);
        return 2;
    }

    rc = run_trace(argv + i, follow, summary, &filter);
    filter_free(&filter);
    return rc;
}
