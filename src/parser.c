/*
 * parser.c - tokenizer, expansion, and command-line parser
 *
 * Supported syntax:
 *   command [args] [< file] [> file] [>> file] [2> file] [&> file]
 *   cmd1 | cmd2 | cmd3
 *   cmd1 ; cmd2
 *   cmd &
 *
 * Quotes ('...' and "..."), backslash escaping, $VAR/${VAR}, $?, $$, $!,
 * and leading ~ are expanded.  Glob expansion is intentionally omitted.
 */
#include "myshell.h"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Growable string                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} wordbuf_t;

static void wb_reserve(wordbuf_t *wb, size_t extra)
{
    size_t need = wb->len + extra + 1;

    if (need <= wb->cap)
        return;
    if (wb->cap == 0)
        wb->cap = 32;
    while (wb->cap < need)
        wb->cap *= 2;
    wb->buf = msh_realloc(wb->buf, wb->cap);
}

static void wb_putc(wordbuf_t *wb, char c)
{
    wb_reserve(wb, 1);
    wb->buf[wb->len++] = c;
    wb->buf[wb->len] = '\0';
}

static void wb_puts(wordbuf_t *wb, const char *s)
{
    size_t n = strlen(s);

    wb_reserve(wb, n);
    memcpy(wb->buf + wb->len, s, n);
    wb->len += n;
    wb->buf[wb->len] = '\0';
}

static void wb_putint(wordbuf_t *wb, long value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%ld", value);
    wb_puts(wb, buf);
}

/* ------------------------------------------------------------------ */
/* Token list                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    token_t *items;
    int count;
    int cap;
} token_list_t;

static void token_add(token_list_t *tl, token_type_t type, char *text, int glob)
{
    if (tl->count == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 16;
        tl->items = msh_realloc(tl->items,
                                (size_t)tl->cap * sizeof(*tl->items));
    }
    tl->items[tl->count].type = type;
    tl->items[tl->count].text = text;
    tl->items[tl->count].glob = glob;
    tl->count++;
}

static void token_list_free(token_list_t *tl)
{
    int i;

    for (i = 0; i < tl->count; ++i)
        free(tl->items[i].text);
    free(tl->items);
    tl->items = NULL;
    tl->count = 0;
    tl->cap = 0;
}

/* ------------------------------------------------------------------ */
/* Operator recognition                                               */
/* ------------------------------------------------------------------ */

static int match_operator(const char *p, token_type_t *type, int *length)
{
    struct opdef {
        const char *text;
        token_type_t type;
    };
    static const struct opdef ops[] = {
        { "&>>", TOK_BOTH_APPEND },
        { "&>",  TOK_BOTH },
        { "2>&1", TOK_ERR_DUP_OUT },
        { "2>>", TOK_ERR_APPEND },
        { "2>",  TOK_ERR },
        { ">>",  TOK_OUT_APPEND },
        { ">",   TOK_OUT },
        { "<",   TOK_IN },
        { "&&",  TOK_AND_IF },
        { "||",  TOK_OR_IF },
        { "|",   TOK_PIPE },
        { ";",   TOK_SEMI },
        { "&",   TOK_AMP },
    };
    size_t i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        size_t n = strlen(ops[i].text);
        if (strncmp(p, ops[i].text, n) == 0) {
            *type = ops[i].type;
            *length = (int)n;
            return 1;
        }
    }
    return 0;
}

