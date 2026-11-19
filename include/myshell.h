/*
 * myshell.h - shared declarations for myshell
 *
 * myshell is a small POSIX shell written for the open-source project
 * (Lab 3).  The original skeleton is the "simplesh.c" example from the
 * textbook (page 357), extended with job control, redirection, pipes,
 * and a set of built-in file/text utilities.
 */
#ifndef MYSHELL_H
#define MYSHELL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>

#define MSH_NAME          "myshell"
#define MSH_VERSION       "1.2.0"
#define MSH_PROMPT        "shell> "
#define MSH_MAX_JOBS      64
#define MSH_MAX_ARGS      256
#define MSH_IO_BUFSIZE    65536

/* ------------------------------------------------------------------ */
/* Parsed command representation                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    TOK_WORD = 0,
    TOK_PIPE,
    TOK_AND_IF,         /* && */
    TOK_OR_IF,          /* || */
    TOK_SEMI,
    TOK_AMP,
    TOK_IN,             /* <  */
    TOK_OUT,            /* >  */
    TOK_OUT_APPEND,     /* >> */
    TOK_ERR,            /* 2> */
    TOK_ERR_APPEND,     /* 2>> */
    TOK_ERR_DUP_OUT,    /* 2>&1 */
    TOK_BOTH,           /* &> */
    TOK_BOTH_APPEND,    /* &>> */
    TOK_END
} token_type_t;

typedef struct {
    token_type_t type;
    char *text;         /* only for TOK_WORD */
    int glob;           /* expand unquoted *, ?, [...] */
} token_t;

typedef struct {
    char *in_path;      /* < file */
    char *out_path;     /* > file, >> file, &> file */
    int out_append;     /* nonzero for >> */
    char *err_path;     /* 2> file, 2>> file */
    int err_append;     /* nonzero for 2>> */
    int err_to_out;     /* 2>&1 : dup stdout onto stderr */
    int merge_err;      /* &> : dup stdout onto stderr */
} redirect_t;

typedef struct {
    char **argv;
    int argc;
    redirect_t redir;
} command_t;

typedef struct {
    command_t *commands;
    int ncommands;
    int background;
    char *text;         /* printable form, used by jobs/wait */
} pipeline_t;

typedef enum {
    CONN_ALWAYS = 0,    /* ; or first command */
    CONN_AND,           /* && */
    CONN_OR             /* || */
} connector_t;

typedef struct {
    pipeline_t *pipelines;
    connector_t *connectors;  /* one per pipeline; first is ALWAYS */
    int npipelines;
} command_list_t;

/* ------------------------------------------------------------------ */
/* Background jobs                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int used;
    int id;
    pid_t pgid;
    pid_t *pids;
    int npids;
    int *done;          /* done[i] != 0 when pids[i] was reaped */
    int ndone;
    int running;
    int stopped;
    int status;         /* exit status of the last reaped process */
    char *command;
} job_t;

typedef struct {
    char *name;
    char *body;
} msh_function_t;

typedef struct shell_state {
    int interactive;
    int should_exit;
    int exit_status;
    int last_status;    /* $? */
    double last_duration;  /* seconds */
    int show_prompt_info;

    char **history;
    int history_count;
    int history_cap;

    /* Positional parameters ($1, $2, ...), set by scripts and functions. */
    char **positional;
    int positional_count;

    msh_function_t *functions;
    int function_count;
    int function_cap;

    pid_t shell_pid;
    pid_t shell_pgid;
    pid_t last_bg_pid;  /* $! */
    int terminal_fd;

    int next_job_id;
    job_t jobs[MSH_MAX_JOBS];

    struct termios saved_termios;
    int termios_saved;
    int termios_modified;
} shell_state_t;

/* ------------------------------------------------------------------ */
/* parser.c                                                           */
/* ------------------------------------------------------------------ */

int parse_line(const char *line, shell_state_t *sh,
               command_list_t *list, char **error);
void free_command_list(command_list_t *list);
void free_pipeline(pipeline_t *pl);
void free_command(command_t *cmd);
char *msh_expand_word(const char *raw, shell_state_t *sh, char **error);
const char *msh_find_substitution_end(const char *p);

/* ------------------------------------------------------------------ */
/* execute.c                                                          */
/* ------------------------------------------------------------------ */

int execute_command_list(command_list_t *list, shell_state_t *sh);
int execute_pipeline(pipeline_t *pl, shell_state_t *sh);
int apply_redirections(const redirect_t *redir);
int has_redirections(const redirect_t *redir);
void reset_child_signals(void);
int msh_execute_simple(const char *text, shell_state_t *sh);
int msh_capture_command(const char *command, shell_state_t *sh, char **output);

/* ------------------------------------------------------------------ */
/* script.c : control flow, functions, and script files               */
/* ------------------------------------------------------------------ */

int script_execute(const char *source, shell_state_t *sh);
int script_source_is_complete(const char *source);
int script_source_needs_parser(const char *source);
int msh_is_function(shell_state_t *sh, const char *name);
int msh_call_function(shell_state_t *sh, const char *name, int argc,
                      char **argv);
void msh_functions_free(shell_state_t *sh);

