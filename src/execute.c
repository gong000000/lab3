/*
 * execute.c - process creation, pipes, redirection, and waiting
 */
#include "myshell.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int has_redirections(const redirect_t *redir)
{
    return redir->in_path != NULL || redir->out_path != NULL ||
           redir->err_path != NULL || redir->err_to_out || redir->merge_err;
}

int apply_redirections(const redirect_t *redir)
{
    int fd;

    if (redir->in_path != NULL) {
        fd = open(redir->in_path, O_RDONLY);
        if (fd < 0) {
            msh_perror(redir->in_path);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            msh_perror("dup2");
            close(fd);
            return -1;
        }
        if (fd != STDIN_FILENO)
            close(fd);
    }

    if (redir->out_path != NULL) {
        int flags = O_WRONLY | O_CREAT |
                    (redir->out_append ? O_APPEND : O_TRUNC);

        fd = open(redir->out_path, flags, 0666);
        if (fd < 0) {
            msh_perror(redir->out_path);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            msh_perror("dup2");
            close(fd);
            return -1;
        }
        if (fd != STDOUT_FILENO)
            close(fd);
        if (redir->merge_err && dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            msh_perror("dup2");
            return -1;
        }
    }

    if (redir->err_path != NULL) {
        int flags = O_WRONLY | O_CREAT |
                    (redir->err_append ? O_APPEND : O_TRUNC);

        fd = open(redir->err_path, flags, 0666);
        if (fd < 0) {
            msh_perror(redir->err_path);
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            msh_perror("dup2");
            close(fd);
            return -1;
        }
        if (fd != STDERR_FILENO)
            close(fd);
    }

    if (redir->err_to_out && dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        msh_perror("dup2");
        return -1;
    }

    return 0;
}

void reset_child_signals(void)
{
    static const int signals[] = {
        SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU,
        SIGCHLD, SIGPIPE, SIGTERM, SIGHUP
    };
    struct sigaction sa;
    sigset_t empty;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        (void)sigaction(signals[i], &sa, NULL);

    sigemptyset(&empty);
    (void)sigprocmask(SIG_SETMASK, &empty, NULL);
}

