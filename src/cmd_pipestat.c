/*
 * cmd_pipestat.c - execute one pipeline with per-stage instrumentation.
 *
 * Each stage writes into a relay process.  The relay counts bytes, forwards
 * them to the next stage (or stdout), and reports the count to the parent.
 */
#include "myshell.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pid_t stage_pid;
    pid_t relay_pid;
    int count_fd;
    int status;
    uint64_t bytes;
    double elapsed;
    struct timespec started;
    char *label;
} stage_stat_t;

static char *join_argv(const command_t *cmd)
{
    size_t total = 1;
    int i;
    char *text;
    char *p;

    for (i = 0; i < cmd->argc; ++i)
        total += strlen(cmd->argv[i]) + 1;
    text = msh_malloc(total);
    p = text;
    for (i = 0; i < cmd->argc; ++i) {
        size_t len = strlen(cmd->argv[i]);

        if (i > 0)
            *p++ = ' ';
        memcpy(p, cmd->argv[i], len);
        p += len;
    }
    *p = '\0';
    return text;
}

static int read_count(int fd, uint64_t *value)
{
    unsigned char buf[sizeof(uint64_t)];
    size_t got = 0;

    while (got < sizeof(buf)) {
        ssize_t n = read(fd, buf + got, sizeof(buf) - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        got += (size_t)n;
    }
    memcpy(value, buf, sizeof(buf));
    return 0;
}

static void close_if(int fd)
{
    if (fd >= 0)
        close(fd);
}

int cmd_pipestat(int argc, char **argv, shell_state_t *sh)
{
    char *text;
    char *error = NULL;
    command_list_t list;
    pipeline_t *pl;
    stage_stat_t *stats;
    int n;
    int i;
    int prev_read = -1;

    if (argc < 2) {
        msh_usage("pipestat 'command1 | command2 ...'");
        return 2;
    }

    {
        size_t total = 1;
        char *p;

        for (i = 1; i < argc; ++i)
            total += strlen(argv[i]) + 1;
        text = msh_malloc(total);
        p = text;
        for (i = 1; i < argc; ++i) {
            size_t len = strlen(argv[i]);

            if (i > 1)
                *p++ = ' ';
            memcpy(p, argv[i], len);
            p += len;
        }
        *p = '\0';
    }

    if (parse_line(text, sh, &list, &error) < 0) {
        msh_error("pipestat: %s", error != NULL ? error : "syntax error");
        free(error);
        free(text);
        return 2;
    }
    free(text);

    if (list.npipelines != 1) {
        msh_error("pipestat: use exactly one pipeline");
        free_command_list(&list);
        return 2;
    }
    pl = &list.pipelines[0];
    if (pl->background) {
        msh_error("pipestat: background pipelines are not supported");
        free_command_list(&list);
        return 2;
    }
    n = pl->ncommands;
    stats = msh_calloc((size_t)n, sizeof(*stats));
    for (i = 0; i < n; ++i)
        stats[i].count_fd = -1;

    for (i = 0; i < n; ++i) {
        command_t *cmd = &pl->commands[i];
        int out_pipe[2] = { -1, -1 };
        int next_pipe[2] = { -1, -1 };
        int count_pipe[2] = { -1, -1 };
        pid_t relay;
        pid_t stage;
        struct timespec start;

        if (pipe(out_pipe) < 0 || pipe(count_pipe) < 0) {
            msh_perror("pipestat: pipe");
            goto fail;
        }
        if (i < n - 1 && pipe(next_pipe) < 0) {
            msh_perror("pipestat: pipe");
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(count_pipe[0]);
            close(count_pipe[1]);
            goto fail;
        }

        relay = fork();
        if (relay < 0) {
            msh_perror("pipestat: fork");
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(count_pipe[0]);
            close(count_pipe[1]);
            if (i < n - 1) {
                close(next_pipe[0]);
                close(next_pipe[1]);
            }
            goto fail;
        }
        if (relay == 0) {
            uint64_t total = 0;
            char buf[65536];

            (void)signal(SIGPIPE, SIG_IGN);
            close(out_pipe[1]);
            close(count_pipe[0]);
            close_if(prev_read);
            if (i < n - 1)
                close(next_pipe[0]);

            for (;;) {
                ssize_t got = read(out_pipe[0], buf, sizeof(buf));

                if (got < 0) {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                if (got == 0)
                    break;
                total += (uint64_t)got;
                if (i < n - 1) {
                    if (msh_write_all(next_pipe[1], buf, (size_t)got) < 0)
                        break;
                } else if (msh_write_all(STDOUT_FILENO, buf, (size_t)got) < 0) {
                    break;
                }
            }
            (void)msh_write_all(count_pipe[1], &total, sizeof(total));
            close(out_pipe[0]);
            close(count_pipe[1]);
            if (i < n - 1)
                close(next_pipe[1]);
            _exit(0);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
        stage = fork();
        if (stage < 0) {
            msh_perror("pipestat: fork");
            kill(relay, SIGKILL);
            waitpid(relay, NULL, 0);
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(count_pipe[0]);
            close(count_pipe[1]);
            if (i < n - 1) {
                close(next_pipe[0]);
                close(next_pipe[1]);
            }
            goto fail;
        }
        if (stage == 0) {
            (void)setpgid(0, 0);
            reset_child_signals();
            __fpurge(stdin);
            if (prev_read != -1) {
                if (dup2(prev_read, STDIN_FILENO) < 0)
                    _exit(127);
                close(prev_read);
            }
            if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
                _exit(127);
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(count_pipe[0]);
            close(count_pipe[1]);
            if (i < n - 1) {
                close(next_pipe[0]);
                close(next_pipe[1]);
            }
            if (apply_redirections(&cmd->redir) < 0)
                _exit(1);
            if (is_builtin(cmd->argv[0])) {
                int rc = run_builtin(cmd->argc, cmd->argv, sh, 1);
                fflush(stdout);
                fflush(stderr);
                _exit(rc & 0xff);
            }
            execvp(cmd->argv[0], cmd->argv);
            _exit(127);
        }

        (void)setpgid(stage, stage);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(count_pipe[1]);
        if (i < n - 1)
            close(next_pipe[1]);
        close_if(prev_read);
        prev_read = (i < n - 1) ? next_pipe[0] : -1;

        stats[i].stage_pid = stage;
        stats[i].relay_pid = relay;
        stats[i].count_fd = count_pipe[0];
        stats[i].label = join_argv(cmd);
        stats[i].started = start;
    }

    close_if(prev_read);
    for (i = 0; i < n; ++i) {
        int status;
        pid_t waited;
        struct timespec end;

        do {
            waited = waitpid(stats[i].stage_pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0)
            status = 0;
        stats[i].status = WIFEXITED(status) ? WEXITSTATUS(status)
                        : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
        do {
            waited = waitpid(stats[i].relay_pid, NULL, 0);
        } while (waited < 0 && errno == EINTR);
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats[i].elapsed = (double)(end.tv_sec - stats[i].started.tv_sec) +
                           (double)(end.tv_nsec - stats[i].started.tv_nsec) / 1e9;
        if (read_count(stats[i].count_fd, &stats[i].bytes) < 0)
            stats[i].bytes = 0;
        close(stats[i].count_fd);
        stats[i].count_fd = -1;
    }

    printf("\n");
    for (i = 0; i < n; ++i)
        printf("[%d] %s: %llu bytes, %.3fs, status %d\n", i + 1,
               stats[i].label, (unsigned long long)stats[i].bytes,
               stats[i].elapsed, stats[i].status);

    for (i = 0; i < n; ++i)
        free(stats[i].label);
    i = stats[n - 1].status;
    free(stats);
    free_command_list(&list);
    return i;

fail:
    for (i = 0; i < n; ++i) {
        close_if(stats[i].count_fd);
        if (stats[i].stage_pid > 0)
            kill(stats[i].stage_pid, SIGKILL);
        if (stats[i].relay_pid > 0)
            kill(stats[i].relay_pid, SIGKILL);
    }
    for (i = 0; i < n; ++i) {
        if (stats[i].stage_pid > 0)
            waitpid(stats[i].stage_pid, NULL, 0);
        if (stats[i].relay_pid > 0)
            waitpid(stats[i].relay_pid, NULL, 0);
        free(stats[i].label);
    }
    free(stats);
    free_command_list(&list);
    return 1;
}
