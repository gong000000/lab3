/*
 * script.c - shell scripting: if/for/while, functions, and script files.
 *
 * The classic parser in parser.c turns one command line into a flat
 * pipeline list.  Scripts need statements and nesting, so this module
 * splits the source into statements, builds a small tree, and executes it.
 * Simple command statements are handed back to the original parser and
 * executor, which keeps pipes, redirections, globbing, and job control
 * working exactly as before.
 */
#include "myshell.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Small growable buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sbuf_t;

static void sb_reserve(sbuf_t *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;

    if (need <= sb->cap)
        return;
    if (sb->cap == 0)
        sb->cap = 64;
    while (sb->cap < need)
        sb->cap *= 2;
    sb->buf = msh_realloc(sb->buf, sb->cap);
}

static void sb_add(sbuf_t *sb, const char *text, size_t len)
{
    sb_reserve(sb, len);
    memcpy(sb->buf + sb->len, text, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
}

static void sb_adds(sbuf_t *sb, const char *text)
{
    sb_add(sb, text, strlen(text));
}

/* ------------------------------------------------------------------ */
/* Statement splitting                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char **items;
    int count;
    int cap;
} stmt_list_t;

static void stmt_add(stmt_list_t *sl, const char *start, size_t len)
{
    char *text;

    while (len > 0 && isspace((unsigned char)start[len - 1]))
        --len;
    while (len > 0 && isspace((unsigned char)*start)) {
        ++start;
        --len;
    }
    if (len == 0)
        return;
    text = msh_strndup(start, len);
    if (sl->count == sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 16;
        sl->items = msh_realloc(sl->items,
                                (size_t)sl->cap * sizeof(char *));
    }
    sl->items[sl->count++] = text;
}

static void stmt_list_free(stmt_list_t *sl)
{
    int i;

    for (i = 0; i < sl->count; ++i)
        free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

/* Split on unquoted ';' and newlines.  Semicolons inside $(...) belong to
 * the substituted command.  Returns -1 on an unterminated quote or
 * substitution, which the REPL treats as an incomplete line. */
static int split_statements(const char *source, stmt_list_t *out)
{
    const char *p = source;
    const char *start = source;
    int in_single = 0;
    int in_double = 0;
    int word_start = 1;

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
            word_start = 0;
            continue;
        }
        if (in_double) {
            if (c == '"') {
                in_double = 0;
                ++p;
                continue;
            }
            if (c == '$' && p[1] == '(') {
                const char *end = msh_find_substitution_end(p + 2);

                if (end == NULL)
                    return -1;
                p = end + 1;
                word_start = 0;
                continue;
            }
            ++p;
            continue;
        }
        if (c == '\'') {
            in_single = 1;
            ++p;
            word_start = 0;
            continue;
        }
        if (c == '"') {
            in_double = 1;
            ++p;
            word_start = 0;
            continue;
        }
        if (c == '$' && p[1] == '(') {
            const char *end = msh_find_substitution_end(p + 2);

            if (end == NULL)
                return -1;
            p = end + 1;
            word_start = 0;
            continue;
        }
        if (c == '#' && word_start) {
            stmt_add(out, start, (size_t)(p - start));
            while (*p != '\0' && *p != '\n')
                ++p;
            start = p;
            word_start = 1;
            continue;
        }
        if (c == ';' || c == '\n') {
            stmt_add(out, start, (size_t)(p - start));
            ++p;
            start = p;
            word_start = 1;
            continue;
        }
        word_start = isspace((unsigned char)c) || c == '|' || c == '&' ||
                     c == '(' || c == ')';
        ++p;
    }

    if (in_single || in_double)
        return -1;
    stmt_add(out, start, (size_t)(p - start));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Word helpers                                                       */
/* ------------------------------------------------------------------ */

static void first_word(const char *s, char *buffer, size_t size)
{
    size_t i = 0;

    while (*s != '\0' && !isspace((unsigned char)*s)) {
        if (i + 1 < size)
            buffer[i++] = *s;
        ++s;
    }
    buffer[i] = '\0';
}

static int starts_word(const char *s, const char *word)
{
    size_t n = strlen(word);

    if (strncmp(s, word, n) != 0)
        return 0;
    return s[n] == '\0' || isspace((unsigned char)s[n]);
}