/* Execute one command line with the shell's parser/executor.  Used by the
 * network extension (netserv) to run a command received over TCP. */
int shell_execute_line(const char *line, shell_state_t *sh);
int cmd_explain(int argc, char **argv, shell_state_t *sh);
int cmd_undo(int argc, char **argv, shell_state_t *sh);
int cmd_serve(int argc, char **argv, shell_state_t *sh);
int cmd_watch(int argc, char **argv, shell_state_t *sh);
int cmd_prompt(int argc, char **argv, shell_state_t *sh);
int cmd_pipestat(int argc, char **argv, shell_state_t *sh);
int cmd_history(int argc, char **argv, shell_state_t *sh);
int cmd_trace(int argc, char **argv, shell_state_t *sh);
int cmd_dbg(int argc, char **argv, shell_state_t *sh);
const char *const *msh_builtin_names(int *count);

/* lineedit.c */
char *lineedit_read(const char *prompt, shell_state_t *sh);
void history_init(shell_state_t *sh);
void history_add(shell_state_t *sh, const char *line);
void history_save(shell_state_t *sh, const char *line);
void history_print(shell_state_t *sh);
char *history_expand(const char *line, shell_state_t *sh);
void history_clear(shell_state_t *sh);
void history_free(shell_state_t *sh);
int trash_move(const char *path);
int trash_move_ex(const char *path, const char *undo_extra);

/* ------------------------------------------------------------------ */
/* jobs.c                                                             */
/* ------------------------------------------------------------------ */

int job_add(shell_state_t *sh, pid_t pgid, const pid_t *pids, int npids,
            const char *command);
void job_reap(shell_state_t *sh);
void job_print_all(shell_state_t *sh);
void job_cleanup(shell_state_t *sh);
void job_mark_all_done(shell_state_t *sh, int status);

/* ------------------------------------------------------------------ */
/* builtin.c                                                          */
/* ------------------------------------------------------------------ */

int is_builtin(const char *name);
int is_parent_builtin(const char *name);
int run_builtin(int argc, char **argv, shell_state_t *sh, int in_child);

int builtin_cd(int argc, char **argv, shell_state_t *sh);
int builtin_pwd(int argc, char **argv, shell_state_t *sh);
int builtin_echo(int argc, char **argv, shell_state_t *sh);
int builtin_exit(int argc, char **argv, shell_state_t *sh);
int builtin_jobs(int argc, char **argv, shell_state_t *sh);
int builtin_wait(int argc, char **argv, shell_state_t *sh);
int builtin_export(int argc, char **argv, shell_state_t *sh);
int builtin_unset(int argc, char **argv, shell_state_t *sh);
int builtin_help(int argc, char **argv, shell_state_t *sh);
int builtin_true(int argc, char **argv, shell_state_t *sh);
int builtin_false(int argc, char **argv, shell_state_t *sh);

/* ------------------------------------------------------------------ */
/* cmd_ls.c                                                           */
/* ------------------------------------------------------------------ */

int cmd_ls(int argc, char **argv);

/* ------------------------------------------------------------------ */
/* cmd_fs.c : mkdir, rmdir, ln, cp, rm, mv                            */
/* ------------------------------------------------------------------ */

int cmd_mkdir(int argc, char **argv);
int cmd_rmdir(int argc, char **argv);
int cmd_ln(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_mv(int argc, char **argv);

/* ------------------------------------------------------------------ */
/* cmd_text.c : cat, grep                                             */
/* ------------------------------------------------------------------ */

int cmd_cat(int argc, char **argv);
int cmd_grep(int argc, char **argv);

/* ------------------------------------------------------------------ */
/* cmd_net.c : TCP remote shell server/client                         */
/* ------------------------------------------------------------------ */

int cmd_netserv(int argc, char **argv, shell_state_t *sh);
int cmd_netcli(int argc, char **argv, shell_state_t *sh);

/* ------------------------------------------------------------------ */
/* util.c                                                             */
/* ------------------------------------------------------------------ */

void *msh_malloc(size_t size);
void *msh_calloc(size_t nmemb, size_t size);
void *msh_realloc(void *ptr, size_t size);
char *msh_strdup(const char *s);
char *msh_strndup(const char *s, size_t n);

void msh_error(const char *fmt, ...);
void msh_perror(const char *context);
void msh_usage(const char *usage);
int msh_confirm(const char *fmt, ...);

char *msh_join_path(const char *dir, const char *name);
char *msh_basename_dup(const char *path);
char *msh_dirname_dup(const char *path);
int msh_path_exists(const char *path);
int msh_is_directory(const char *path);

int msh_write_all(int fd, const void *buf, size_t len);
int msh_copy_file_contents(const char *src, const char *dst, mode_t mode,
                           int force);
int msh_copy_tree(const char *src, const char *dst, int preserve);
int msh_remove_tree(const char *path);
int msh_mkdir_p(const char *path, mode_t mode);
int msh_parse_octal(const char *text, mode_t *mode);
void msh_format_mode(mode_t mode, char out[11]);
long long msh_file_size(const struct stat *st);
void msh_format_size(long long size, char *out, size_t out_size);
char *msh_read_file(const char *path);

#endif /* MYSHELL_H */
