/*
 * lineedit.c - terminal line editor, history, completion, and Ctrl-R search.
 *
 * Only the interactive REPL uses this.  Non-interactive input (-c, scripts,
 * pipes) keeps the plain getline() path, so existing tests are unaffected.
 */
#include "myshell.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#define HIST_MAX 1000

typedef struct {
    char **items;
    int count;
    int cap;
} strlist_t;

static char *history_file(void)
{
    const char *home = getenv("HOME");
    char *path;

    if (home == NULL || home[0] == '\0')
        return NULL;
    path = msh_join_path(home, ".myshell_history");
    return path;
}

void history_add(shell_state_t *sh, const char *line)
{
    if (line == NULL || line[0] == '\0')
        return;
    if (sh->history_count > 0 &&
        strcmp(sh->history[sh->history_count - 1], line) == 0)
        return;
    if (sh->history_count == sh->history_cap) {
        int newcap = sh->history_cap ? sh->history_cap * 2 : 32;

        sh->history = msh_realloc(sh->history,
                                  (size_t)newcap * sizeof(char *));
        sh->history_cap = newcap;
    }
    sh->history[sh->history_count++] = msh_strdup(line);
    if (sh->history_count > HIST_MAX) {
        int i;

        free(sh->history[0]);
        for (i = 1; i < sh->history_count; ++i)
            sh->history[i - 1] = sh->history[i];
        --sh->history_count;
    }
}

void history_save(shell_state_t *sh, const char *line)
{
    char *path = history_file();
    int fd;
    size_t len;

    (void)sh;
    if (path == NULL || line == NULL || line[0] == '\0')
        return;
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        free(path);
        return;
    }
    len = strlen(line);
    (void)msh_write_all(fd, line, len);
    (void)msh_write_all(fd, "\n", 1);
    close(fd);
    free(path);
}

void history_init(shell_state_t *sh)
{
    char *path = history_file();
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    if (path == NULL)
        return;
    fp = fopen(path, "r");
    free(path);
    if (fp == NULL)
        return;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n > 0)
            history_add(sh, line);
    }
    free(line);
    fclose(fp);
}

void history_print(shell_state_t *sh)
{
    int i;

    for (i = 0; i < sh->history_count; ++i)
        printf("%5d  %s\n", i + 1, sh->history[i]);
}

void history_clear(shell_state_t *sh)
{
    char *path = history_file();

    history_free(sh);
    if (path != NULL) {
        (void)unlink(path);
        free(path);
    }
}

void history_free(shell_state_t *sh)
{
    int i;

    for (i = 0; i < sh->history_count; ++i)
        free(sh->history[i]);
    free(sh->history);
    sh->history = NULL;
    sh->history_count = 0;
    sh->history_cap = 0;
}

char *history_expand(const char *line, shell_state_t *sh)
{
    const char *p = line;
    const char *start;
    const char *end;
    char number[32];
    int index = -1;
    const char *replacement;
    char *result;
    size_t prefix, suffix;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p != '!')
        return msh_strdup(line);
    start = p;
    ++p;
    if (*p == '!') {
        end = ++p;
        if (sh->history_count == 0)
            goto missing;
        replacement = sh->history[sh->history_count - 1];
    } else if (*p == '-') {
        size_t len = 0;

        ++p;
        while (isdigit((unsigned char)*p) && len + 1 < sizeof(number))
            number[len++] = *p++;
        number[len] = '\0';
        if (len == 0)
            return msh_strdup(line);
        index = sh->history_count - atoi(number);
        end = p;
        if (index < 0 || index >= sh->history_count)
            goto missing;
        replacement = sh->history[index];
    } else if (isdigit((unsigned char)*p)) {
        size_t len = 0;

        while (isdigit((unsigned char)*p) && len + 1 < sizeof(number))
            number[len++] = *p++;
        number[len] = '\0';
        if (len == 0)
            return msh_strdup(line);
        index = atoi(number) - 1;
        end = p;
        if (index < 0 || index >= sh->history_count)
            goto missing;
        replacement = sh->history[index];
    } else {
        return msh_strdup(line);
    }

    prefix = (size_t)(start - line);
    suffix = strlen(end);
    result = msh_malloc(prefix + strlen(replacement) + suffix + 1);
    memcpy(result, line, prefix);
    strcpy(result + prefix, replacement);
    strcat(result, end);
    return result;

missing:
    msh_error("%s: event not found", start);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Helper strings                                                     */
