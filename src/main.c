/*
 * main.c - REPL, signal handling, and terminal/job-control setup
 *
 * The interactive loop follows the simple shell on textbook page 357:
 * read a line, parse it, fork, execute, and wait.  The extensions add
 * built-ins, redirection, pipes, background jobs, and signal handling.
 */
#include "myshell.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static shell_state_t *g_shell = NULL;
static volatile sig_atomic_t g_terminate = 0;

/* ------------------------------------------------------------------ */
/* Signals                                                            */
/* ------------------------------------------------------------------ */

static void on_interrupt(int sig)
{
    ssize_t ignored;

    (void)sig;
    /* The terminal driver echoes ^C; start the next prompt on a new line. */
    ignored = write(STDOUT_FILENO, "\n", 1);
    (void)ignored;
}

static void on_quit(int sig)
{
    ssize_t ignored;

    (void)sig;
    ignored = write(STDOUT_FILENO, "\n", 1);
    (void)ignored;
}

static void on_terminate(int sig)
{
    (void)sig;
    g_terminate = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                /* interrupt getline() */

    sa.sa_handler = on_interrupt;
    (void)sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = on_quit;
    (void)sigaction(SIGQUIT, &sa, NULL);

    sa.sa_handler = on_terminate;
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);

    /* These signals are managed explicitly by the shell. */
    (void)signal(SIGTSTP, SIG_IGN);
    (void)signal(SIGTTIN, SIG_IGN);
    (void)signal(SIGTTOU, SIG_IGN);
    (void)signal(SIGPIPE, SIG_IGN);
}

/* ------------------------------------------------------------------ */
/* Terminal                                                           */
/* ------------------------------------------------------------------ */

static void restore_terminal(void)
{
    if (g_shell != NULL && g_shell->termios_modified) {
        (void)tcsetattr(g_shell->terminal_fd, TCSANOW,
                        &g_shell->saved_termios);
        g_shell->termios_modified = 0;
    }
}

