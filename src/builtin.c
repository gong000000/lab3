/*
 * builtin.c - shell built-in commands and dispatch
 */
#include "myshell.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int (*builtin_fn)(int argc, char **argv, shell_state_t *sh);

typedef struct {
    const char *name;
    builtin_fn fn;
    int runs_in_parent;
} builtin_entry_t;

/* ls and the file/text utilities are implemented in cmd_*.c. */
static int b_ls(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_ls(argc, argv);
}

static int b_mkdir(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_mkdir(argc, argv);
}

static int b_rmdir(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_rmdir(argc, argv);
}

static int b_ln(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_ln(argc, argv);
}

static int b_cp(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_cp(argc, argv);
}

static int b_rm(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_rm(argc, argv);
}

static int b_mv(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_mv(argc, argv);
}

static int b_cat(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_cat(argc, argv);
}

static int b_grep(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    return cmd_grep(argc, argv);
}

/* ------------------------------------------------------------------ */
/* test / [                                                           */
/* ------------------------------------------------------------------ */

static int test_unary(const char *op, const char *arg)
{
    struct stat st;

    if (strcmp(op, "-z") == 0)
        return arg[0] == '\0';
    if (strcmp(op, "-n") == 0)
        return arg[0] != '\0';
    if (strcmp(op, "-e") == 0)
        return stat(arg, &st) == 0;
    if (strcmp(op, "-f") == 0)
        return stat(arg, &st) == 0 && S_ISREG(st.st_mode);
    if (strcmp(op, "-d") == 0)
        return stat(arg, &st) == 0 && S_ISDIR(st.st_mode);
    if (strcmp(op, "-r") == 0)
        return access(arg, R_OK) == 0;
    if (strcmp(op, "-w") == 0)
        return access(arg, W_OK) == 0;
    if (strcmp(op, "-x") == 0)
        return access(arg, X_OK) == 0;
    if (strcmp(op, "-s") == 0)
        return stat(arg, &st) == 0 && st.st_size > 0;
    return 0;
}

static int test_binary(const char *left, const char *op, const char *right)
{
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
        return strcmp(left, right) == 0;
    if (strcmp(op, "!=") == 0)
        return strcmp(left, right) != 0;
    if (strcmp(op, "-eq") == 0)
        return strtoll(left, NULL, 10) == strtoll(right, NULL, 10);
    if (strcmp(op, "-ne") == 0)
        return strtoll(left, NULL, 10) != strtoll(right, NULL, 10);
    if (strcmp(op, "-lt") == 0)
        return strtoll(left, NULL, 10) < strtoll(right, NULL, 10);
    if (strcmp(op, "-le") == 0)
        return strtoll(left, NULL, 10) <= strtoll(right, NULL, 10);
    if (strcmp(op, "-gt") == 0)
        return strtoll(left, NULL, 10) > strtoll(right, NULL, 10);
    if (strcmp(op, "-ge") == 0)
        return strtoll(left, NULL, 10) >= strtoll(right, NULL, 10);
    return -1;
}

static int test_eval(char **av, int n)
{
    if (n <= 0)
        return 0;
    if (strcmp(av[0], "!") == 0)
        return !test_eval(av + 1, n - 1);
    if (n == 1)
        return av[0][0] != '\0';
    if (n == 2) {
        if (av[0][0] == '-' && av[0][1] != '\0' && av[0][2] == '\0')
            return test_unary(av[0], av[1]);
        return av[0][0] != '\0';
    }
    if (n == 3) {
        int rc = test_binary(av[0], av[1], av[2]);

        if (rc >= 0)
            return rc;
    }
    return 0;
}

static int b_test(int argc, char **argv, shell_state_t *sh)
{
    int bracket = strcmp(argv[0], "[") == 0;

    (void)sh;
    if (bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            msh_usage("[: missing ']'");
            return 2;
        }
        --argc;
    }
    return test_eval(argv + 1, argc - 1) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* read / shift / source                                              */
/* ------------------------------------------------------------------ */

