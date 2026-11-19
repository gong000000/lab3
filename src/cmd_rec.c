/*
 * cmd_rec.c - session recording, replay, and standalone HTML export.
 *
 * rec start [-x] FILE   start recording at the interactive REPL
 * rec stop              stop and close the recording
 * rec status            show the current recording state
 * rec export FILE HTML  export a standalone timeline
 * replay FILE [-s N]    replay captured output at N times speed
 *
 * Each REPL command is executed with stdout/stderr temporarily redirected
 * to temporary files.  After the command returns, the output is copied to
 * the real terminal and appended to the record, together with the exit
 * status and elapsed milliseconds.  Output is therefore shown when the
 * command completes, not while it is running; interactive full-screen
 * programs (dbg, editors) should not be recorded.
 */
#include "myshell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REC_MAGIC "MSHREC1"
#define REC_MAX_BLOCK (64u * 1024u * 1024u)

typedef struct {
    char *cmd;
    char *out;
    char *err;
    char *diag;
    int status;
    long long ms;
} rec_item_t;

typedef struct {
    int active;
    int stop_pending;
    int capturing;
    int explain;
    FILE *fp;
    char *path;
    int count;
} rec_state_t;

static rec_state_t g_rec;

/* ------------------------------------------------------------------ */
/* Record format                                                      */
/* ------------------------------------------------------------------ */

static void rec_write_block(FILE *fp, char tag, const char *data, size_t len)
{
    fprintf(fp, "%c %zu\n", tag, len);
    if (len > 0)
        fwrite(data, 1, len, fp);
    fputc('\n', fp);
}

static int rec_read_line(FILE *fp, char *tag, size_t *len)
{
    char line[128];
    char fmt[16];

    if (fgets(line, sizeof(line), fp) == NULL)
        return 0;
    snprintf(fmt, sizeof(fmt), "%%c %%zu");
    if (sscanf(line, fmt, tag, len) != 2)
        return -1;
    return 1;
}

static char *rec_read_block(FILE *fp, size_t len)
{
    char *data;
    int ch;

    if (len > REC_MAX_BLOCK) {
        errno = EFBIG;
        return NULL;
    }
    data = msh_malloc(len + 1);
    if (len > 0 && fread(data, 1, len, fp) != len) {
        free(data);
        errno = EIO;
        return NULL;
    }
    data[len] = '\0';
    ch = fgetc(fp);
    if (ch != '\n' && ch != EOF) {
        free(data);
        errno = EIO;
        return NULL;
    }
    return data;
}

static int rec_read_status(FILE *fp, int *status, long long *ms)
{
    char line[128];

    if (fgets(line, sizeof(line), fp) == NULL ||
        sscanf(line, "S %d", status) != 1)
        return -1;
    if (fgets(line, sizeof(line), fp) == NULL ||
        sscanf(line, "T %lld", ms) != 1)
        return -1;
    return 0;
}

static void rec_item_free(rec_item_t *item)
{
    free(item->cmd);
    free(item->out);
    free(item->err);
    free(item->diag);
    memset(item, 0, sizeof(*item));
}