/* ------------------------------------------------------------------ */

static void sl_add(strlist_t *sl, const char *text)
{
    int i;

    for (i = 0; i < sl->count; ++i)
        if (strcmp(sl->items[i], text) == 0)
            return;
    if (sl->count == sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 32;
        sl->items = msh_realloc(sl->items,
                                (size_t)sl->cap * sizeof(char *));
    }
    sl->items[sl->count++] = msh_strdup(text);
}

static void sl_free(strlist_t *sl)
{
    int i;

    for (i = 0; i < sl->count; ++i)
        free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

static int sl_cmp(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;

    return strcmp(*sa, *sb);
}

/* ------------------------------------------------------------------ */
/* UTF-8 helpers                                                      */
/* ------------------------------------------------------------------ */

static int utf8_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xe0) == 0xc0)
        return 2;
    if ((c & 0xf0) == 0xe0)
        return 3;
    if ((c & 0xf8) == 0xf0)
        return 4;
    return 1;
}

static size_t prev_char(const char *buf, size_t pos)
{
    if (pos == 0)
        return 0;
    --pos;
    while (pos > 0 && ((unsigned char)buf[pos] & 0xc0) == 0x80)
        --pos;
    return pos;
}

static size_t next_char(const char *buf, size_t len, size_t pos)
{
    int n;

    if (pos >= len)
        return len;
    n = utf8_len((unsigned char)buf[pos]);
    if (pos + (size_t)n > len)
        n = 1;
    return pos + (size_t)n;
}

static size_t display_width(const char *buf, size_t len)
{
    size_t i = 0;
    size_t width = 0;

    while (i < len) {
        unsigned char c = (unsigned char)buf[i];
        int n = utf8_len(c);
        wchar_t wc;
        mbstate_t state;

        if (n == 1) {
            width += 1;
            ++i;
            continue;
        }
        if (i + (size_t)n > len) {
            width += 1;
            ++i;
            continue;
        }
        memset(&state, 0, sizeof(state));
        if (mbrtowc(&wc, buf + i, (size_t)n, &state) == (size_t)n) {
            int w = wcwidth(wc);

            width += (w < 0 ? 1 : (size_t)w);
        } else {
            width += 1;
        }
        i += (size_t)n;
    }
    return width;
}

/* ------------------------------------------------------------------ */
/* Editing buffer                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t cursor;
} editbuf_t;

static void eb_init(editbuf_t *eb)
{
    memset(eb, 0, sizeof(*eb));
    eb->cap = 64;
    eb->buf = msh_calloc(eb->cap, 1);
}

static void eb_reserve(editbuf_t *eb, size_t extra)
{
    size_t need = eb->len + extra + 1;

    if (need <= eb->cap)
        return;
    while (eb->cap < need)
        eb->cap *= 2;
    eb->buf = msh_realloc(eb->buf, eb->cap);
}

static void eb_insert(editbuf_t *eb, const char *text, size_t n)
{
    eb_reserve(eb, n);
    memmove(eb->buf + eb->cursor + n, eb->buf + eb->cursor,
            eb->len - eb->cursor + 1);
    memcpy(eb->buf + eb->cursor, text, n);
    eb->len += n;
    eb->cursor += n;
}

static void eb_delete_range(editbuf_t *eb, size_t start, size_t end)
{
    if (start >= end)
        return;
    memmove(eb->buf + start, eb->buf + end, eb->len - end + 1);
    eb->len -= end - start;
    if (eb->cursor > end)
        eb->cursor -= end - start;
    else if (eb->cursor > start)
        eb->cursor = start;
}

static void eb_set(editbuf_t *eb, const char *text)
{
    size_t len = strlen(text);

    eb_reserve(eb, len);
    memcpy(eb->buf, text, len + 1);
    eb->len = len;
    eb->cursor = len;
}

/* ------------------------------------------------------------------ */
/* Terminal drawing                                                   */
/* ------------------------------------------------------------------ */