static int b_read(int argc, char **argv, shell_state_t *sh)
{
    char line[8192];
    char *vars[MSH_MAX_ARGS];
    char *fields[MSH_MAX_ARGS];
    int nvars = 0;
    int nfields = 0;
    int raw = 0;
    int i;
    size_t len;
    char *p;

    (void)sh;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0)
            raw = 1;
        else if (nvars < MSH_MAX_ARGS - 1)
            vars[nvars++] = argv[i];
    }

    if (fgets(line, sizeof(line), stdin) == NULL)
        return 1;
    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    if (!raw) {
        char *src = line;
        char *dst = line;

        while (*src != '\0') {
            if (*src == '\\' && src[1] != '\0')
                ++src;
            *dst++ = *src++;
        }
        *dst = '\0';
    }

    p = line;
    while (*p != '\0' && nfields < MSH_MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            break;
        fields[nfields++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            ++p;
        if (*p != '\0')
            *p++ = '\0';
    }

    if (nvars == 0) {
        setenv("REPLY", line, 1);
        return 0;
    }
    for (i = 0; i < nvars; ++i) {
        if (i < nfields)
            setenv(vars[i], fields[i], 1);
        else
            setenv(vars[i], "", 1);
    }
    return 0;
}

static int b_shift(int argc, char **argv, shell_state_t *sh)
{
    int count = 1;

    if (argc > 2) {
        msh_usage("shift [n]");
        return 2;
    }
    if (argc == 2)
        count = atoi(argv[1]);
    if (count < 0) {
        msh_usage("shift [n]");
        return 2;
    }
    if (count > sh->positional_count)
        return 1;
    if (sh->positional != NULL)
        sh->positional += count;
    sh->positional_count -= count;
    return 0;
}

static int b_source(int argc, char **argv, shell_state_t *sh)
{
    char *text;
    char **saved_args;
    int saved_count;
    int rc;

    if (argc < 2) {
        msh_usage("source FILE [args...]");
        return 2;
    }
    text = msh_read_file(argv[1]);
    if (text == NULL) {
        msh_perror(argv[1]);
        return 1;
    }
    saved_args = sh->positional;
    saved_count = sh->positional_count;
    if (argc > 2) {
        sh->positional = argv + 2;
        sh->positional_count = argc - 2;
    }
    rc = script_execute(text, sh);
    sh->positional = saved_args;
    sh->positional_count = saved_count;
    free(text);
    return rc;
}

static const builtin_entry_t builtins[] = {
    { "cd",     builtin_cd,     1 },
    { "pwd",    builtin_pwd,    0 },
    { "echo",   builtin_echo,   0 },
    { "exit",   builtin_exit,   1 },
    { "jobs",   builtin_jobs,   1 },
    { "wait",   builtin_wait,   1 },
    { "export", builtin_export, 1 },
    { "unset",  builtin_unset,  1 },
    { "help",   builtin_help,   0 },
    { "true",   builtin_true,   0 },
    { "false",  builtin_false,  0 },
    { ":",      builtin_true,   0 },
    { "ls",     b_ls,           0 },
    { "mkdir",  b_mkdir,        0 },
    { "rmdir",  b_rmdir,        0 },
    { "ln",     b_ln,           0 },
    { "cp",     b_cp,           0 },
    { "rm",     b_rm,           0 },
    { "mv",     b_mv,           0 },
    { "cat",    b_cat,          0 },
    { "grep",   b_grep,         0 },
    { "netserv", cmd_netserv,   0 },
    { "netcli",  cmd_netcli,    0 },
    { "explain", cmd_explain,   0 },
    { "undo",    cmd_undo,      0 },
    { "serve",   cmd_serve,     0 },
    { "watch",   cmd_watch,     0 },
    { "prompt",  cmd_prompt,    1 },
    { "pipestat", cmd_pipestat,  0 },
    { "history", cmd_history,   0 },
    { "test",    b_test,        0 },
    { "[",       b_test,        0 },
    { "read",    b_read,        1 },
    { "shift",   b_shift,       1 },
    { "source",  b_source,      1 },
    { ".",       b_source,      1 },
    { "trace",   cmd_trace,     0 },
};

static const builtin_entry_t *find_builtin(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    }
    return NULL;
}

int is_builtin(const char *name)
{
    return name != NULL && find_builtin(name) != NULL;
}