static int rec_read_all(const char *path, rec_item_t **items_out, int *count_out)
{
    FILE *fp = fopen(path, "rb");
    char magic[32];
    rec_item_t *items = NULL;
    int count = 0;
    int cap = 0;

    if (fp == NULL)
        return -1;
    if (fgets(magic, sizeof(magic), fp) == NULL ||
        strncmp(magic, REC_MAGIC, strlen(REC_MAGIC)) != 0) {
        fclose(fp);
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        char tag;
        size_t len;
        rec_item_t item;
        int rc;

        memset(&item, 0, sizeof(item));
        rc = rec_read_line(fp, &tag, &len);
        if (rc == 0)
            break;
        if (rc < 0 || tag != 'C')
            goto fail;
        item.cmd = rec_read_block(fp, len);
        if (item.cmd == NULL)
            goto fail;

        if (rec_read_line(fp, &tag, &len) != 1 || tag != 'O')
            goto fail;
        item.out = rec_read_block(fp, len);
        if (item.out == NULL)
            goto fail;

        if (rec_read_line(fp, &tag, &len) != 1 || tag != 'E')
            goto fail;
        item.err = rec_read_block(fp, len);
        if (item.err == NULL)
            goto fail;

        if (rec_read_status(fp, &item.status, &item.ms) < 0)
            goto fail;

        if (rec_read_line(fp, &tag, &len) != 1 || tag != 'X')
            goto fail;
        item.diag = rec_read_block(fp, len);
        if (item.diag == NULL)
            goto fail;

        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            items = msh_realloc(items, (size_t)cap * sizeof(*items));
        }
        items[count++] = item;
        continue;

fail:
        rec_item_free(&item);
        {
            int i;

            for (i = 0; i < count; ++i)
                rec_item_free(&items[i]);
        }
        free(items);
        fclose(fp);
        errno = EINVAL;
        return -1;
    }
    fclose(fp);
    *items_out = items;
    *count_out = count;
    return 0;
}

static int rec_append(const char *cmd, const char *out, const char *err,
                      const char *diag, int status, long long ms)
{
    if (g_rec.fp == NULL)
        return -1;
    rec_write_block(g_rec.fp, 'C', cmd, strlen(cmd));
    rec_write_block(g_rec.fp, 'O', out, strlen(out));
    rec_write_block(g_rec.fp, 'E', err, strlen(err));
    fprintf(g_rec.fp, "S %d\nT %lld\n", status, ms);
    rec_write_block(g_rec.fp, 'X', diag != NULL ? diag : "",
                    diag != NULL ? strlen(diag) : 0);
    fflush(g_rec.fp);
    ++g_rec.count;
    return 0;
}

static void rec_finalize(void)
{
    if (g_rec.fp != NULL) {
        fflush(g_rec.fp);
        fclose(g_rec.fp);
    }
    g_rec.fp = NULL;
    g_rec.active = 0;
    g_rec.stop_pending = 0;
    g_rec.capturing = 0;
    free(g_rec.path);
    g_rec.path = NULL;
}

void rec_shutdown(void)
{
    rec_finalize();
}

int rec_is_active(void)
{
    return g_rec.active;
}

/* ------------------------------------------------------------------ */
/* Output capture                                                     */
/* ------------------------------------------------------------------ */

static char *rec_read_stream(FILE *fp, size_t *len_out)
{
    long size;
    char *data;

    fflush(fp);
    if (fseek(fp, 0, SEEK_END) < 0)
        return NULL;
    size = ftell(fp);
    if (size < 0)
        return NULL;
    if (fseek(fp, 0, SEEK_SET) < 0)
        return NULL;
    data = msh_malloc((size_t)size + 1);
    if (size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        return NULL;
    }
    data[size] = '\0';
    *len_out = (size_t)size;
    return data;
}

static int rec_capture_stdio(FILE **out_tmp, FILE **err_tmp,
                             int *saved_out, int *saved_err)
{
    *out_tmp = tmpfile();
    *err_tmp = tmpfile();
    if (*out_tmp == NULL || *err_tmp == NULL)
        goto fail;
    *saved_out = dup(STDOUT_FILENO);
    *saved_err = dup(STDERR_FILENO);
    if (*saved_out < 0 || *saved_err < 0)
        goto fail;
    fflush(stdout);
    fflush(stderr);
    if (dup2(fileno(*out_tmp), STDOUT_FILENO) < 0 ||
        dup2(fileno(*err_tmp), STDERR_FILENO) < 0)
        goto fail;
    return 0;

fail:
    if (*saved_out >= 0)
        (void)dup2(*saved_out, STDOUT_FILENO);
    if (*saved_err >= 0)
        (void)dup2(*saved_err, STDERR_FILENO);
    if (*saved_out >= 0) {
        close(*saved_out);
        *saved_out = -1;
    }
    if (*saved_err >= 0) {
        close(*saved_err);
        *saved_err = -1;
    }
    if (*out_tmp != NULL) {
        fclose(*out_tmp);
        *out_tmp = NULL;
    }
    if (*err_tmp != NULL) {
        fclose(*err_tmp);
        *err_tmp = NULL;
    }
    return -1;
}