static int status_to_exit(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

/* Run a built-in that must affect the shell itself (cd, exit, jobs, ...).
 * Redirections are applied temporarily and then restored. */
static int run_parent_builtin(command_t *cmd, shell_state_t *sh)
{
    int saved[3] = { -1, -1, -1 };
    int redirected = has_redirections(&cmd->redir);
    int rc;
    int i;
    int restore_ok = 1;

    if (redirected) {
        for (i = 0; i < 3; ++i) {
            saved[i] = dup(i);
            if (saved[i] < 0) {
                msh_perror("dup");
                restore_ok = 0;
                break;
            }
        }
        if (!restore_ok) {
            for (i = 0; i < 3; ++i) {
                if (saved[i] >= 0)
                    close(saved[i]);
            }
            return 1;
        }
        if (apply_redirections(&cmd->redir) < 0) {
            for (i = 0; i < 3; ++i)
                (void)dup2(saved[i], i);
            for (i = 0; i < 3; ++i)
                close(saved[i]);
            return 1;
        }
    }

    rc = run_builtin(cmd->argc, cmd->argv, sh, 0);
    fflush(stdout);
    fflush(stderr);

    if (redirected) {
        for (i = 0; i < 3; ++i) {
            if (dup2(saved[i], i) < 0)
                restore_ok = 0;
            close(saved[i]);
        }
        if (!restore_ok)
            msh_error("failed to restore standard file descriptors");
    }
    return rc;
}

int execute_pipeline(pipeline_t *pl, shell_state_t *sh)
{
    int n;
    pid_t *pids;
    pid_t pgid = 0;
    int prev_read = -1;
    int i;
    int last_status = 0;

    if (pl == NULL || pl->ncommands <= 0)
        return 1;
    n = pl->ncommands;

    if (!pl->background && n == 1) {
        command_t *cmd = &pl->commands[0];

        if (is_parent_builtin(cmd->argv[0])) {
            int rc = run_parent_builtin(cmd, sh);
            sh->last_status = rc;
            return rc;
        }
    }

    pids = msh_calloc((size_t)n, sizeof(pid_t));

    for (i = 0; i < n; ++i) {
        command_t *cmd = &pl->commands[i];
        int pipefd[2] = { -1, -1 };
        pid_t pid;

        if (i < n - 1) {
            if (pipe(pipefd) < 0) {
                msh_perror("pipe");
                goto fork_failed;
            }
        }

        pid = fork();
        if (pid < 0) {
            msh_perror("fork");
            if (pipefd[0] >= 0) {
                close(pipefd[0]);
                close(pipefd[1]);
            }
            goto fork_failed;
        }

        if (pid == 0) {
            /* ---------------- child ---------------- */
            pid_t child_pgid = (pgid == 0) ? getpid() : pgid;

            (void)setpgid(0, child_pgid);
            reset_child_signals();
            __fpurge(stdin);

            if (prev_read != -1) {
                if (dup2(prev_read, STDIN_FILENO) < 0) {
                    msh_perror("dup2");
                    _exit(1);
                }
                close(prev_read);
            }
            if (i < n - 1) {
                close(pipefd[0]);
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                    msh_perror("dup2");
                    _exit(1);
                }
                close(pipefd[1]);
            }

            if (apply_redirections(&cmd->redir) < 0)
                _exit(1);

            if (is_builtin(cmd->argv[0])) {
                int rc = run_builtin(cmd->argc, cmd->argv, sh, 1);
                fflush(stdout);
                fflush(stderr);
                _exit(rc);
            }

            execvp(cmd->argv[0], cmd->argv);
            fprintf(stderr, "%s: %s: %s\n", MSH_NAME, cmd->argv[0],
                    strerror(errno));
            _exit(127);
        }

        /* ---------------- parent ---------------- */
        pids[i] = pid;
        if (pgid == 0)
            pgid = pid;
        if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH)
            msh_perror("setpgid");

        if (sh->interactive && !pl->background && i == 0)
            (void)tcsetpgrp(sh->terminal_fd, pgid);

        if (prev_read != -1)
            close(prev_read);
        if (i < n - 1) {
            close(pipefd[1]);
            prev_read = pipefd[0];
        }
    }

    if (prev_read != -1)
        close(prev_read);

    if (pl->background) {
        int id = job_add(sh, pgid, pids, n, pl->text);

        sh->last_bg_pid = pgid;
        if (sh->interactive) {
            if (id >= 0)
                printf("[%d] %d\n", id, (int)pgid);
            else
                printf("[?] %d\n", (int)pgid);
        }
        free(pids);
        sh->last_status = 0;
        return 0;
    }

    if (sh->interactive)
        (void)tcsetpgrp(sh->terminal_fd, pgid);

    for (i = 0; i < n; ++i) {
        int status = 0;
        pid_t waited;

        do {
            waited = waitpid(pids[i], &status, 0);
        } while (waited < 0 && errno == EINTR);

        if (waited < 0) {
            msh_perror("waitpid");
            last_status = 1;
        } else {
            last_status = status_to_exit(status);
            if (i == n - 1 && sh->interactive) {
                if (WIFSIGNALED(status)) {
                    int sig = WTERMSIG(status);

                    if (sig == SIGINT) {
                        putchar('\n');
                    } else if (sig == SIGQUIT) {
                        if (WCOREDUMP(status))
                            printf("Quit (core dumped)\n");
                        else
                            printf("Quit\n");
                    }
                    fflush(stdout);
                }
            }
        }
    }

    if (sh->interactive)
        (void)tcsetpgrp(sh->terminal_fd, sh->shell_pgid);

    free(pids);
    sh->last_status = last_status;
    return last_status;

fork_failed:
    if (pgid > 0)
        (void)kill(-pgid, SIGTERM);
    for (i = 0; i < n; ++i) {
        if (pids[i] > 0)
            (void)waitpid(pids[i], NULL, 0);
    }
    if (prev_read != -1)
        close(prev_read);
    free(pids);
    sh->last_status = 1;
    return 1;
}

int execute_command_list(command_list_t *list, shell_state_t *sh)
{
    int i;
    int last = sh->last_status;

    for (i = 0; i < list->npipelines; ++i) {
        connector_t connector = list->connectors != NULL
                                    ? list->connectors[i]
                                    : CONN_ALWAYS;

        if (sh->should_exit)
            break;
        if (i > 0) {
            if (connector == CONN_AND && last != 0)
                continue;
            if (connector == CONN_OR && last == 0)
                continue;
        }
        last = execute_pipeline(&list->pipelines[i], sh);
    }
    sh->last_status = last;
    return last;
}
