/*
 * jobs.c - background job bookkeeping
 */
#include "myshell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void job_clear(job_t *job)
{
    free(job->pids);
    free(job->done);
    free(job->command);
    memset(job, 0, sizeof(*job));
}

int job_add(shell_state_t *sh, pid_t pgid, const pid_t *pids, int npids,
            const char *command)
{
    job_t *slot = NULL;
    int i;

    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        if (!sh->jobs[i].used) {
            slot = &sh->jobs[i];
            break;
        }
    }
    if (slot == NULL) {
        for (i = 0; i < MSH_MAX_JOBS; ++i) {
            if (!sh->jobs[i].running) {
                slot = &sh->jobs[i];
                break;
            }
        }
    }
    if (slot == NULL) {
        msh_error("too many background jobs");
        return -1;
    }

    job_clear(slot);
    slot->used = 1;
    slot->id = sh->next_job_id++;
    slot->pgid = pgid;
    slot->npids = npids;
    slot->pids = msh_malloc((size_t)npids * sizeof(pid_t));
    slot->done = msh_calloc((size_t)npids, sizeof(int));
    memcpy(slot->pids, pids, (size_t)npids * sizeof(pid_t));
    slot->ndone = 0;
    slot->running = 1;
    slot->stopped = 0;
    slot->status = 0;
    slot->command = msh_strdup(command != NULL ? command : "");
    return slot->id;
}

void job_reap(shell_state_t *sh)
{
    int status;
    pid_t pid;

    for (;;) {
        int i, j;

        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            if (pid < 0 && errno == EINTR)
                continue;
            break;
        }

        for (i = 0; i < MSH_MAX_JOBS; ++i) {
            job_t *job = &sh->jobs[i];

            if (!job->used || !job->running)
                continue;
            for (j = 0; j < job->npids; ++j) {
                if (job->pids[j] == pid && !job->done[j]) {
                    job->done[j] = 1;
                    job->ndone++;
                    if (WIFEXITED(status))
                        job->status = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status))
                        job->status = 128 + WTERMSIG(status);
                    if (job->ndone >= job->npids) {
                        job->running = 0;
                        if (sh->interactive)
                            printf("[%d] Done\t%s\n", job->id,
                                   job->command);
                    }
                    goto next_pid;
                }
            }
        }
next_pid:
        ;
    }
}

void job_print_all(shell_state_t *sh)
{
    int i;

    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        job_t *job = &sh->jobs[i];

        if (!job->used)
            continue;
        printf("[%d]  %-8s %s\n", job->id,
               job->running ? "Running" : "Done", job->command);
    }
}

void job_mark_all_done(shell_state_t *sh, int status)
{
    int i;

    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        job_t *job = &sh->jobs[i];

        if (job->used) {
            job->running = 0;
            job->status = status;
        }
    }
}

void job_cleanup(shell_state_t *sh)
{
    int i;

    /* Send SIGHUP to still-running children, as a login shell does. */
    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        job_t *job = &sh->jobs[i];

        if (job->used && job->running) {
            if (job->pgid > 0)
                (void)kill(-job->pgid, SIGHUP);
            else if (job->npids > 0)
                (void)kill(job->pids[0], SIGHUP);
        }
    }

    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        if (sh->jobs[i].used)
            job_clear(&sh->jobs[i]);
    }
}