static void rec_restore_stdio(int saved_out, int saved_err)
{
    fflush(stdout);
    fflush(stderr);
    if (saved_out >= 0) {
        (void)dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    if (saved_err >= 0) {
        (void)dup2(saved_err, STDERR_FILENO);
        close(saved_err);
    }
}

/* ------------------------------------------------------------------ */
/* -x diagnostics                                                     */
/* ------------------------------------------------------------------ */

static int rec_is_rec_command(const char *line)
{
    const char *p = line;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (strncmp(p, "rec", 3) == 0 &&
        (p[3] == '\0' || isspace((unsigned char)p[3])))
        return 1;
    if (strncmp(p, "replay", 6) == 0 &&
        (p[6] == '\0' || isspace((unsigned char)p[6])))
        return 1;
    return 0;
}

static char *rec_capture_explain(const char *line, shell_state_t *sh)
{
    FILE *tmp;
    int saved;
    char *argv[3];
    char *text;
    size_t len;

    tmp = tmpfile();
    if (tmp == NULL)
        return NULL;
    saved = dup(STDOUT_FILENO);
    if (saved < 0) {
        fclose(tmp);
        return NULL;
    }
    fflush(stdout);
    if (dup2(fileno(tmp), STDOUT_FILENO) < 0) {
        close(saved);
        fclose(tmp);
        return NULL;
    }
    argv[0] = "explain";
    argv[1] = (char *)line;
    argv[2] = NULL;
    (void)cmd_explain(2, argv, sh);
    fflush(stdout);
    (void)dup2(saved, STDOUT_FILENO);
    close(saved);
    text = rec_read_stream(tmp, &len);
    fclose(tmp);
    return text;
}

static char *rec_capture_trace(const char *line, shell_state_t *sh)
{
    FILE *tmp;
    int saved;
    char *trace_argv[9];
    char *text;
    char *summary;
    char *last = NULL;
    char *p;
    size_t len;

    tmp = tmpfile();
    if (tmp == NULL)
        return NULL;
    saved = dup(STDOUT_FILENO);
    if (saved < 0) {
        fclose(tmp);
        return NULL;
    }
    fflush(stdout);
    if (dup2(fileno(tmp), STDOUT_FILENO) < 0) {
        close(saved);
        fclose(tmp);
        return NULL;
    }
    trace_argv[0] = "trace";
    trace_argv[1] = "-c";
    trace_argv[2] = "-f";
    trace_argv[3] = "-e";
    trace_argv[4] =
        "openat,read,write,execve,exit_group,clone,vfork,fork,newfstatat,mmap,brk";
    trace_argv[5] = "/bin/sh";
    trace_argv[6] = "-c";
    trace_argv[7] = (char *)line;
    trace_argv[8] = NULL;
    (void)cmd_trace(8, trace_argv, sh);
    fflush(stdout);
    (void)dup2(saved, STDOUT_FILENO);
    close(saved);
    text = rec_read_stream(tmp, &len);
    fclose(tmp);
    if (text == NULL)
        return NULL;

    /* Keep only the summary table, not the per-call trace lines. */
    p = text;
    while ((p = strstr(p, "   calls")) != NULL) {
        last = p;
        ++p;
    }
    summary = msh_strdup(last != NULL ? last : text);
    free(text);
    return summary;
}

static char *rec_build_diag(const char *line, shell_state_t *sh)
{
    char *explain;
    char *trace;
    char *diag;
    size_t need;

    if (rec_is_rec_command(line))
        return msh_strdup("");
    explain = rec_capture_explain(line, sh);
    trace = rec_capture_trace(line, sh);
    if (explain == NULL)
        explain = msh_strdup("");
    if (trace == NULL)
        trace = msh_strdup("");
    need = strlen(explain) + strlen(trace) + 64;
    diag = msh_malloc(need);
    snprintf(diag, need, "explain:\n%s\n\ntrace -c summary:\n%s", explain,
             trace);
    free(explain);
    free(trace);
    return diag;
}

/* ------------------------------------------------------------------ */
/* Execute and record one REPL command                                */
/* ------------------------------------------------------------------ */

int rec_run_and_record(const char *line, shell_state_t *sh)
{
    FILE *out_tmp = NULL;
    FILE *err_tmp = NULL;
    int saved_out = -1;
    int saved_err = -1;
    char *out = NULL;
    char *err = NULL;
    char *diag = NULL;
    size_t out_len = 0;
    size_t err_len = 0;
    struct timespec start;
    struct timespec end;
    long long ms;
    int status;
    int captured = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    if (rec_capture_stdio(&out_tmp, &err_tmp, &saved_out, &saved_err) == 0) {
        g_rec.capturing = 1;
        status = shell_execute_line(line, sh);
        g_rec.capturing = 0;
        rec_restore_stdio(saved_out, saved_err);
        out = rec_read_stream(out_tmp, &out_len);
        err = rec_read_stream(err_tmp, &err_len);
        fclose(out_tmp);
        fclose(err_tmp);
        captured = 1;
    } else {
        status = shell_execute_line(line, sh);
        out = msh_strdup("");
        err = msh_strdup("");
        out_len = err_len = 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    ms = (long long)(end.tv_sec - start.tv_sec) * 1000 +
         (long long)(end.tv_nsec - start.tv_nsec) / 1000000;

    if (out == NULL) {
        out = msh_strdup("");
        out_len = 0;
    }
    if (err == NULL) {
        err = msh_strdup("");
        err_len = 0;
    }
    if (out_len > 0)
        fwrite(out, 1, out_len, stdout);
    if (err_len > 0)
        fwrite(err, 1, err_len, stderr);
    fflush(stdout);
    fflush(stderr);

    if (g_rec.explain && captured)
        diag = rec_build_diag(line, sh);
    else
        diag = msh_strdup("");

    rec_append(line, out, err, diag, status, ms);
    free(out);
    free(err);
    free(diag);
    if (g_rec.stop_pending)
        rec_finalize();
    return status;
}

/* ------------------------------------------------------------------ */
/* Replay / export                                                    */
/* ------------------------------------------------------------------ */

static void rec_sleep_ms(long long ms, double speed)
{
    struct timespec req;

    if (speed <= 0 || ms <= 0)
        return;
    ms = (long long)((double)ms / speed);
    if (ms > 10000)
        ms = 10000;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) < 0 && errno == EINTR)
        ;
}