static int is_token_delim(char c)
{
    return c == '\0' || isspace((unsigned char)c) ||
           c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

/* ------------------------------------------------------------------ */
/* Expansion                                                          */
/* ------------------------------------------------------------------ */

static void wb_putvalue(wordbuf_t *wb, shell_state_t *sh, int index)
{
    if (index == 0)
        wb_puts(wb, MSH_NAME);
    else if (index <= sh->positional_count && sh->positional != NULL)
        wb_puts(wb, sh->positional[index - 1]);
}

static void wb_putall(wordbuf_t *wb, shell_state_t *sh)
{
    int i;

    for (i = 0; i < sh->positional_count; ++i) {
        if (i > 0)
            wb_putc(wb, ' ');
        wb_puts(wb, sh->positional[i]);
    }
}

/* Find the ')' that closes a $(...) substitution.  Nested parentheses and
 * quoted text are skipped.  Returns NULL when the substitution is not
 * closed. */
const char *msh_find_substitution_end(const char *p)
{
    int depth = 1;
    int in_single = 0;
    int in_double = 0;

    while (*p != '\0') {
        char c = *p;

        if (in_single) {
            if (c == '\'')
                in_single = 0;
            ++p;
            continue;
        }
        if (c == '\\') {
            if (p[1] != '\0')
                ++p;
            ++p;
            continue;
        }
        if (in_double) {
            if (c == '"')
                in_double = 0;
            else if (c == '\\' && p[1] != '\0')
                ++p;
            ++p;
            continue;
        }
        if (c == '\'') {
            in_single = 1;
            ++p;
            continue;
        }
        if (c == '"') {
            in_double = 1;
            ++p;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0)
                return p;
        }
        ++p;
    }
    return NULL;
}

static int expand_substitution(const char **pp, wordbuf_t *wb,
                               shell_state_t *sh, char **error)
{
    const char *start = *pp + 2;       /* skip "$(" */
    const char *end = msh_find_substitution_end(start);
    char *inner;
    char *output = NULL;
    char *p;
    int rc;

    if (end == NULL) {
        free(wb->buf);
        wb->buf = NULL;
        wb->len = wb->cap = 0;
        *error = msh_strdup("bad substitution: missing ')'");
        return -1;
    }

    inner = msh_strndup(start, (size_t)(end - start));
    rc = msh_capture_command(inner, sh, &output);
    free(inner);
    if (rc != 0) {
        free(output);
        free(wb->buf);
        wb->buf = NULL;
        wb->len = wb->cap = 0;
        *error = msh_strdup("command substitution failed");
        return -1;
    }

    /* Command substitution behaves like an unquoted expansion: trailing
     * newlines disappear and other newlines become field separators. */
    for (p = output; p != NULL && *p != '\0'; ++p) {
        if (*p == '\n' || *p == '\r')
            wb_putc(wb, ' ');
        else
            wb_putc(wb, *p);
    }
    while (wb->len > 0 && wb->buf[wb->len - 1] == ' ')
        --wb->len;
    if (wb->buf != NULL)
        wb->buf[wb->len] = '\0';
    free(output);
    *pp = end + 1;
    return 0;
}

static int expand_dollar(const char **pp, wordbuf_t *wb, shell_state_t *sh,
                         char **error)
{
    const char *p = *pp + 1;

    if (*p == '(')
        return expand_substitution(pp, wb, sh, error);

    if (*p == '?') {
        wb_putint(wb, sh->last_status);
        *pp = p + 1;
        return 0;
    }
    if (*p == '$') {
        wb_putint(wb, (long)sh->shell_pid);
        *pp = p + 1;
        return 0;
    }
    if (*p == '!') {
        wb_putint(wb, (long)sh->last_bg_pid);
        *pp = p + 1;
        return 0;
    }
    if (*p == '#') {
        wb_putint(wb, sh->positional_count);
        *pp = p + 1;
        return 0;
    }
    if (*p == '@' || *p == '*') {
        wb_putall(wb, sh);
        *pp = p + 1;
        return 0;
    }
    if (*p >= '0' && *p <= '9') {
        wb_putvalue(wb, sh, *p - '0');
        *pp = p + 1;
        return 0;
    }

    if (*p == '{') {
        const char *start = p + 1;
        const char *end = strchr(start, '}');
        char *name;
        const char *value;

        if (end == NULL) {
            free(wb->buf);
            wb->buf = NULL;
            wb->len = wb->cap = 0;
            *error = msh_strdup("bad substitution: missing '}'");
            return -1;
        }
        name = msh_strndup(start, (size_t)(end - start));
        if (strcmp(name, "?") == 0) {
            wb_putint(wb, sh->last_status);
        } else if (strcmp(name, "$") == 0) {
            wb_putint(wb, (long)sh->shell_pid);
        } else if (strcmp(name, "!") == 0) {
            wb_putint(wb, (long)sh->last_bg_pid);
        } else if (strcmp(name, "#") == 0) {
            wb_putint(wb, sh->positional_count);
        } else if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
            wb_putall(wb, sh);
        } else if (name[0] >= '0' && name[0] <= '9' &&
                   name[strspn(name, "0123456789")] == '\0') {
            wb_putvalue(wb, sh, atoi(name));
        } else if (strcmp(name, "0") == 0) {
            wb_puts(wb, MSH_NAME);
        } else {
            value = getenv(name);
            if (value != NULL)
                wb_puts(wb, value);
        }
        free(name);
        *pp = end + 1;
        return 0;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
        const char *start = p;
        char *name;
        const char *value;

        while (isalnum((unsigned char)*p) || *p == '_')
            ++p;
        name = msh_strndup(start, (size_t)(p - start));
        value = getenv(name);
        if (value != NULL)
            wb_puts(wb, value);
        free(name);
        *pp = p;
        return 0;
    }

    wb_putc(wb, '$');
    *pp = p;
    return 0;
}