static const char *rest_after_word(const char *s, const char *word)
{
    const char *p = s + strlen(word);

    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

/* ------------------------------------------------------------------ */
/* Function header and brace scanning                                 */
/* ------------------------------------------------------------------ */

static int parse_function_header(const char *s, char **name_out,
                                 const char **rest_out)
{
    const char *p = s;
    const char *name_start;
    char *name;
    const char *q;

    if (!(isalpha((unsigned char)*p) || *p == '_'))
        return 0;
    name_start = p;
    while (isalnum((unsigned char)*p) || *p == '_')
        ++p;
    name = msh_strndup(name_start, (size_t)(p - name_start));

    q = p;
    while (*q == ' ' || *q == '\t')
        ++q;
    if (*q != '(') {
        free(name);
        return 0;
    }
    ++q;
    while (*q == ' ' || *q == '\t')
        ++q;
    if (*q != ')') {
        free(name);
        return 0;
    }
    ++q;
    *name_out = name;
    *rest_out = q;
    return 1;
}

/* Return the '}' matching an already-consumed '{', or NULL. */
static const char *find_closing_brace(const char *p)
{
    int depth = 0;

    while (*p != '\0') {
        if (*p == '\\') {
            if (p[1] != '\0')
                ++p;
            ++p;
            continue;
        }
        if (*p == '\'') {
            ++p;
            while (*p != '\0' && *p != '\'')
                ++p;
            if (*p == '\'')
                ++p;
            continue;
        }
        if (*p == '"') {
            ++p;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    ++p;
                } else if (*p == '$' && p[1] == '(') {
                    const char *end = msh_find_substitution_end(p + 2);

                    if (end == NULL)
                        return NULL;
                    p = end;
                }
                ++p;
            }
            if (*p == '"')
                ++p;
            continue;
        }
        if (*p == '$' && p[1] == '(') {
            const char *end = msh_find_substitution_end(p + 2);

            if (end == NULL)
                return NULL;
            p = end + 1;
            continue;
        }
        if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            if (depth == 0)
                return p;
            --depth;
        }
        ++p;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Parse tree                                                         */
/* ------------------------------------------------------------------ */

enum {
    SN_LIST = 0,
    SN_CMD,
    SN_IF,
    SN_FOR,
    SN_WHILE,
    SN_FUNC
};

typedef struct snode snode_t;

typedef struct {
    char *cond;
    snode_t *body;
} sif_branch_t;

struct snode {
    int type;
    char *cmd;
    snode_t **items;
    int nitems;
    sif_branch_t *branches;
    int nbranches;
    snode_t *else_body;
    char *var;
    char **words;
    int nwords;
    int use_positional;
    char *cond;
    int until;
    char *redir_suffix;
    snode_t *body;
    char *name;
    char *func_body;
};

static snode_t *node_new(int type)
{
    snode_t *node = msh_calloc(1, sizeof(*node));

    node->type = type;
    return node;
}

static void node_free(snode_t *node)
{
    int i;

    if (node == NULL)
        return;
    switch (node->type) {
    case SN_LIST:
        for (i = 0; i < node->nitems; ++i)
            node_free(node->items[i]);
        free(node->items);
        break;
    case SN_CMD:
        free(node->cmd);
        break;
    case SN_IF:
        for (i = 0; i < node->nbranches; ++i) {
            free(node->branches[i].cond);
            node_free(node->branches[i].body);
        }
        free(node->branches);
        node_free(node->else_body);
        free(node->redir_suffix);
        break;
    case SN_FOR:
        free(node->var);
        for (i = 0; i < node->nwords; ++i)
            free(node->words[i]);
        free(node->words);
        free(node->redir_suffix);
        node_free(node->body);
        break;
    case SN_WHILE:
        free(node->cond);
        free(node->redir_suffix);
        node_free(node->body);
        break;
    case SN_FUNC:
        free(node->name);
        free(node->func_body);
        break;
    default:
        break;
    }
    free(node);
}

static void list_add(snode_t *list, snode_t *item)
{
    list->items = msh_realloc(list->items,
                              (size_t)(list->nitems + 1) * sizeof(snode_t *));
    list->items[list->nitems++] = item;
}

typedef struct {
    stmt_list_t *sl;
    int pos;
    char *error;
} sp_t;

static const char *sp_current(const sp_t *sp)
{
    return sp->sl->items[sp->pos];
}

static void sp_error(sp_t *sp, const char *message)
{
    if (sp->error == NULL)
        sp->error = msh_strdup(message);
}

static void sp_replace_current(sp_t *sp, const char *text)
{
    char *copy = msh_strdup(text);

    free(sp->sl->items[sp->pos]);
    sp->sl->items[sp->pos] = copy;
}

static int is_stop_word(const char *s, const char *const *stops, int nstops)
{
    char word[64];
    int i;

    first_word(s, word, sizeof(word));
    for (i = 0; i < nstops; ++i) {
        if (strcmp(word, stops[i]) == 0)
            return 1;
    }
    return 0;
}

/* Consume "then"/"do".  If the keyword shares its statement with the body
 * ("then echo hi"), the rest of that statement becomes the current one. */
static int consume_keyword(sp_t *sp, const char *keyword)
{
    const char *s;
    const char *rest;

    if (sp->pos >= sp->sl->count)
        return 0;
    s = sp_current(sp);
    if (!starts_word(s, keyword))
        return 0;
    rest = rest_after_word(s, keyword);
    if (*rest != '\0')
        sp_replace_current(sp, rest);
    else
        ++sp->pos;
    return 1;
}