static void draw_line(const char *prompt, const editbuf_t *eb)
{
    size_t tail_width;

    fputs("\r\033[2K", stdout);
    fputs(prompt, stdout);
    fwrite(eb->buf, 1, eb->len, stdout);
    tail_width = display_width(eb->buf + eb->cursor, eb->len - eb->cursor);
    if (tail_width > 0)
        printf("\033[%zuD", tail_width);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Keys                                                               */
/* ------------------------------------------------------------------ */

enum {
    KEY_EOF = -2,
    KEY_INTR = -3,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_TAB,
    KEY_CTRL_R
};

static int read_byte(void)
{
    unsigned char c;

    for (;;) {
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n == 1)
            return (int)c;
        if (n < 0 && errno == EINTR)
            return KEY_INTR;
        if (n == 0)
            return KEY_EOF;
        if (n < 0)
            return KEY_EOF;
    }
}

static int read_escape(void)
{
    struct pollfd pfd;
    unsigned char c;
    int n;
    int c2;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 40) <= 0)
        return 27;
    n = (int)read(STDIN_FILENO, &c, 1);
    if (n != 1)
        return 27;
    if (c != '[' && c != 'O')
        return 27;

    c2 = read_byte();
    if (c2 < 0)
        return c2;
    if (c2 == 'A')
        return KEY_UP;
    if (c2 == 'B')
        return KEY_DOWN;
    if (c2 == 'C')
        return KEY_RIGHT;
    if (c2 == 'D')
        return KEY_LEFT;
    if (c2 == 'H')
        return KEY_HOME;
    if (c2 == 'F')
        return KEY_END;
    if (c2 == '1' || c2 == '7') {
        int c3 = read_byte();
        if (c3 == '~')
            return KEY_HOME;
        return 27;
    }
    if (c2 == '4' || c2 == '8') {
        int c3 = read_byte();
        if (c3 == '~')
            return KEY_END;
        return 27;
    }
    if (c2 == '3') {
        int c3 = read_byte();
        if (c3 == '~')
            return KEY_DELETE;
    }
    return 27;
}

static int read_key(void)
{
    int c = read_byte();

    if (c < 0)
        return c;
    if (c == 27)
        return read_escape();
    if (c == 1)
        return KEY_HOME;            /* Ctrl-A */
    if (c == 5)
        return KEY_END;             /* Ctrl-E */
    if (c == 9)
        return KEY_TAB;
    if (c == 18)
        return KEY_CTRL_R;          /* Ctrl-R */
    if (c == 127 || c == 8)
        return 8;                   /* backspace */
    return c;
}

/* ------------------------------------------------------------------ */
/* Completion                                                          */
/* ------------------------------------------------------------------ */

static void add_path_commands(strlist_t *sl, const char *prefix)
{
    const char *path = getenv("PATH");
    char *copy;
    char *save = NULL;
    char *dir;

    if (path == NULL)
        return;
    copy = msh_strdup(path);
    dir = strtok_r(copy, ":", &save);
    while (dir != NULL) {
        DIR *dp = opendir(dir[0] ? dir : ".");
        struct dirent *entry;

        if (dp != NULL) {
            while ((entry = readdir(dp)) != NULL) {
                char *full;

                if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
                    continue;
                if (entry->d_name[0] == '.')
                    continue;
                full = msh_join_path(dir[0] ? dir : ".", entry->d_name);
                if (access(full, X_OK) == 0)
                    sl_add(sl, entry->d_name);
                free(full);
            }
            closedir(dp);
        }
        dir = strtok_r(NULL, ":", &save);
    }
    free(copy);
}

static void add_file_commands(strlist_t *sl, const char *word)
{
    const char *slash = strrchr(word, '/');
    char *dir;
    const char *base;

    if (slash != NULL) {
        dir = msh_strndup(word, (size_t)(slash - word) + 1);
        base = slash + 1;
    } else {
        dir = msh_strdup("./");
        base = word;
    }

    {
        DIR *dp = opendir(dir[0] ? dir : ".");
        struct dirent *entry;

        if (dp != NULL) {
            while ((entry = readdir(dp)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 ||
                    strcmp(entry->d_name, "..") == 0)
                    continue;
                if (strncmp(entry->d_name, base, strlen(base)) != 0)
                    continue;
                {
                    char *full = msh_join_path(dir, entry->d_name);
                    struct stat st;
                    char *name;

                    name = msh_malloc(strlen(entry->d_name) + 2);
                    strcpy(name, entry->d_name);
                    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                        strcat(name, "/");
                    if (slash != NULL) {
                        char *withdir = msh_malloc(strlen(dir) + strlen(name) + 1);
                        strcpy(withdir, dir);
                        strcat(withdir, name);
                        sl_add(sl, withdir);
                        free(withdir);
                    } else {
                        sl_add(sl, name);
                    }
                    free(name);
                    free(full);
                }
            }
            closedir(dp);
        }
    }
    free(dir);
}