static void init_shell(shell_state_t *sh, int allow_interactive)
{
    memset(sh, 0, sizeof(*sh));
    sh->shell_pid = getpid();
    sh->shell_pgid = getpgrp();
    sh->terminal_fd = STDIN_FILENO;
    sh->next_job_id = 1;
    sh->last_status = 0;
    sh->exit_status = 0;
    g_shell = sh;

    sh->interactive = allow_interactive && isatty(STDIN_FILENO);
    if (!sh->interactive) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            (void)setenv("PWD", cwd, 1);
        return;
    }

    (void)atexit(restore_terminal);

    /* If the shell was started in the background, put it in its own
     * process group and give it the terminal. */
    {
        pid_t tpgid = tcgetpgrp(STDIN_FILENO);

        if (tpgid >= 0 && sh->shell_pgid != tpgid) {
            (void)setpgid(0, 0);
            sh->shell_pgid = getpid();
            (void)tcsetpgrp(STDIN_FILENO, sh->shell_pgid);
        }
    }

    if (tcgetattr(STDIN_FILENO, &sh->saved_termios) == 0) {
        struct termios t = sh->saved_termios;

        /* The assignment maps Ctrl-Z to SIGQUIT.  On a normal Linux
         * terminal Ctrl-Z is VSUSP (SIGTSTP); remap VQUIT to Ctrl-Z and
         * disable VSUSP so Ctrl-Z delivers SIGQUIT as requested. */
        t.c_cc[VQUIT] = 0x1a;       /* Ctrl-Z */
#ifdef VSUSP
        t.c_cc[VSUSP] = _POSIX_VDISABLE;
#endif
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
            sh->termios_modified = 1;
    }

    {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            (void)setenv("PWD", cwd, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Line execution                                                     */
/* ------------------------------------------------------------------ */

/* Find the next top-level ';' or '&' in a command line.  Quoted and
 * escaped separators are ignored, and "#" starts a comment at the
 * beginning of a word.  Returns NULL when the rest of the line is one
 * segment. */
static char *find_separator(char *start, int *background)
{
    char *p;
    int in_single = 0;
    int in_double = 0;
    int at_word_start = 1;

    *background = 0;
    for (p = start; *p != '\0'; ++p) {
        char c = *p;

        if (c == '\\' && !in_single) {
            if (p[1] != '\0')
                ++p;
            at_word_start = 0;
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
            at_word_start = 0;
            continue;
        }
        if (c == '"') {
            in_double = 1;
            at_word_start = 0;
            continue;
        }
        if (c == '#' && at_word_start)
            return NULL;            /* comment: no more separators */
        if (c == ';') {
            *background = 0;
            return p;
        }
        if (c == '&') {
            /* &> and &>> are redirections, not control operators. */
            if (p[1] == '>') {
                at_word_start = 0;
                continue;
            }
            /* Leave && intact so the parser reports a syntax error. */
            if (p[1] == '&') {
                ++p;
                at_word_start = 0;
                continue;
            }
            /* Leave >& and 2>&1 intact (unsupported, parser error). */
            if (p > start && p[-1] == '>') {
                at_word_start = 0;
                continue;
            }
            *background = 1;
            return p;
        }
        at_word_start = isspace((unsigned char)c) ? 1 : 0;
    }
    return NULL;
}

static int execute_line(const char *line, shell_state_t *sh)
{
    char *copy = msh_strdup(line);
    char *cursor = copy;
    int overall = 0;

    while (*cursor != '\0' && !sh->should_exit) {
        int background;
        char *segment = cursor;
        char *separator = find_separator(cursor, &background);
        char *to_parse;
        command_list_t list;
        char *error = NULL;

        if (separator != NULL) {
            *separator = '\0';
            cursor = separator + 1;
        } else {
            cursor = segment + strlen(segment);
        }

        if (*segment == '\0')
            continue;

        if (background) {
            size_t len = strlen(segment);
            to_parse = msh_malloc(len + 3);
            memcpy(to_parse, segment, len);
            to_parse[len] = ' ';
            to_parse[len + 1] = '&';
            to_parse[len + 2] = '\0';
        } else {
            to_parse = segment;
        }

        if (parse_line(to_parse, sh, &list, &error) < 0) {
            msh_error("syntax error: %s",
                      error != NULL ? error : "bad input");
            free(error);
            sh->last_status = 2;
            overall = 2;
        } else if (list.npipelines == 0) {
            free_command_list(&list);
        } else {
            overall = execute_command_list(&list, sh);
            free_command_list(&list);
        }

        if (background)
            free(to_parse);
    }

    free(copy);
    return overall;
}

static void execute_text(const char *text, shell_state_t *sh)
{
    char *copy = msh_strdup(text);
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);

    while (line != NULL && !sh->should_exit) {
        if (line[0] != '\0')
            execute_line(line, sh);
        line = strtok_r(NULL, "\n", &save);
    }
    free(copy);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                          */
/* ------------------------------------------------------------------ */

static int run_repl(shell_state_t *sh)
{
    char *line = NULL;
    size_t cap = 0;

    while (!sh->should_exit && !g_terminate) {
        ssize_t n;

        job_reap(sh);

        if (sh->interactive) {
            fputs(MSH_PROMPT, stdout);
            fflush(stdout);
        }

        errno = 0;
        n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                if (g_terminate)
                    break;
                continue;
            }
            if (feof(stdin)) {
                if (sh->interactive)
                    putchar('\n');
                break;
            }
            msh_perror("getline");
            break;
        }

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;

        execute_line(line, sh);
    }

    free(line);
    if (g_terminate)
        return 128 + SIGTERM;
    return sh->should_exit ? sh->exit_status : sh->last_status;
}

static void print_usage(void)
{
    fprintf(stderr,
            "usage: %s [-c command] [--help] [--version]\n", MSH_NAME);
}

int main(int argc, char **argv)
{
    shell_state_t sh;
    int rc;

    install_signal_handlers();

    if (argc > 1 && (strcmp(argv[1], "--help") == 0 ||
                     strcmp(argv[1], "-h") == 0)) {
        printf("myshell %s - a small POSIX shell\n", MSH_VERSION);
        printf("usage: %s [-c command]\n", MSH_NAME);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("myshell %s\n", MSH_VERSION);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            print_usage();
            return 2;
        }
        init_shell(&sh, 0);
        execute_text(argv[2], &sh);
        rc = sh.should_exit ? sh.exit_status : sh.last_status;
        job_cleanup(&sh);
        restore_terminal();
        return rc;
    }

    if (argc > 1) {
        print_usage();
        return 2;
    }

    init_shell(&sh, 1);
    rc = run_repl(&sh);
    job_cleanup(&sh);
    restore_terminal();
    return rc;
}