static void define_function(shell_state_t *sh, const char *name,
                            const char *body)
{
    int i;

    for (i = 0; i < sh->function_count; ++i) {
        if (strcmp(sh->functions[i].name, name) == 0) {
            free(sh->functions[i].body);
            sh->functions[i].body = msh_strdup(body);
            return;
        }
    }
    if (sh->function_count == sh->function_cap) {
        sh->function_cap = sh->function_cap ? sh->function_cap * 2 : 16;
        sh->functions = msh_realloc(sh->functions,
                                    (size_t)sh->function_cap *
                                    sizeof(*sh->functions));
    }
    sh->functions[sh->function_count].name = msh_strdup(name);
    sh->functions[sh->function_count].body = msh_strdup(body);
    ++sh->function_count;
}

/* Parse NAME() { body } or "function NAME { body }". */
static snode_t *parse_function(sp_t *sp)
{
    const char *s = sp_current(sp);
    const char *rest;
    char *name = NULL;
    int done = 0;
    sbuf_t body = { 0 };
    snode_t *node;

    if (starts_word(s, "function")) {
        const char *p = rest_after_word(s, "function");
        const char *name_start = p;

        if (!(isalpha((unsigned char)*p) || *p == '_')) {
            sp_error(sp, "function: invalid name");
            return NULL;
        }
        while (isalnum((unsigned char)*p) || *p == '_')
            ++p;
        name = msh_strndup(name_start, (size_t)(p - name_start));
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '(') {
            ++p;
            while (*p == ' ' || *p == '\t')
                ++p;
            if (*p == ')')
                ++p;
        }
        rest = p;
    } else if (!parse_function_header(s, &name, &rest)) {
        sp_error(sp, "invalid function definition");
        return NULL;
    }

    ++sp->pos;
    while (*rest == ' ' || *rest == '\t')
        ++rest;
    if (*rest == '\0') {
        /* The opening brace may be on the following line. */
        if (sp->pos < sp->sl->count && sp_current(sp)[0] == '{') {
            rest = sp_current(sp) + 1;
            ++sp->pos;
        }
    }
    if (*rest != '{') {
        sp_error(sp, "function: expected '{'");
        free(name);
        return NULL;
    }
    ++rest;

    {
        const char *close = find_closing_brace(rest);

        if (close != NULL) {
            const char *tail = close + 1;

            while (*tail == ' ' || *tail == '\t')
                ++tail;
            if (*tail != '\0') {
                sp_error(sp, "function: text after closing '}'");
                free(name);
                free(body.buf);
                return NULL;
            }
            sb_add(&body, rest, (size_t)(close - rest));
            done = 1;
        } else if (*rest != '\0') {
            sb_adds(&body, rest);
        }
    }

    {
        int depth = 1;

        while (!done && sp->pos < sp->sl->count) {
            const char *line = sp_current(sp);
            int nested_open = 0;

            if (line[0] == '}') {
                --depth;
                if (depth == 0) {
                    ++sp->pos;
                    done = 1;
                    break;
                }
                /* A nested function's closing brace stays in the body. */
            } else if (line[0] == '{') {
                nested_open = 1;
            } else {
                char *nested_name = NULL;
                const char *nested_rest = NULL;

                if (parse_function_header(line, &nested_name, &nested_rest)) {
                    const char *q = nested_rest;

                    free(nested_name);
                    while (*q == ' ' || *q == '\t')
                        ++q;
                    if (*q == '{' && find_closing_brace(q + 1) == NULL)
                        nested_open = 1;
                }
            }

            if (nested_open)
                ++depth;
            if (body.len > 0)
                sb_adds(&body, "\n");
            sb_adds(&body, line);
            ++sp->pos;
        }
    }

    if (!done) {
        sp_error(sp, "function: missing '}'");
        free(name);
        free(body.buf);
        return NULL;
    }

    node = node_new(SN_FUNC);
    node->name = name;
    node->func_body = body.buf != NULL ? body.buf : msh_strdup("");
    return node;
}

static snode_t *parse_block(sp_t *sp, const char *const *stops, int nstops,
                            int *hit_stop);

static void consume_fi(sp_t *sp, snode_t *node)
{
    const char *rest = rest_after_word(sp_current(sp), "fi");

    if (*rest != '\0')
        node->redir_suffix = msh_strdup(rest);
    ++sp->pos;
}