static void complete_word(editbuf_t *eb, const char *prompt)
{
    size_t start = eb->cursor;
    size_t i;
    char *word;
    strlist_t sl = { 0 };
    int count = 0;
    int first_word;
    int j;

    while (start > 0 && !isspace((unsigned char)eb->buf[start - 1]))
        --start;
    word = msh_strndup(eb->buf + start, eb->cursor - start);
    first_word = 1;
    for (i = 0; i < start; ++i)
        if (!isspace((unsigned char)eb->buf[i]))
            first_word = 0;

    if (first_word && strchr(word, '/') == NULL) {
        const char *const *names = msh_builtin_names(&count);

        for (j = 0; j < count; ++j)
            if (strncmp(names[j], word, strlen(word)) == 0)
                sl_add(&sl, names[j]);
        add_path_commands(&sl, word);
    } else {
        add_file_commands(&sl, word);
    }

    if (sl.count > 0) {
        size_t common;

        qsort(sl.items, (size_t)sl.count, sizeof(char *), sl_cmp);
        common = strlen(sl.items[0]);
        for (j = 1; j < sl.count; ++j) {
            size_t k = 0;

            while (k < common && sl.items[j][k] != '\0' &&
                   sl.items[0][k] == sl.items[j][k])
                ++k;
            common = k;
        }
        if (sl.count == 1) {
            const char *item = sl.items[0];
            if (strlen(item) > strlen(word))
                eb_insert(eb, item + strlen(word), strlen(item) - strlen(word));
            if (item[strlen(item) - 1] != '/')
                eb_insert(eb, " ", 1);
        } else if (common > strlen(word)) {
            char *addition = msh_malloc(common - strlen(word) + 1);
            memcpy(addition, sl.items[0] + strlen(word),
                   common - strlen(word));
            addition[common - strlen(word)] = '\0';
            eb_insert(eb, addition, strlen(addition));
            free(addition);
        } else {
            fputc('\n', stdout);
            for (j = 0; j < sl.count; ++j)
                printf("%s\n", sl.items[j]);
        }
    }
    free(word);
    sl_free(&sl);
    draw_line(prompt, eb);
}

/* ------------------------------------------------------------------ */
/* Ctrl-R search                                                      */
/* ------------------------------------------------------------------ */