int is_parent_builtin(const char *name)
{
    const builtin_entry_t *entry = find_builtin(name);

    return entry != NULL && entry->runs_in_parent;
}

int run_builtin(int argc, char **argv, shell_state_t *sh, int in_child)
{
    const builtin_entry_t *entry;

    (void)in_child;
    if (argc <= 0 || argv == NULL || argv[0] == NULL)
        return 1;
    entry = find_builtin(argv[0]);
    if (entry == NULL) {
        msh_error("%s: not a built-in command", argv[0]);
        return 127;
    }
    return entry->fn(argc, argv, sh);
}

/* ------------------------------------------------------------------ */
/* cd / pwd                                                           */
/* ------------------------------------------------------------------ */

int builtin_cd(int argc, char **argv, shell_state_t *sh)
{
    const char *target;
    char oldpwd[PATH_MAX];
    char newpwd[PATH_MAX];
    int print_path = 0;

    (void)sh;
    if (argc > 2) {
        msh_error("cd: too many arguments");
        return 1;
    }

    if (argc == 1) {
        target = getenv("HOME");
        if (target == NULL || target[0] == '\0') {
            msh_error("cd: HOME not set");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (target == NULL || target[0] == '\0') {
            msh_error("cd: OLDPWD not set");
            return 1;
        }
        print_path = 1;
    } else {
        target = argv[1];
    }

    if (getcwd(oldpwd, sizeof(oldpwd)) == NULL)
        oldpwd[0] = '\0';

    if (chdir(target) < 0) {
        msh_perror(target);
        return 1;
    }

    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
        setenv("PWD", newpwd, 1);
        if (oldpwd[0] != '\0')
            setenv("OLDPWD", oldpwd, 1);
        if (print_path)
            printf("%s\n", newpwd);
    }
    return 0;
}

int builtin_pwd(int argc, char **argv, shell_state_t *sh)
{
    char *cwd;

    (void)sh;
    if (argc > 1 && strcmp(argv[1], "-L") != 0 &&
        strcmp(argv[1], "-P") != 0) {
        msh_error("pwd: invalid option: %s", argv[1]);
        return 1;
    }

    cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        msh_perror("pwd");
        return 1;
    }
    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* echo                                                               */
/* ------------------------------------------------------------------ */

static void echo_escape(const char *s)
{
    while (*s != '\0') {
        if (*s != '\\') {
            putchar(*s++);
            continue;
        }
        ++s;
        switch (*s) {
        case 'a': putchar('\a'); ++s; break;
        case 'b': putchar('\b'); ++s; break;
        case 'e': putchar('\033'); ++s; break;
        case 'f': putchar('\f'); ++s; break;
        case 'n': putchar('\n'); ++s; break;
        case 'r': putchar('\r'); ++s; break;
        case 't': putchar('\t'); ++s; break;
        case 'v': putchar('\v'); ++s; break;
        case '\\': putchar('\\'); ++s; break;
        case 'c': return;
        case '0': {
            int value = 0;
            int count = 0;
            ++s;
            while (count < 3 && *s >= '0' && *s <= '7') {
                value = value * 8 + (*s - '0');
                ++s;
                ++count;
            }
            putchar(value & 0xff);
            break;
        }
        case '\0':
            putchar('\\');
            return;
        default:
            putchar('\\');
            putchar(*s++);
            break;
        }
    }
}

int builtin_echo(int argc, char **argv, shell_state_t *sh)
{
    int newline = 1;
    int escapes = 0;
    int i = 1;
    int printed = 0;

    (void)sh;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *opt = argv[i] + 1;

        if (strcmp(opt, "-") == 0)
            break;
        if (strspn(opt, "neE") != strlen(opt))
            break;
        while (*opt != '\0') {
            if (*opt == 'n')
                newline = 0;
            else if (*opt == 'e')
                escapes = 1;
            else if (*opt == 'E')
                escapes = 0;
            ++opt;
        }
        ++i;
    }

    for (; i < argc; ++i) {
        if (printed)
            putchar(' ');
        if (escapes)
            echo_escape(argv[i]);
        else
            fputs(argv[i], stdout);
        printed = 1;
    }
    if (newline)
        putchar('\n');
    fflush(stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/* exit                                                               */
/* ------------------------------------------------------------------ */

int builtin_exit(int argc, char **argv, shell_state_t *sh)
{
    long status = sh->last_status;
    char *end = NULL;

    if (argc > 2) {
        msh_error("exit: too many arguments");
        return 1;
    }
    if (argc == 2) {
        errno = 0;
        status = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0') {
            msh_error("exit: numeric argument required");
            status = 2;
        }
    }
    status &= 0xff;
    sh->should_exit = 1;
    sh->exit_status = (int)status;
    return (int)status;
}