static snode_t *parse_if(sp_t *sp)
{
    static const char *const if_stops[] = { "elif", "else", "fi" };
    static const char *const else_stops[] = { "fi" };
    snode_t *node = node_new(SN_IF);
    char *cond;
    int hit = 0;

    cond = msh_strdup(rest_after_word(sp_current(sp), "if"));
    ++sp->pos;
    if (cond[0] == '\0') {
        sp_error(sp, "if: missing condition");
        free(cond);
        node_free(node);
        return NULL;
    }

    for (;;) {
        snode_t *body;

        if (!consume_keyword(sp, "then")) {
            sp_error(sp, "if: expected 'then'");
            free(cond);
            node_free(node);
            return NULL;
        }
        body = parse_block(sp, if_stops, 3, &hit);
        if (body == NULL) {
            free(cond);
            node_free(node);
            return NULL;
        }
        node->branches = msh_realloc(node->branches,
                                     (size_t)(node->nbranches + 1) *
                                     sizeof(*node->branches));
        node->branches[node->nbranches].cond = cond;
        node->branches[node->nbranches].body = body;
        ++node->nbranches;
        cond = NULL;

        if (!hit) {
            sp_error(sp, "if: missing 'fi'");
            node_free(node);
            return NULL;
        }
        if (starts_word(sp_current(sp), "fi")) {
            consume_fi(sp, node);
            return node;
        }
        if (starts_word(sp_current(sp), "elif")) {
            cond = msh_strdup(rest_after_word(sp_current(sp), "elif"));
            ++sp->pos;
            if (cond[0] == '\0') {
                sp_error(sp, "elif: missing condition");
                free(cond);
                node_free(node);
                return NULL;
            }
            continue;
        }
        if (starts_word(sp_current(sp), "else")) {
            const char *rest = rest_after_word(sp_current(sp), "else");

            if (*rest != '\0') {
                sp_replace_current(sp, rest);
            } else {
                ++sp->pos;
            }
            node->else_body = parse_block(sp, else_stops, 1, &hit);
            if (node->else_body == NULL) {
                node_free(node);
                return NULL;
            }
            if (!hit || !starts_word(sp_current(sp), "fi")) {
                sp_error(sp, "if: missing 'fi'");
                node_free(node);
                return NULL;
            }
            consume_fi(sp, node);
            return node;
        }
    }
}

/* Collect the "done" suffix (redirections such as "< file"). */
static void consume_done(sp_t *sp, snode_t *node)
{
    const char *rest = rest_after_word(sp_current(sp), "done");

    if (*rest != '\0')
        node->redir_suffix = msh_strdup(rest);
    ++sp->pos;
}

static snode_t *parse_while(sp_t *sp)
{
    static const char *const stops[] = { "done" };
    snode_t *node = node_new(SN_WHILE);
    int hit = 0;
    const char *keyword = "while";

    if (starts_word(sp_current(sp), "until")) {
        keyword = "until";
        node->until = 1;
    }
    node->cond = msh_strdup(rest_after_word(sp_current(sp), keyword));
    ++sp->pos;
    if (node->cond[0] == '\0') {
        sp_error(sp, "loop: missing condition");
        node_free(node);
        return NULL;
    }
    if (!consume_keyword(sp, "do")) {
        sp_error(sp, "loop: expected 'do'");
        node_free(node);
        return NULL;
    }
    node->body = parse_block(sp, stops, 1, &hit);
    if (node->body == NULL) {
        node_free(node);
        return NULL;
    }
    if (!hit) {
        sp_error(sp, "loop: missing 'done'");
        node_free(node);
        return NULL;
    }
    consume_done(sp, node);
    return node;
}