static int reverse_search(shell_state_t *sh, editbuf_t *eb, const char *prompt)
{
    char query[256] = "";
    size_t qlen = 0;
    int found = sh->history_count;
    char *match = NULL;
    char saved[4096];
    size_t saved_len = eb->len;

    if (saved_len >= sizeof(saved))
        saved_len = sizeof(saved) - 1;
    memcpy(saved, eb->buf, saved_len);
    saved[saved_len] = '\0';

    for (;;) {
        int i;
        int key;

        if (found >= sh->history_count)
            found = sh->history_count;
        match = NULL;
        for (i = found - 1; i >= 0; --i) {
            if (strstr(sh->history[i], query) != NULL) {
                found = i;
                match = sh->history[i];
                break;
            }
        }
        fputs("\r\033[2K", stdout);
        printf("(reverse-i-search)`%s': %s", query,
               match != NULL ? match : "");
        fflush(stdout);

        key = read_key();
        if (key == KEY_INTR || key == 27) {
            eb_set(eb, saved);
            draw_line(prompt, eb);
            return 0;
        }
        if (key == '\r' || key == '\n') {
            if (match != NULL)
                eb_set(eb, match);
            else
                eb_set(eb, saved);
            draw_line(prompt, eb);
            return 0;
        }
        if (key == 127 || key == 8) {
            if (qlen > 0)
                query[--qlen] = '\0';
            found = sh->history_count;
        } else if (key == KEY_CTRL_R) {
            if (found > 0)
                --found;
        } else if (key >= 32 && key < 127 && qlen + 1 < sizeof(query)) {
            query[qlen++] = (char)key;
            query[qlen] = '\0';
            found = sh->history_count;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Read a line                                                        */
/* ------------------------------------------------------------------ */

char *lineedit_read(const char *prompt, shell_state_t *sh)
{
    editbuf_t eb;
    struct termios cooked;
    struct termios raw;
    int hist_index = sh->history_count;
    char *saved_current = msh_strdup("");

    eb_init(&eb);
    if (tcgetattr(STDIN_FILENO, &cooked) < 0) {
        /* Should not happen in the interactive path. */
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;

        fputs(prompt, stdout);
        fflush(stdout);
        n = getline(&line, &cap, stdin);
        if (n < 0) {
            free(line);
            return NULL;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        return line;
    }

    raw = cooked;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    draw_line(prompt, &eb);

    for (;;) {
        int key = read_key();

        if (key == KEY_INTR) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
            free(saved_current);
            free(eb.buf);
            return msh_strdup("");
        }
        if (key == KEY_EOF) {
            if (eb.len == 0) {
                (void)tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
                fputs("\r\n", stdout);
                fflush(stdout);
                free(saved_current);
                free(eb.buf);
                return NULL;
            }
            if (eb.cursor < eb.len) {
                size_t end = next_char(eb.buf, eb.len, eb.cursor);
                eb_delete_range(&eb, eb.cursor, end);
            }
            draw_line(prompt, &eb);
            continue;
        }
        if (key == 4) {             /* Ctrl-D */
            if (eb.len == 0) {
                (void)tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
                fputs("\r\n", stdout);
                fflush(stdout);
                free(saved_current);
                free(eb.buf);
                return NULL;
            }
            if (eb.cursor < eb.len) {
                size_t end = next_char(eb.buf, eb.len, eb.cursor);
                eb_delete_range(&eb, eb.cursor, end);
            }
            draw_line(prompt, &eb);
            continue;
        }
        if (key == '\r' || key == '\n') {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &cooked);
            fputs("\r\n", stdout);
            fflush(stdout);
            free(saved_current);
            {
                char *result = msh_strdup(eb.buf);
                free(eb.buf);
                return result;
            }
        }
        if (key == 127 || key == 8) {
            if (eb.cursor > 0) {
                size_t start = prev_char(eb.buf, eb.cursor);
                eb_delete_range(&eb, start, eb.cursor);
            }
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_DELETE) {
            if (eb.cursor < eb.len) {
                size_t end = next_char(eb.buf, eb.len, eb.cursor);
                eb_delete_range(&eb, eb.cursor, end);
            }
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_LEFT) {
            if (eb.cursor > 0)
                eb.cursor = prev_char(eb.buf, eb.cursor);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_RIGHT) {
            if (eb.cursor < eb.len)
                eb.cursor = next_char(eb.buf, eb.len, eb.cursor);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_HOME) {
            eb.cursor = 0;
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_END) {
            eb.cursor = eb.len;
            draw_line(prompt, &eb);
            continue;
        }
        if (key == 21) {            /* Ctrl-U */
            eb_delete_range(&eb, 0, eb.cursor);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == 11) {            /* Ctrl-K */
            eb_delete_range(&eb, eb.cursor, eb.len);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == 23) {            /* Ctrl-W */
            size_t end = eb.cursor;
            size_t start = end;

            while (start > 0 && isspace((unsigned char)eb.buf[start - 1]))
                --start;
            while (start > 0 && !isspace((unsigned char)eb.buf[start - 1]))
                --start;
            eb_delete_range(&eb, start, end);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && hist_index == sh->history_count) {
                free(saved_current);
                saved_current = msh_strdup(eb.buf);
            }
            if (key == KEY_UP && hist_index > 0)
                --hist_index;
            else if (key == KEY_DOWN && hist_index < sh->history_count)
                ++hist_index;
            eb_set(&eb, hist_index == sh->history_count
                            ? saved_current
                            : sh->history[hist_index]);
            draw_line(prompt, &eb);
            continue;
        }
        if (key == KEY_TAB) {
            complete_word(&eb, prompt);
            continue;
        }
        if (key == KEY_CTRL_R) {
            (void)reverse_search(sh, &eb, prompt);
            hist_index = sh->history_count;
            continue;
        }
        if (key >= 32 && key < 256) {
            char bytes[5];
            int n = utf8_len((unsigned char)key);

            bytes[0] = (char)key;
            if (n > 1) {
                int i;

                for (i = 1; i < n; ++i) {
                    int next = read_byte();
                    if (next < 0)
                        break;
                    bytes[i] = (char)next;
                }
                if (i != n)
                    n = 1;
            }
            eb_insert(&eb, bytes, (size_t)n);
            hist_index = sh->history_count;
            draw_line(prompt, &eb);
            continue;
        }
        draw_line(prompt, &eb);
    }
}