static int expand_tilde(const char **pp, wordbuf_t *wb)
{
    const char *p = *pp + 1;
    const char *start = p;
    char *name;
    const char *home = NULL;

    while (!is_token_delim(*p) && *p != '/' && *p != '"' && *p != '\'')
        ++p;
    name = msh_strndup(start, (size_t)(p - start));

    if (name[0] == '\0') {
        home = getenv("HOME");
    } else {
        struct passwd *pw = getpwnam(name);
        if (pw != NULL)
            home = pw->pw_dir;
    }

    if (home != NULL) {
        wb_puts(wb, home);
    } else {
        wb_putc(wb, '~');
        wb_puts(wb, name);
    }
    free(name);
    *pp = p;
    return 0;
}

/* Read one unquoted/quoted word and append it to tl.  Returns 1 if a
 * word was produced, 0 at end of input, -1 on error. */
static int read_word(const char **pp, token_list_t *tl, shell_state_t *sh,
                     char **error)
{
    const char *p = *pp;
    wordbuf_t wb = { 0 };
    int started = 0;
    int closed;
    int unquoted_wild = 0;
    int quoted_wild = 0;

    while (*p != '\0') {
        char c = *p;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            break;
        if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>')
            break;
        if (c == '#' && !started)
            break;

        if (c == '~' && !started) {
            if (expand_tilde(&p, &wb) < 0)
                goto fail;
            started = 1;
            continue;
        }

        if (c == '\'') {
            ++p;
            closed = 0;
            while (*p != '\0') {
                if (*p == '\'') {
                    ++p;
                    closed = 1;
                    break;
                }
                if (*p == '*' || *p == '?' || *p == '[')
                    quoted_wild = 1;
                wb_putc(&wb, *p++);
            }
            if (!closed) {
                *error = msh_strdup("unterminated single quote");
                goto fail;
            }
            started = 1;
            continue;
        }

        if (c == '"') {
            ++p;
            closed = 0;
            while (*p != '\0') {
                if (*p == '"') {
                    ++p;
                    closed = 1;
                    break;
                }
                if (*p == '\\') {
                    char next = p[1];
                    if (next == '$' || next == '"' || next == '\\') {
                        if (next == '*' || next == '?' || next == '[')
                            quoted_wild = 1;
                        wb_putc(&wb, next);
                        p += 2;
                    } else if (next == '\n') {
                        p += 2;
                    } else {
                        wb_putc(&wb, '\\');
                        ++p;
                    }
                    continue;
                }
                if (*p == '$') {
                    if (expand_dollar(&p, &wb, sh, error) < 0)
                        goto fail;
                    continue;
                }
                if (*p == '*' || *p == '?' || *p == '[')
                    quoted_wild = 1;
                wb_putc(&wb, *p++);
            }
            if (!closed) {
                *error = msh_strdup("unterminated double quote");
                goto fail;
            }
            started = 1;
            continue;
        }

        if (c == '\\') {
            ++p;
            if (*p != '\0') {
                if (*p == '*' || *p == '?' || *p == '[')
                    quoted_wild = 1;
                wb_putc(&wb, *p++);
            }
            started = 1;
            continue;
        }

        if (c == '$') {
            if (expand_dollar(&p, &wb, sh, error) < 0)
                goto fail;
            started = 1;
            continue;
        }

        if (c == '*' || c == '?' || c == '[')
            unquoted_wild = 1;
        wb_putc(&wb, c);
        ++p;
        started = 1;
    }

    if (!started) {
        *pp = p;
        free(wb.buf);
        return 0;
    }

    token_add(tl, TOK_WORD, wb.buf != NULL ? wb.buf : msh_strdup(""),
              unquoted_wild && !quoted_wild);
    *pp = p;
    return 1;

fail:
    free(wb.buf);
    return -1;
}