static char **split_raw_words(const char *text, int *count)
{
    char **words = NULL;
    int nwords = 0;
    const char *p = text;

    *count = 0;
    while (*p != '\0') {
        const char *start;
        int in_single = 0;
        int in_double = 0;

        while (*p != '\0' && isspace((unsigned char)*p))
            ++p;
        if (*p == '\0')
            break;
        start = p;
        while (*p != '\0') {
            char c = *p;

            if (in_single) {
                if (c == '\'')
                    in_single = 0;
                ++p;
                continue;
            }
            if (in_double) {
                if (c == '"') {
                    in_double = 0;
                    ++p;
                    continue;
                }
                if (c == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (c == '$' && p[1] == '(') {
                    const char *end = msh_find_substitution_end(p + 2);

                    if (end == NULL)
                        break;
                    p = end + 1;
                    continue;
                }
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
            if (c == '\\' && p[1] != '\0') {
                p += 2;
                continue;
            }
            if (c == '$' && p[1] == '(') {
                const char *end = msh_find_substitution_end(p + 2);

                if (end == NULL)
                    break;
                p = end + 1;
                continue;
            }
            if (isspace((unsigned char)c))
                break;
            ++p;
        }
        words = msh_realloc(words, (size_t)(nwords + 1) * sizeof(char *));
        words[nwords++] = msh_strndup(start, (size_t)(p - start));
    }
    *count = nwords;
    return words;
}

static snode_t *parse_for(sp_t *sp)
{
    static const char *const stops[] = { "done" };
    snode_t *node = node_new(SN_FOR);
    const char *rest = rest_after_word(sp_current(sp), "for");
    const char *p = rest;
    const char *name_start;
    int hit = 0;

    if (!(isalpha((unsigned char)*p) || *p == '_')) {
        sp_error(sp, "for: invalid variable name");
        node_free(node);
        return NULL;
    }
    name_start = p;
    while (isalnum((unsigned char)*p) || *p == '_')
        ++p;
    node->var = msh_strndup(name_start, (size_t)(p - name_start));
    while (*p == ' ' || *p == '\t')
        ++p;

    if (*p == '\0') {
        node->use_positional = 1;
    } else if (starts_word(p, "in")) {
        node->words = split_raw_words(rest_after_word(p, "in"),
                                      &node->nwords);
    } else {
        sp_error(sp, "for: expected 'in' or 'do'");
        node_free(node);
        return NULL;
    }

    ++sp->pos;
    if (!consume_keyword(sp, "do")) {
        sp_error(sp, "for: expected 'do'");
        node_free(node);
        return NULL;
    }
    node->body = parse_block(sp, stops, 1, &hit);
    if (node->body == NULL) {
        node_free(node);
        return NULL;
    }
    if (!hit) {
        sp_error(sp, "for: missing 'done'");
        node_free(node);
        return NULL;
    }
    consume_done(sp, node);
    return node;
}

static snode_t *parse_block(sp_t *sp, const char *const *stops, int nstops,
                            int *hit_stop)
{
    snode_t *list = node_new(SN_LIST);

    if (hit_stop != NULL)
        *hit_stop = 0;
    while (sp->pos < sp->sl->count) {
        const char *s = sp_current(sp);
        char word[64];
        char *fname = NULL;
        const char *frest = NULL;

        if (is_stop_word(s, stops, nstops)) {
            if (hit_stop != NULL)
                *hit_stop = 1;
            break;
        }
        first_word(s, word, sizeof(word));

        if (strcmp(word, "if") == 0) {
            snode_t *node = parse_if(sp);

            if (node == NULL) {
                node_free(list);
                return NULL;
            }
            list_add(list, node);
            continue;
        }
        if (strcmp(word, "for") == 0) {
            snode_t *node = parse_for(sp);

            if (node == NULL) {
                node_free(list);
                return NULL;
            }
            list_add(list, node);
            continue;
        }
        if (strcmp(word, "while") == 0 || strcmp(word, "until") == 0) {
            snode_t *node = parse_while(sp);

            if (node == NULL) {
                node_free(list);
                return NULL;
            }
            list_add(list, node);
            continue;
        }
        if (strcmp(word, "function") == 0 ||
            parse_function_header(s, &fname, &frest)) {
            snode_t *node;

            free(fname);
            node = parse_function(sp);
            if (node == NULL) {
                node_free(list);
                return NULL;
            }
            list_add(list, node);
            continue;
        }

        if (strcmp(word, "fi") == 0 || strcmp(word, "done") == 0 ||
            strcmp(word, "then") == 0 || strcmp(word, "do") == 0 ||
            strcmp(word, "else") == 0 || strcmp(word, "elif") == 0 ||
            s[0] == '}') {
            sp_error(sp, "unexpected reserved word");
            node_free(list);
            return NULL;
        }

        {
            snode_t *node = node_new(SN_CMD);

            node->cmd = msh_strdup(s);
            list_add(list, node);
            ++sp->pos;
        }
    }
    return list;
}

/* ------------------------------------------------------------------ */
/* Execution                                                          */
/* ------------------------------------------------------------------ */

enum {
    CTRL_NONE = 0,
    CTRL_BREAK,
    CTRL_CONTINUE,
    CTRL_RETURN
};

static int exec_node(snode_t *node, shell_state_t *sh);
static int exec_node_inner(snode_t *node, shell_state_t *sh, int skip_redir);

static int condition_true(const char *condition, shell_state_t *sh)
{
    const char *p = condition;
    int invert = 0;
    int rc;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p == '!' && (p[1] == '\0' || isspace((unsigned char)p[1]))) {
        invert = 1;
        ++p;
        while (*p == ' ' || *p == '\t')
            ++p;
    }
    if (*p == '\0') {
        sh->last_status = 2;
        return 0;
    }
    rc = msh_execute_simple(p, sh);
    return invert ? (rc != 0) : (rc == 0);
}

static int assignment_split(const char *text, char **name_out,
                            const char **value_out)
{
    const char *p = text;
    const char *start = p;

    if (!(isalpha((unsigned char)*p) || *p == '_'))
        return 0;
    while (isalnum((unsigned char)*p) || *p == '_')
        ++p;
    if (*p != '=')
        return 0;
    *name_out = msh_strndup(start, (size_t)(p - start));
    *value_out = p + 1;
    return 1;
}

static int exec_command_node(const char *text, shell_state_t *sh)
{
    char *name = NULL;
    const char *value = NULL;

    if (assignment_split(text, &name, &value)) {
        char *error = NULL;
        char *expanded = msh_expand_word(value, sh, &error);

        if (expanded == NULL) {
            msh_error("%s: %s", name,
                      error != NULL ? error : "bad substitution");
            free(error);
            free(name);
            sh->last_status = 2;
            return CTRL_NONE;
        }
        if (setenv(name, expanded, 1) < 0)
            msh_perror("setenv");
        free(expanded);
        free(name);
        sh->last_status = 0;
        return CTRL_NONE;
    }

    if (starts_word(text, "break")) {
        sh->last_status = 0;
        return CTRL_BREAK;
    }
    if (starts_word(text, "continue")) {
        sh->last_status = 0;
        return CTRL_CONTINUE;
    }
    if (starts_word(text, "return")) {
        const char *rest = rest_after_word(text, "return");
        int status = *rest != '\0' ? atoi(rest) : sh->last_status;

        sh->last_status = status;
        return CTRL_RETURN;
    }

    (void)msh_execute_simple(text, sh);
    return CTRL_NONE;
}

static void append_word(char ***words, int *count, const char *text,
                        size_t len)
{
    char *copy = msh_strndup(text, len);

    *words = msh_realloc(*words, (size_t)(*count + 1) * sizeof(char *));
    (*words)[(*count)++] = copy;
}

static int raw_word_quoted(const char *raw)
{
    size_t len = strlen(raw);

    if (len < 2)
        return 0;
    if (raw[0] == '"' && raw[len - 1] == '"')
        return 1;
    if (raw[0] == '\'' && raw[len - 1] == '\'')
        return 1;
    return 0;
}

static int expand_words(char **raw, int nraw, shell_state_t *sh,
                        char ***out, int *out_count)
{
    char **result = NULL;
    int count = 0;
    int i;

    for (i = 0; i < nraw; ++i) {
        char *error = NULL;
        char *expanded;
        const char *p;

        /* "$@" expands to one field per positional parameter. */
        if (strcmp(raw[i], "\"$@\"") == 0 || strcmp(raw[i], "$@") == 0) {
            int j;

            for (j = 0; j < sh->positional_count; ++j)
                append_word(&result, &count, sh->positional[j],
                            strlen(sh->positional[j]));
            continue;
        }

        expanded = msh_expand_word(raw[i], sh, &error);
        if (expanded == NULL) {
            msh_error("%s", error != NULL ? error : "bad substitution");
            free(error);
            goto fail;
        }
        if (raw_word_quoted(raw[i])) {
            append_word(&result, &count, expanded, strlen(expanded));
            free(expanded);
            continue;
        }
        p = expanded;
        while (*p != '\0') {
            const char *start;

            while (*p != '\0' && isspace((unsigned char)*p))
                ++p;
            if (*p == '\0')
                break;
            start = p;
            while (*p != '\0' && !isspace((unsigned char)*p))
                ++p;
            append_word(&result, &count, start, (size_t)(p - start));
        }
        free(expanded);
    }
    *out = result;
    *out_count = count;
    return 0;

fail:
    for (i = 0; i < count; ++i)
        free(result[i]);
    free(result);
    return -1;
}

static int exec_with_redirection(const char *suffix, snode_t *body,
                                 shell_state_t *sh)
{
    char *text = msh_malloc(strlen(suffix) + 4);
    command_list_t list;
    char *error = NULL;
    int saved[3] = { -1, -1, -1 };
    int ctrl;
    int i;

    sprintf(text, ": %s", suffix);
    if (parse_line(text, sh, &list, &error) < 0) {
        msh_error("syntax error: %s",
                  error != NULL ? error : "bad redirection");
        free(error);
        free(text);
        sh->last_status = 2;
        return CTRL_NONE;
    }
    free(text);
    if (list.npipelines != 1 || list.pipelines[0].ncommands != 1) {
        msh_error("syntax error: bad redirection");
        free_command_list(&list);
        sh->last_status = 2;
        return CTRL_NONE;
    }

    for (i = 0; i < 3; ++i) {
        saved[i] = dup(i);
        if (saved[i] < 0) {
            msh_perror("dup");
            while (--i >= 0)
                close(saved[i]);
            free_command_list(&list);
            return CTRL_NONE;
        }
    }
    if (apply_redirections(&list.pipelines[0].commands[0].redir) < 0) {
        for (i = 0; i < 3; ++i) {
            (void)dup2(saved[i], i);
            close(saved[i]);
        }
        free_command_list(&list);
        return CTRL_NONE;
    }

    ctrl = exec_node_inner(body, sh, 1);

    for (i = 0; i < 3; ++i) {
        (void)dup2(saved[i], i);
        close(saved[i]);
    }
    free_command_list(&list);
    return ctrl;
}

static int exec_for(snode_t *node, shell_state_t *sh)
{
    char **words = NULL;
    int nwords = 0;
    int use_positional = node->use_positional;
    int ctrl = CTRL_NONE;
    int i;

    if (use_positional) {
        if (node->nwords > 0) {
            if (expand_words(node->words, node->nwords, sh, &words,
                             &nwords) < 0)
                return CTRL_NONE;
        } else {
            nwords = sh->positional_count;
            if (nwords > 0) {
                words = msh_calloc((size_t)nwords, sizeof(char *));
                for (i = 0; i < nwords; ++i)
                    words[i] = msh_strdup(sh->positional[i]);
            }
        }
    } else if (expand_words(node->words, node->nwords, sh, &words,
                            &nwords) < 0) {
        return CTRL_NONE;
    }

    sh->last_status = 0;
    for (i = 0; i < nwords && !sh->should_exit; ++i) {
        setenv(node->var, words[i], 1);
        ctrl = exec_node(node->body, sh);
        if (ctrl == CTRL_BREAK) {
            ctrl = CTRL_NONE;
            break;
        }
        if (ctrl == CTRL_CONTINUE) {
            ctrl = CTRL_NONE;
            continue;
        }
        if (ctrl != CTRL_NONE)
            break;
    }

    for (i = 0; i < nwords; ++i)
        free(words[i]);
    free(words);
    return ctrl;
}

static int exec_while(snode_t *node, shell_state_t *sh)
{
    int ctrl = CTRL_NONE;
    int last_body_status = 0;

    while (!sh->should_exit) {
        int truth = condition_true(node->cond, sh);

        if (node->until)
            truth = !truth;
        if (!truth)
            break;
        ctrl = exec_node(node->body, sh);
        if (ctrl == CTRL_BREAK) {
            ctrl = CTRL_NONE;
            break;
        }
        if (ctrl == CTRL_CONTINUE) {
            ctrl = CTRL_NONE;
            continue;
        }
        if (ctrl != CTRL_NONE)
            break;
        last_body_status = sh->last_status;
    }
    if (ctrl == CTRL_NONE)
        sh->last_status = last_body_status;
    return ctrl;
}

static int exec_node_inner(snode_t *node, shell_state_t *sh, int skip_redir)
{
    int ctrl;
    int i;

    if (node == NULL || sh->should_exit)
        return CTRL_NONE;

    switch (node->type) {
    case SN_LIST:
        for (i = 0; i < node->nitems; ++i) {
            ctrl = exec_node_inner(node->items[i], sh, 0);
            if (ctrl != CTRL_NONE)
                return ctrl;
        }
        return CTRL_NONE;

    case SN_CMD:
        return exec_command_node(node->cmd, sh);

    case SN_IF:
        if (node->redir_suffix != NULL && !skip_redir)
            return exec_with_redirection(node->redir_suffix, node, sh);
        for (i = 0; i < node->nbranches; ++i) {
            if (condition_true(node->branches[i].cond, sh))
                return exec_node_inner(node->branches[i].body, sh, 0);
        }
        if (node->else_body != NULL)
            return exec_node_inner(node->else_body, sh, 0);
        sh->last_status = 0;
        return CTRL_NONE;

    case SN_FOR:
        if (node->redir_suffix != NULL && !skip_redir)
            return exec_with_redirection(node->redir_suffix, node, sh);
        return exec_for(node, sh);

    case SN_WHILE:
        if (node->redir_suffix != NULL && !skip_redir)
            return exec_with_redirection(node->redir_suffix, node, sh);
        return exec_while(node, sh);

    case SN_FUNC:
        define_function(sh, node->name, node->func_body);
        sh->last_status = 0;
        return CTRL_NONE;

    default:
        return CTRL_NONE;
    }
}

static int exec_node(snode_t *node, shell_state_t *sh)
{
    return exec_node_inner(node, sh, 0);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int msh_is_function(shell_state_t *sh, const char *name)
{
    int i;

    for (i = 0; i < sh->function_count; ++i) {
        if (strcmp(sh->functions[i].name, name) == 0)
            return 1;
    }
    return 0;
}

int msh_call_function(shell_state_t *sh, const char *name, int argc,
                      char **argv)
{
    char **saved_args = sh->positional;
    int saved_count = sh->positional_count;
    int i;

    for (i = 0; i < sh->function_count; ++i) {
        if (strcmp(sh->functions[i].name, name) == 0) {
            int ctrl;

            sh->positional = argv;
            sh->positional_count = argc;
            ctrl = script_execute(sh->functions[i].body, sh);
            sh->positional = saved_args;
            sh->positional_count = saved_count;
            (void)ctrl;
            return sh->last_status;
        }
    }
    msh_error("%s: function not found", name);
    return 127;
}

void msh_functions_free(shell_state_t *sh)
{
    int i;

    for (i = 0; i < sh->function_count; ++i) {
        free(sh->functions[i].name);
        free(sh->functions[i].body);
    }
    free(sh->functions);
    sh->functions = NULL;
    sh->function_count = 0;
    sh->function_cap = 0;
}

int script_execute(const char *source, shell_state_t *sh)
{
    stmt_list_t statements = { 0 };
    sp_t sp;
    snode_t *root;
    int ctrl;

    if (split_statements(source, &statements) < 0) {
        msh_error("syntax error: unterminated quote or substitution");
        stmt_list_free(&statements);
        sh->last_status = 2;
        return 2;
    }

    memset(&sp, 0, sizeof(sp));
    sp.sl = &statements;
    root = parse_block(&sp, NULL, 0, NULL);
    if (root == NULL) {
        msh_error("syntax error: %s",
                  sp.error != NULL ? sp.error : "bad script");
        free(sp.error);
        stmt_list_free(&statements);
        sh->last_status = 2;
        return 2;
    }

    ctrl = exec_node(root, sh);
    if (ctrl == CTRL_BREAK || ctrl == CTRL_CONTINUE) {
        msh_error("%s: only meaningful in a loop",
                  ctrl == CTRL_BREAK ? "break" : "continue");
    }
    /* CTRL_RETURN is consumed here; the status is already in sh. */

    node_free(root);
    free(sp.error);
    stmt_list_free(&statements);
    return sh->last_status;
}

/* First word of a statement after skipping inline "then"/"do"/"else"
 * prefixes, used by the completeness scanner. */
static const char *skip_inline_prefixes(const char *s)
{
    for (;;) {
        const char *rest = NULL;

        if (starts_word(s, "then"))
            rest = rest_after_word(s, "then");
        else if (starts_word(s, "do"))
            rest = rest_after_word(s, "do");
        else if (starts_word(s, "else"))
            rest = rest_after_word(s, "else");
        if (rest == NULL || *rest == '\0')
            return s;
        s = rest;
    }
}

static int statement_needs_parser(const char *s)
{
    static const char *const reserved[] = {
        "if", "for", "while", "until", "function", "then", "do", "done",
        "fi", "else", "elif", "break", "continue", "return", "{", "}"
    };
    char word[64];
    char *name = NULL;
    const char *rest = NULL;
    size_t i;

    first_word(s, word, sizeof(word));
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        if (strcmp(word, reserved[i]) == 0)
            return 1;
    }
    if (parse_function_header(s, &name, &rest)) {
        free(name);
        return 1;
    }
    return 0;
}

int script_source_needs_parser(const char *source)
{
    stmt_list_t statements = { 0 };
    int found = 0;
    int i;

    if (split_statements(source, &statements) < 0)
        return 1;
    for (i = 0; i < statements.count && !found; ++i) {
        char *name = NULL;
        const char *value = NULL;

        if (statement_needs_parser(statements.items[i]))
            found = 1;
        else if (assignment_split(statements.items[i], &name, &value)) {
            free(name);
            found = 1;
        }
    }
    stmt_list_free(&statements);
    return found;
}

int script_source_is_complete(const char *source)
{
    stmt_list_t statements = { 0 };
    int if_depth = 0;
    int loop_depth = 0;
    int brace_depth = 0;
    int pending_function = 0;
    int i;
    size_t len;

    if (split_statements(source, &statements) < 0)
        return 0;

    for (i = 0; i < statements.count; ++i) {
        const char *s = skip_inline_prefixes(statements.items[i]);
        char word[64];
        char *name = NULL;
        const char *rest = NULL;

        if (pending_function) {
            const char *p = s;

            while (*p == ' ' || *p == '\t')
                ++p;
            if (*p == '{') {
                pending_function = 0;
                if (find_closing_brace(p + 1) == NULL)
                    ++brace_depth;
                continue;
            }
        }

        if (parse_function_header(s, &name, &rest)) {
            const char *p = rest;

            free(name);
            while (*p == ' ' || *p == '\t')
                ++p;
            if (*p == '{') {
                if (find_closing_brace(p + 1) == NULL)
                    ++brace_depth;
            } else {
                pending_function = 1;
            }
            continue;
        }
        if (starts_word(s, "function")) {
            pending_function = 1;
            continue;
        }

        first_word(s, word, sizeof(word));
        if (strcmp(word, "if") == 0) {
            ++if_depth;
        } else if (strcmp(word, "fi") == 0) {
            if (if_depth > 0)
                --if_depth;
        } else if (strcmp(word, "for") == 0 ||
                   strcmp(word, "while") == 0 ||
                   strcmp(word, "until") == 0) {
            ++loop_depth;
        } else if (strcmp(word, "done") == 0) {
            if (loop_depth > 0)
                --loop_depth;
        } else if (s[0] == '{') {
            ++brace_depth;
        } else if (s[0] == '}') {
            if (brace_depth > 0)
                --brace_depth;
        }
    }

    stmt_list_free(&statements);
    if (if_depth > 0 || loop_depth > 0 || brace_depth > 0 || pending_function)
        return 0;

    len = strlen(source);
    while (len > 0 && isspace((unsigned char)source[len - 1]))
        --len;
    if (len > 0) {
        if (source[len - 1] == '|' || source[len - 1] == '\\')
            return 0;
        if (len >= 2 && ((source[len - 2] == '&' && source[len - 1] == '&') ||
                         (source[len - 2] == '|' && source[len - 1] == '|')))
            return 0;
    }
    return 1;
}
