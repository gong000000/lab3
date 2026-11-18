/*
 * cmd_watch.c - periodically run a command (like watch(1)).
 */
#include "myshell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *join_watch_command(int argc, char **argv, int start)
{
    size_t total = 1;
    int i;
    char *text;
    char *p;

    for (i = start; i < argc; ++i)
        total += strlen(argv[i]) + 1;
    text = msh_malloc(total);
    p = text;
    for (i = start; i < argc; ++i) {
        size_t len = strlen(argv[i]);

        if (i > start)
            *p++ = ' ';
        memcpy(p, argv[i], len);
        p += len;
    }
    *p = '\0';
    return text;
}

int cmd_watch(int argc, char **argv, shell_state_t *sh)
{
    int interval = 2;
    int count = 0;
    int done = 0;
    int i;
    char *command;
    int is_tty = isatty(STDOUT_FILENO);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-n") == 0) {
            char *end = NULL;
            long value;

            if (++i >= argc) {
                msh_error("watch: -n requires a number");
                return 2;
            }
            errno = 0;
            value = strtol(argv[i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                value < 1 || value > 3600) {
                msh_error("watch: invalid interval");
                return 2;
            }
            interval = (int)value;
        } else if (strcmp(argv[i], "-c") == 0) {
            char *end = NULL;
            long value;

            if (++i >= argc) {
                msh_error("watch: -c requires a number");
                return 2;
            }
            errno = 0;
            value = strtol(argv[i], &end, 10);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                value < 0) {
                msh_error("watch: invalid count");
                return 2;
            }
            count = (int)value;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            msh_error("watch: invalid option: %s", argv[i]);
            msh_usage("watch [-n seconds] [-c count] command ...");
            return 2;
        } else {
            break;
        }
    }

    if (i >= argc) {
        msh_usage("watch [-n seconds] [-c count] command ...");
        return 2;
    }
    command = join_watch_command(argc, argv, i);

    while (!done) {
        shell_state_t local;
        time_t now;

        if (is_tty)
            fputs("\033[H\033[2J", stdout);
        now = time(NULL);
        printf("Every %ds: %s    (%s)", interval, command, ctime(&now));
        fflush(stdout);

        local = *sh;
        local.interactive = 0;
        local.should_exit = 0;
        local.exit_status = 0;
        local.last_status = 0;
        local.last_bg_pid = 0;
        local.terminal_fd = -1;
        local.shell_pid = getpid();
        local.shell_pgid = getpgrp();
        memset(local.jobs, 0, sizeof(local.jobs));

        (void)shell_execute_line(command, &local);

        ++done;
        if (count > 0 && done >= count)
            break;
        sleep((unsigned)interval);
    }

    free(command);
    return 0;
}