static int tokenize(const char *line, token_list_t *tl, shell_state_t *sh,
                    char **error)
{
    const char *p = line;

    while (*p != '\0') {
        token_type_t type;
        int length;
        int rc;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (*p == '\0')
            break;
        if (*p == '#')
            break;

        if (match_operator(p, &type, &length)) {
            token_add(tl, type, NULL, 0);
            p += length;
            continue;
        }

        rc = read_word(&p, tl, sh, error);
        if (rc < 0)
            return -1;
        if (rc == 0)
            break;
    }

    token_add(tl, TOK_END, NULL, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Command building                                                   */
/* ------------------------------------------------------------------ */

static void command_add_arg_raw(command_t *cmd, char *arg)
{
    if (cmd->argc + 1 >= MSH_MAX_ARGS) {
        msh_error("too many arguments (limit %d)", MSH_MAX_ARGS - 1);
        free(arg);
        return;
    }
    cmd->argv = msh_realloc(cmd->argv,
                            (size_t)(cmd->argc + 2) * sizeof(char *));
    cmd->argv[cmd->argc++] = arg;
    cmd->argv[cmd->argc] = NULL;
}

static void command_add_arg(command_t *cmd, char *arg, int do_glob)
{
    glob_t matches;
    int rc;

    if (!do_glob || strpbrk(arg, "*?[") == NULL) {
        command_add_arg_raw(cmd, arg);
        return;
    }

    memset(&matches, 0, sizeof(matches));
    rc = glob(arg, 0, NULL, &matches);
    if (rc == 0) {
        size_t i;

        for (i = 0; i < matches.gl_pathc; ++i)
            command_add_arg_raw(cmd, msh_strdup(matches.gl_pathv[i]));
        globfree(&matches);
        free(arg);
    } else {
        globfree(&matches);
        command_add_arg_raw(cmd, arg);
    }
}

static void pipeline_add_command(pipeline_t *pl, command_t *cmd)
{
    pl->commands = msh_realloc(pl->commands,
                               (size_t)(pl->ncommands + 1) *
                               sizeof(command_t));
    pl->commands[pl->ncommands++] = *cmd;
    memset(cmd, 0, sizeof(*cmd));
}

static void command_list_add(command_list_t *list, pipeline_t *pl,
                             connector_t connector)
{
    list->pipelines = msh_realloc(list->pipelines,
                                  (size_t)(list->npipelines + 1) *
                                  sizeof(pipeline_t));
    list->connectors = msh_realloc(list->connectors,
                                   (size_t)(list->npipelines + 1) *
                                   sizeof(connector_t));
    list->pipelines[list->npipelines] = *pl;
    list->connectors[list->npipelines] = connector;
    list->npipelines++;
    memset(pl, 0, sizeof(*pl));
}

static int is_redirect(token_type_t type)
{
    return type == TOK_IN || type == TOK_OUT || type == TOK_OUT_APPEND ||
           type == TOK_ERR || type == TOK_ERR_APPEND ||
           type == TOK_ERR_DUP_OUT || type == TOK_BOTH ||
           type == TOK_BOTH_APPEND;
}

static int parse_redirection(token_list_t *tl, int *index, redirect_t *redir,
                             char **error)
{
    token_type_t type = tl->items[*index].type;
    char *file;

    if (type == TOK_ERR_DUP_OUT) {
        ++*index;
        free(redir->err_path);
        redir->err_path = NULL;
        redir->err_to_out = 1;
        redir->merge_err = 0;
        return 0;
    }

    ++*index;
    if (*index >= tl->count || tl->items[*index].type != TOK_WORD) {
        *error = msh_strdup("syntax error: redirection requires a file name");
        return -1;
    }
    file = tl->items[*index].text;
    tl->items[*index].text = NULL;
    ++*index;

    switch (type) {
    case TOK_IN:
        free(redir->in_path);
        redir->in_path = file;
        break;
    case TOK_OUT:
        free(redir->out_path);
        redir->out_path = file;
        redir->out_append = 0;
        break;
    case TOK_OUT_APPEND:
        free(redir->out_path);
        redir->out_path = file;
        redir->out_append = 1;
        break;
    case TOK_ERR:
        free(redir->err_path);
        redir->err_path = file;
        redir->err_append = 0;
        redir->err_to_out = 0;
        redir->merge_err = 0;
        break;
    case TOK_ERR_APPEND:
        free(redir->err_path);
        redir->err_path = file;
        redir->err_append = 1;
        redir->err_to_out = 0;
        redir->merge_err = 0;
        break;
    case TOK_BOTH:
        free(redir->out_path);
        free(redir->err_path);
        redir->out_path = file;
        redir->err_path = NULL;
        redir->out_append = 0;
        redir->err_to_out = 0;
        redir->merge_err = 1;
        break;
    case TOK_BOTH_APPEND:
        free(redir->out_path);
        free(redir->err_path);
        redir->out_path = file;
        redir->err_path = NULL;
        redir->out_append = 1;
        redir->err_to_out = 0;
        redir->merge_err = 1;
        break;
    default:
        free(file);
        *error = msh_strdup("internal parser error");
        return -1;
    }
    return 0;
}

static char *build_pipeline_text(const pipeline_t *pl)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *fp;
    int i, j;

    fp = open_memstream(&buf, &len);
    if (fp == NULL)
        return msh_strdup("");

    for (i = 0; i < pl->ncommands; ++i) {
        const command_t *cmd = &pl->commands[i];

        if (i > 0)
            fputs(" | ", fp);
        for (j = 0; j < cmd->argc; ++j)
            fprintf(fp, "%s%s", j ? " " : "", cmd->argv[j]);
        if (cmd->redir.in_path)
            fprintf(fp, " < %s", cmd->redir.in_path);
        if (cmd->redir.out_path)
            fprintf(fp, " %s %s", cmd->redir.out_append ? ">>" : ">",
                    cmd->redir.out_path);
        if (cmd->redir.err_path)
            fprintf(fp, " %s %s", cmd->redir.err_append ? "2>>" : "2>",
                    cmd->redir.err_path);
        else if (cmd->redir.err_to_out)
            fputs(" 2>&1", fp);
    }
    if (pl->background)
        fputs(" &", fp);
    fclose(fp);
    return buf != NULL ? buf : msh_strdup("");
}

/* ------------------------------------------------------------------ */
/* Public parser API                                                  */
/* ------------------------------------------------------------------ */

int parse_line(const char *line, shell_state_t *sh, command_list_t *list,
               char **error)
{
    token_list_t tl = { 0 };
    command_list_t result = { 0 };
    pipeline_t pl;
    connector_t pending = CONN_ALWAYS;
    int i = 0;

    memset(&pl, 0, sizeof(pl));
    *error = NULL;
    memset(list, 0, sizeof(*list));

    if (tokenize(line, &tl, sh, error) < 0) {
        token_list_free(&tl);
        return -1;
    }

    while (i < tl.count && tl.items[i].type != TOK_END) {
        connector_t connector = pending;
        int command_started = 0;

        pending = CONN_ALWAYS;
        if (tl.items[i].type == TOK_SEMI) {
            ++i;
            continue;
        }
        if (tl.items[i].type == TOK_AND_IF || tl.items[i].type == TOK_OR_IF ||
            tl.items[i].type == TOK_AMP || tl.items[i].type == TOK_PIPE) {
            *error = msh_strdup("syntax error: unexpected operator");
            goto fail;
        }

        memset(&pl, 0, sizeof(pl));

        for (;;) {
            command_t cmd;

            memset(&cmd, 0, sizeof(cmd));

            while (i < tl.count) {
                token_type_t type = tl.items[i].type;

                if (type == TOK_WORD) {
                    char *word = tl.items[i].text;
                    int do_glob = tl.items[i].glob;
                    tl.items[i].text = NULL;
                    command_add_arg(&cmd, word, do_glob);
                    ++i;
                } else if (is_redirect(type)) {
                    if (parse_redirection(&tl, &i, &cmd.redir, error) < 0) {
                        free_command(&cmd);
                        goto fail;
                    }
                } else {
                    break;
                }
            }

            if (cmd.argc == 0) {
                *error = msh_strdup("syntax error: empty command");
                free_command(&cmd);
                goto fail;
            }

            pipeline_add_command(&pl, &cmd);
            command_started = 1;

            if (i < tl.count && tl.items[i].type == TOK_PIPE) {
                ++i;
                if (i >= tl.count || tl.items[i].type == TOK_END ||
                    tl.items[i].type == TOK_SEMI ||
                    tl.items[i].type == TOK_AMP ||
                    tl.items[i].type == TOK_AND_IF ||
                    tl.items[i].type == TOK_OR_IF) {
                    *error = msh_strdup("syntax error near '|'");
                    goto fail;
                }
                continue;
            }
            break;
        }

        if (!command_started) {
            *error = msh_strdup("syntax error: empty pipeline");
            goto fail;
        }

        if (i < tl.count && tl.items[i].type == TOK_AMP) {
            pl.background = 1;
            ++i;
            if (i < tl.count && (tl.items[i].type == TOK_AMP ||
                                 tl.items[i].type == TOK_AND_IF ||
                                 tl.items[i].type == TOK_OR_IF)) {
                *error = msh_strdup("syntax error near '&'");
                goto fail;
            }
        } else if (i < tl.count && tl.items[i].type == TOK_AND_IF) {
            pending = CONN_AND;
            ++i;
        } else if (i < tl.count && tl.items[i].type == TOK_OR_IF) {
            pending = CONN_OR;
            ++i;
        } else if (i < tl.count && tl.items[i].type == TOK_SEMI) {
            ++i;
        } else if (i < tl.count && tl.items[i].type != TOK_END) {
            *error = msh_strdup("syntax error: unexpected token");
            goto fail;
        }

        pl.text = build_pipeline_text(&pl);
        command_list_add(&result, &pl, connector);

        if (pending != CONN_ALWAYS &&
            (i >= tl.count || tl.items[i].type == TOK_END ||
             tl.items[i].type == TOK_SEMI || tl.items[i].type == TOK_AMP ||
             tl.items[i].type == TOK_AND_IF || tl.items[i].type == TOK_OR_IF ||
             tl.items[i].type == TOK_PIPE)) {
            *error = msh_strdup("syntax error: missing command after connector");
            goto fail;
        }
    }

    token_list_free(&tl);
    *list = result;
    return 0;

fail:
    free_pipeline(&pl);
    free_command_list(&result);
    token_list_free(&tl);
    return -1;
}

/* Expand one word without globbing.  Used by assignments and for-loops,
 * where the result is a single string and not an argv entry. */
/* ------------------------------------------------------------------ */
/* Freeing                                                            */
/* ------------------------------------------------------------------ */

void free_command(command_t *cmd)
{
    int i;

    for (i = 0; i < cmd->argc; ++i)
        free(cmd->argv[i]);
    free(cmd->argv);
    free(cmd->redir.in_path);
    free(cmd->redir.out_path);
    free(cmd->redir.err_path);
    memset(cmd, 0, sizeof(*cmd));
}

void free_pipeline(pipeline_t *pl)
{
    int i;

    for (i = 0; i < pl->ncommands; ++i)
        free_command(&pl->commands[i]);
    free(pl->commands);
    free(pl->text);
    memset(pl, 0, sizeof(*pl));
}

void free_command_list(command_list_t *list)
{
    int i;

    for (i = 0; i < list->npipelines; ++i)
        free_pipeline(&list->pipelines[i]);
    free(list->pipelines);
    free(list->connectors);
    memset(list, 0, sizeof(*list));
}