static void rec_print_text(FILE *fp, const char *text)
{
    if (text != NULL && text[0] != '\0') {
        fputs(text, fp);
        if (text[strlen(text) - 1] != '\n')
            fputc('\n', fp);
    }
}

static int rec_export_html(const char *record, const char *output)
{
    rec_item_t *items = NULL;
    int count = 0;
    int i;
    FILE *fp;

    if (rec_read_all(record, &items, &count) < 0)
        return -1;
    fp = fopen(output, "w");
    if (fp == NULL) {
        for (i = 0; i < count; ++i)
            rec_item_free(&items[i]);
        free(items);
        return -1;
    }
    fputs("<!doctype html>\n<html lang=\"ko\"><head><meta charset=\"utf-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
          "<title>myshell session recording</title>\n"
          "<style>body{font-family:system-ui,sans-serif;max-width:960px;"
          "margin:2rem auto;padding:0 1rem;background:#111;color:#ddd}"
          "h1{color:#8fd3ff}.entry{border:1px solid #333;border-radius:8px;"
          "margin:1.2rem 0;padding:1rem;background:#191919}"
          ".cmd{font-family:Consolas,monospace;color:#9ef;white-space:pre-wrap}"
          ".meta{font-size:.85rem;color:#999;margin:.5rem 0}.out,.err,.diag{"
          "font-family:Consolas,monospace;white-space:pre-wrap;background:#0c0c0c;"
          "padding:.7rem;border-radius:6px;overflow:auto}.err{color:#ff9b9b}"
          ".diag{color:#d7c48a}details{margin-top:.6rem}</style></head><body>\n",
          fp);
    fprintf(fp, "<h1>myshell session recording</h1>\n"
                "<p>%d command(s) · %s</p>\n",
            count, record);
    for (i = 0; i < count; ++i) {
        rec_item_t *it = &items[i];
        char *escaped;

        fprintf(fp, "<div class=\"entry\"><div class=\"cmd\">$ ");
        escaped = msh_malloc(strlen(it->cmd) * 6 + 1);
        {
            size_t k = 0;
            const unsigned char *p = (const unsigned char *)it->cmd;

            while (*p != '\0') {
                if (*p == '&') { memcpy(escaped + k, "&amp;", 5); k += 5; }
                else if (*p == '<') { memcpy(escaped + k, "&lt;", 4); k += 4; }
                else if (*p == '>') { memcpy(escaped + k, "&gt;", 4); k += 4; }
                else if (*p == '"') { memcpy(escaped + k, "&quot;", 6); k += 6; }
                else escaped[k++] = (char)*p;
                ++p;
            }
            escaped[k] = '\0';
        }
        fputs(escaped, fp);
        free(escaped);
        fprintf(fp, "</div><div class=\"meta\">status=%d · %lld ms</div>\n",
                it->status, it->ms);
        if (it->out[0] != '\0') {
            fprintf(fp, "<pre class=\"out\">");
            {
                const unsigned char *p = (const unsigned char *)it->out;

                while (*p != '\0') {
                    if (*p == '&') fputs("&amp;", fp);
                    else if (*p == '<') fputs("&lt;", fp);
                    else if (*p == '>') fputs("&gt;", fp);
                    else fputc(*p, fp);
                    ++p;
                }
            }
            fputs("</pre>\n", fp);
        }
        if (it->err[0] != '\0') {
            fprintf(fp, "<pre class=\"err\">");
            {
                const unsigned char *p = (const unsigned char *)it->err;

                while (*p != '\0') {
                    if (*p == '&') fputs("&amp;", fp);
                    else if (*p == '<') fputs("&lt;", fp);
                    else if (*p == '>') fputs("&gt;", fp);
                    else fputc(*p, fp);
                    ++p;
                }
            }
            fputs("</pre>\n", fp);
        }
        if (it->diag[0] != '\0') {
            fputs("<details><summary>explain / trace</summary>"
                  "<pre class=\"diag\">", fp);
            {
                const unsigned char *p = (const unsigned char *)it->diag;

                while (*p != '\0') {
                    if (*p == '&') fputs("&amp;", fp);
                    else if (*p == '<') fputs("&lt;", fp);
                    else if (*p == '>') fputs("&gt;", fp);
                    else fputc(*p, fp);
                    ++p;
                }
            }
            fputs("</pre></details>\n", fp);
        }
        fputs("</div>\n", fp);
    }
    fputs("</body></html>\n", fp);
    fclose(fp);
    for (i = 0; i < count; ++i)
        rec_item_free(&items[i]);
    free(items);
    return 0;
}