/* ------------------------------------------------------------------ */
/* jobs / wait                                                        */
/* ------------------------------------------------------------------ */

int builtin_jobs(int argc, char **argv, shell_state_t *sh)
{
    int show_pid = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-l") == 0)
            show_pid = 1;
        else {
            msh_error("jobs: invalid option: %s", argv[i]);
            return 1;
        }
    }

    job_reap(sh);

    for (i = 0; i < MSH_MAX_JOBS; ++i) {
        job_t *job = &sh->jobs[i];

        if (!job->used)
            continue;
        if (show_pid)
            printf("[%d]  %d  %-8s %s\n", job->id, (int)job->pgid,
                   job->running ? "Running" : "Done", job->command);
        else
            printf("[%d]  %-8s %s\n", job->id,
                   job->running ? "Running" : "Done", job->command);
    }
    return 0;
}

static int wait_status_to_exit(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

int builtin_wait(int argc, char **argv, shell_state_t *sh)
{
    int status = 0;
    int last = 0;
    int i;

    if (argc == 1) {
        pid_t pid;

        for (;;) {
            pid = waitpid(-1, &status, 0);
            if (pid > 0) {
                last = wait_status_to_exit(status);
                continue;
            }
            if (pid < 0 && errno == EINTR)
                continue;
            break;
        }
        job_mark_all_done(sh, last);
        sh->last_status = last;
        return last;
    }

    for (i = 1; i < argc; ++i) {
        char *end = NULL;
        long value = strtol(argv[i], &end, 10);
        pid_t pid;

        if (end == argv[i] || *end != '\0' || value <= 0) {
            msh_error("wait: invalid pid: %s", argv[i]);
            return 1;
        }
        do {
            pid = waitpid((pid_t)value, &status, 0);
        } while (pid < 0 && errno == EINTR);
        if (pid < 0) {
            msh_perror("wait");
            return 1;
        }
        last = wait_status_to_exit(status);
    }
    job_mark_all_done(sh, last);
    sh->last_status = last;
    return last;
}

/* ------------------------------------------------------------------ */
/* export / unset                                                     */
/* ------------------------------------------------------------------ */

int builtin_export(int argc, char **argv, shell_state_t *sh)
{
    int i;

    (void)sh;
    if (argc == 1) {
        extern char **environ;
        char **p;

        for (p = environ; *p != NULL; ++p)
            printf("%s\n", *p);
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        char *eq = strchr(argv[i], '=');

        if (eq != NULL) {
            char *name = msh_strndup(argv[i], (size_t)(eq - argv[i]));
            if (name[0] == '\0' ||
                !(isalpha((unsigned char)name[0]) || name[0] == '_')) {
                msh_error("export: invalid name: %s", name);
                free(name);
                return 1;
            }
            if (setenv(name, eq + 1, 1) < 0) {
                msh_perror("export");
                free(name);
                return 1;
            }
            free(name);
        } else if (getenv(argv[i]) == NULL) {
            if (setenv(argv[i], "", 1) < 0) {
                msh_perror("export");
                return 1;
            }
        }
    }
    return 0;
}

int builtin_unset(int argc, char **argv, shell_state_t *sh)
{
    int i;

    (void)sh;
    for (i = 1; i < argc; ++i) {
        if (unsetenv(argv[i]) < 0) {
            msh_perror("unset");
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* help / trivial built-ins                                           */
/* ------------------------------------------------------------------ */

int builtin_help(int argc, char **argv, shell_state_t *sh)
{
    (void)argc;
    (void)argv;
    (void)sh;

    printf("myshell %s - a small POSIX shell\n\n", MSH_VERSION);
    printf("Syntax:\n");
    printf("  command [args] [< file] [> file] [>> file] [2> file] [&> file]\n");
    printf("  command1 | command2 | command3\n");
    printf("  command1 ; command2\n");
    printf("  command &\n\n");
    printf("Built-in commands:\n");
    printf("  cd pwd echo exit jobs wait export unset help true false\n");
    printf("  ls mkdir rmdir ln cp rm mv cat grep\n");
    printf("  netserv [-p PORT] [-a ADDR] [-s TOKEN] [-t SEC] [-1]\n");
    printf("  netcli [-p PORT] [-s TOKEN] [-t SEC] [-i] HOST [COMMAND...]\n");
    printf("  explain 'command line'\n");
    printf("  undo\n");
    printf("  serve [-p PORT] [-d DIR] [-a ADDR] [-1]\n");
    printf("  watch [-n SEC] [-c COUNT] COMMAND...\n");
    printf("  prompt [on|off]\n");
    printf("  pipestat 'command1 | command2'\n");
    printf("  history [-c]\n");
    printf("  test EXPR    (or [ EXPR ])\n");
    printf("  read [-r] VAR...  shift [N]  source FILE [ARGS...]\n");
    printf("  trace [-c] [-f] [-e NAME[,NAME...]] COMMAND...\n\n");
    printf("Scripting:\n");
    printf("  $(command)  command substitution\n");
    printf("  if CMD; then ...; elif CMD; then ...; else ...; fi\n");
    printf("  for NAME in WORDS; do ...; done\n");
    printf("  while CMD; do ...; done      until CMD; do ...; done\n");
    printf("  NAME() { ...; }              break / continue / return\n");
    printf("  myshell SCRIPT [ARGS...]\n\n");
    printf("Examples:\n");
    printf("  netserv -1 -p 9090 &\n");
    printf("  netcli -p 9090 127.0.0.1 echo hello\n");
    printf("  netcli -i -p 9090 127.0.0.1\n\n");
    printf("Interactive keys:\n");
    printf("  Ctrl-C  SIGINT  - interrupt the foreground job\n");
    printf("  Ctrl-Z  SIGQUIT - quit the foreground job (mapped by myshell)\n");
    printf("  Ctrl-D          - end of input / exit\n");
    printf("  Left/Right/Home/End  - move the cursor\n");
    printf("  Tab                  - complete commands and file names\n");
    printf("  Ctrl-R               - reverse history search\n");
    printf("  Ctrl-U/K/W           - delete before/after/previous word\n");
    printf("  Up/Down              - browse history\n");
    return 0;
}

int builtin_true(int argc, char **argv, shell_state_t *sh)
{
    (void)argc;
    (void)argv;
    (void)sh;
    return 0;
}

int builtin_false(int argc, char **argv, shell_state_t *sh)
{
    (void)argc;
    (void)argv;
    (void)sh;
    return 1;
}

int cmd_prompt(int argc, char **argv, shell_state_t *sh)
{
    if (argc > 2) {
        msh_usage("prompt [on|off]");
        return 2;
    }
    if (argc == 2) {
        if (strcmp(argv[1], "on") == 0)
            sh->show_prompt_info = 1;
        else if (strcmp(argv[1], "off") == 0)
            sh->show_prompt_info = 0;
        else {
            msh_usage("prompt [on|off]");
            return 2;
        }
    }
    printf("prompt: %s\n", sh->show_prompt_info ? "on" : "off");
    return 0;
}

const char *const *msh_builtin_names(int *count)
{
    static const char *names[] = {
        "cd", "pwd", "echo", "exit", "jobs", "wait", "export", "unset",
        "help", "true", "false", ":", "ls", "mkdir", "rmdir", "ln",
        "cp", "rm", "mv", "cat", "grep", "netserv", "netcli", "explain",
        "undo", "serve", "watch", "prompt", "pipestat", "history",
        "test", "[", "read", "shift", "source", ".", "trace"
    };
    *count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}

int cmd_history(int argc, char **argv, shell_state_t *sh)
{
    if (argc == 2 && strcmp(argv[1], "-c") == 0) {
        history_clear(sh);
        return 0;
    }
    if (argc != 1) {
        msh_usage("history [-c]");
        return 2;
    }
    history_print(sh);
    return 0;
}
