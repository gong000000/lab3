/*
 * main.c - REPL, signal handling, and terminal/job-control setup
 *
 * The interactive loop follows the simple shell on textbook page 357:
 * read a line, parse it, fork, execute, and wait.  The extensions add
 * built-ins, redirection, pipes, background jobs, and signal handling.
 */
#include "myshell.h"

#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    if (sh->interactive)
        history_init(sh);
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
            if (c == '\\' && p[1] != '\0') {
                ++p;
                continue;
            }
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

int shell_execute_line(const char *line, shell_state_t *sh)
{
    char *copy;
    char *cursor;
    int overall = 0;

    if (script_source_needs_parser(line))
        return script_execute(line, sh);

    copy = msh_strdup(line);
    cursor = copy;

    while (*cursor != '\0' && !sh->should_exit) {
        int background;
        char *segment = cursor;
        char *separator = find_separator(cursor, &background);
        char *to_parse;

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

        overall = msh_execute_simple(to_parse, sh);

        if (background)
            free(to_parse);
    }

    free(copy);
    return overall;
}

static void execute_text(const char *text, shell_state_t *sh)
{
    char *copy;
    char *save = NULL;
    char *line;

    if (script_source_needs_parser(text)) {
        (void)script_execute(text, sh);
        return;
    }

    copy = msh_strdup(text);
    line = strtok_r(copy, "\n", &save);
    while (line != NULL && !sh->should_exit) {
        if (line[0] != '\0')
            shell_execute_line(line, sh);
        line = strtok_r(NULL, "\n", &save);
    }
    free(copy);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                          */
/* ------------------------------------------------------------------ */

static void build_prompt(shell_state_t *sh, char *buffer, size_t size)
{
    if (!sh->show_prompt_info) {
        snprintf(buffer, size, "%s", MSH_PROMPT);
        return;
    }

    {
        int running = 0;
        int i;

        for (i = 0; i < MSH_MAX_JOBS; ++i) {
            if (sh->jobs[i].used && sh->jobs[i].running)
                ++running;
        }
        snprintf(buffer, size, "shell [%d|%.2fs|%d]> ", sh->last_status,
                 sh->last_duration, running);
    }
}

static int read_line_plain(char **linep, size_t *capp)
{
    ssize_t n;

    errno = 0;
    n = getline(linep, capp, stdin);
    if (n < 0)
        return 0;
    while (n > 0 && ((*linep)[n - 1] == '\n' || (*linep)[n - 1] == '\r'))
        (*linep)[--n] = '\0';
    return 1;
}

static int run_repl(shell_state_t *sh)
{
    char *line = NULL;
    size_t cap = 0;

    while (!sh->should_exit && !g_terminate) {
        char prompt[256];
        char *command;
        int owned = 0;

        job_reap(sh);
        build_prompt(sh, prompt, sizeof(prompt));

        if (sh->interactive) {
            char *expanded;

            line = lineedit_read(prompt, sh);
            if (line == NULL)
                break;
            if (line[0] == '\0') {
                free(line);
                line = NULL;
                continue;
            }
            expanded = history_expand(line, sh);
            if (expanded == NULL) {
                free(line);
                line = NULL;
                continue;
            }
            history_add(sh, expanded);
            history_save(sh, expanded);
            free(line);
            line = expanded;
            command = line;
        } else {
            if (!read_line_plain(&line, &cap)) {
                if (errno == EINTR && !g_terminate) {
                    clearerr(stdin);
                    continue;
                }
                break;
            }
            if (line[0] == '\0')
                continue;
            command = line;
        }

        /* Keep reading while a block (if/for/while/function) or a
         * quoted string is still open. */
        while (command != NULL && !script_source_is_complete(command)) {
            char *more = NULL;
            char *joined;

            if (sh->interactive) {
                more = lineedit_read("... ", sh);
                if (more == NULL || more[0] == '\0') {
                    free(more);
                    if (owned)
                        free(command);
                    command = NULL;
                    break;
                }
            } else {
                size_t morecap = 0;

                if (!read_line_plain(&more, &morecap)) {
                    free(more);
                    break;
                }
            }

            joined = msh_malloc(strlen(command) + strlen(more) + 2);
            sprintf(joined, "%s\n%s", command, more);
            if (owned)
                free(command);
            free(more);
            command = joined;
            owned = 1;
        }

        if (command == NULL) {
            if (sh->interactive) {
                free(line);
                line = NULL;
            }
            continue;
        }

        {
            struct timespec start, end;

            clock_gettime(CLOCK_MONOTONIC, &start);
            shell_execute_line(command, sh);
            clock_gettime(CLOCK_MONOTONIC, &end);
            sh->last_duration = (double)(end.tv_sec - start.tv_sec) +
                                (double)(end.tv_nsec - start.tv_nsec) / 1e9;
        }
        if (owned)
            free(command);
        if (sh->interactive) {
            free(line);
            line = NULL;
        }
    }

    free(line);
    if (g_terminate)
        return 128 + SIGTERM;
    return sh->should_exit ? sh->exit_status : sh->last_status;
}

static void print_usage(void)
{
    fprintf(stderr,
            "usage: %s [-c command] [script [args...]]\n", MSH_NAME);
}

int main(int argc, char **argv)
{
    shell_state_t sh;
    int rc;

    setlocale(LC_CTYPE, "");
    install_signal_handlers();

    if (argc > 1 && (strcmp(argv[1], "--help") == 0 ||
                     strcmp(argv[1], "-h") == 0)) {
        printf("myshell %s - a small POSIX shell\n", MSH_VERSION);
        printf("usage: %s [-c command] [script [args...]]\n", MSH_NAME);
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
        sh.positional = argv + 3;
        sh.positional_count = argc > 3 ? argc - 3 : 0;
        execute_text(argv[2], &sh);
        rc = sh.should_exit ? sh.exit_status : sh.last_status;
        job_cleanup(&sh);
        msh_functions_free(&sh);
        history_free(&sh);
        restore_terminal();
        return rc;
    }

    if (argc > 1 && argv[1][0] != '-') {
        char *script = msh_read_file(argv[1]);

        if (script == NULL) {
            msh_perror(argv[1]);
            return 127;
        }
        init_shell(&sh, 0);
        sh.positional = argv + 2;
        sh.positional_count = argc - 2;
        (void)script_execute(script, &sh);
        free(script);
        rc = sh.should_exit ? sh.exit_status : sh.last_status;
        job_cleanup(&sh);
        msh_functions_free(&sh);
        history_free(&sh);
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
    msh_functions_free(&sh);
    history_free(&sh);
    restore_terminal();
    return rc;
}