int cmd_replay(int argc, char **argv, shell_state_t *sh)
{
    const char *path = NULL;
    double speed = 1.0;
    int show_diag = 0;
    int i = 1;
    rec_item_t *items = NULL;
    int count = 0;

    (void)sh;
    while (i < argc) {
        if (strcmp(argv[i], "-s") == 0) {
            if (++i >= argc) {
                msh_usage("replay FILE [-s SPEED]");
                return 2;
            }
            speed = atof(argv[i]);
            if (speed < 0)
                speed = 0;
            if (speed > 1000)
                speed = 1000;
        } else if (strncmp(argv[i], "-s", 2) == 0) {
            speed = atof(argv[i] + 2);
        } else if (strcmp(argv[i], "-x") == 0) {
            show_diag = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            msh_usage("replay FILE [-s SPEED]");
            return 2;
        } else {
            path = argv[i];
        }
        ++i;
    }
    if (path == NULL) {
        msh_usage("replay FILE [-s SPEED]");
        return 2;
    }
    if (rec_read_all(path, &items, &count) < 0) {
        msh_error("replay: %s: %s", path, strerror(errno));
        return 1;
    }
    printf("=== replay: %s (%d commands, speed %.2fx) ===\n", path, count,
           speed);
    for (i = 0; i < count; ++i) {
        rec_item_t *it = &items[i];

        rec_sleep_ms(it->ms, speed);
        printf("$ %s\n", it->cmd);
        rec_print_text(stdout, it->out);
        rec_print_text(stderr, it->err);
        printf("[status=%d, %lldms]\n", it->status, it->ms);
        if (show_diag)
            rec_print_text(stdout, it->diag);
        fflush(stdout);
        fflush(stderr);
    }
    for (i = 0; i < count; ++i)
        rec_item_free(&items[i]);
    free(items);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Builtin entry point                                                */
/* ------------------------------------------------------------------ */

static int rec_start(const char *path, int explain)
{
    FILE *fp;

    if (g_rec.active) {
        msh_error("rec: already recording to %s", g_rec.path);
        return 1;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        msh_error("rec: %s: %s", path, strerror(errno));
        return 1;
    }
    fprintf(fp, "%s\n", REC_MAGIC);
    fflush(fp);
    g_rec.fp = fp;
    g_rec.path = msh_strdup(path);
    g_rec.active = 1;
    g_rec.stop_pending = 0;
    g_rec.capturing = 0;
    g_rec.explain = explain;
    g_rec.count = 0;
    printf("recording to %s%s\n", path, explain ? " (diagnostics on)" : "");
    return 0;
}

static int rec_stop(void)
{
    if (!g_rec.active) {
        msh_error("rec: not recording");
        return 1;
    }
    printf("recording stopped: %s (%d commands)\n", g_rec.path, g_rec.count);
    if (g_rec.capturing)
        g_rec.stop_pending = 1;
    else
        rec_finalize();
    return 0;
}

int cmd_rec(int argc, char **argv, shell_state_t *sh)
{
    (void)sh;
    if (argc < 2) {
        msh_usage("rec start [-x] FILE | rec stop | rec status | "
                  "rec export FILE HTML");
        return 2;
    }
    if (strcmp(argv[1], "start") == 0) {
        const char *path = NULL;
        int explain = 0;
        int i;

        for (i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-x") == 0)
                explain = 1;
            else if (path == NULL)
                path = argv[i];
            else {
                msh_usage("rec start [-x] FILE");
                return 2;
            }
        }
        if (path == NULL) {
            msh_usage("rec start [-x] FILE");
            return 2;
        }
        return rec_start(path, explain);
    }
    if (strcmp(argv[1], "stop") == 0) {
        if (argc != 2) {
            msh_usage("rec stop");
            return 2;
        }
        return rec_stop();
    }
    if (strcmp(argv[1], "status") == 0) {
        if (g_rec.active)
            printf("recording: %s (%d commands)\n", g_rec.path, g_rec.count);
        else
            printf("recording: off\n");
        return 0;
    }
    if (strcmp(argv[1], "export") == 0) {
        if (argc != 4) {
            msh_usage("rec export FILE HTML");
            return 2;
        }
        if (g_rec.active) {
            msh_error("rec: stop recording before exporting");
            return 1;
        }
        if (rec_export_html(argv[2], argv[3]) < 0) {
            msh_error("rec export: %s: %s", argv[2], strerror(errno));
            return 1;
        }
        printf("wrote %s\n", argv[3]);
        return 0;
    }
    msh_usage("rec start [-x] FILE | rec stop | rec status | "
              "rec export FILE HTML");
    return 2;
}
