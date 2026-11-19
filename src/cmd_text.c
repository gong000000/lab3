/*
 * cmd_text.c - built-in cat and grep
 */
#include "myshell.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* cat                                                                */

typedef struct {
    int number;
    int number_nonblank;
    int show_ends;
    int show_tabs;
} cat_options_t;

static int line_is_blank(const char *line, size_t len)
{
    size_t i;

    for (i = 0; i < len; ++i) {
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            return 0;
    }
    return 1;
}

static void cat_write_line(const char *line, size_t len,
                           const cat_options_t *opts, long *line_no)
{
    size_t i;
    int has_newline = len > 0 && line[len - 1] == '\n';
    size_t body = has_newline ? len - 1 : len;
    int blank = line_is_blank(line, body);

    if (opts->number && (!opts->number_nonblank || !blank))
        printf("%6ld\t", ++*line_no);

    for (i = 0; i < body; ++i) {
        if (opts->show_tabs && line[i] == '\t')
            fputs("^I", stdout);
        else
            putchar((unsigned char)line[i]);
    }
    if (opts->show_ends)
        putchar('$');
    if (has_newline)
        putchar('\n');
}

static int cat_stream(FILE *fp, const cat_options_t *opts, long *line_no)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int rc = 0;

    while ((n = getline(&line, &cap, fp)) >= 0) {
        cat_write_line(line, (size_t)n, opts, line_no);
    }
    if (ferror(fp)) {
        msh_error("cat: read error: %s", strerror(errno));
        rc = -1;
    }
    free(line);
    return rc;
}

int cmd_cat(int argc, char **argv)
{
    cat_options_t opts;
    int end_options = 0;
    int status = 0;
    int i;
    long line_no = 0;
    char **files;
    int nfiles = 0;

    memset(&opts, 0, sizeof(opts));
    files = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && arg[0] == '-' && arg[1] != '\0') {
            const char *p;

            for (p = arg + 1; *p != '\0'; ++p) {
                switch (*p) {
                case 'n': opts.number = 1; break;
                case 'b': opts.number = 1; opts.number_nonblank = 1; break;
                case 'E': opts.show_ends = 1; break;
                case 'T': opts.show_tabs = 1; break;
                case 'A':
                    opts.show_ends = 1;
                    opts.show_tabs = 1;
                    break;
                default:
                    msh_error("cat: invalid option -- '%c'", *p);
                    msh_usage("cat [-n] [-b] [-E] [-T] [file ...]");
                    free(files);
                    return 2;
                }
            }
            continue;
        }
        files[nfiles++] = argv[i];
    }

    if (nfiles == 0) {
        free(files);
        return cat_stream(stdin, &opts, &line_no) < 0 ? 1 : 0;
    }

    for (i = 0; i < nfiles; ++i) {
        const char *arg = files[i];
        FILE *fp;

        if (strcmp(arg, "-") == 0) {
            fp = stdin;
        } else {
            fp = fopen(arg, "rb");
            if (fp == NULL) {
                msh_perror(arg);
                status = 1;
                continue;
            }
        }
        if (cat_stream(fp, &opts, &line_no) < 0)
            status = 1;
        if (fp != stdin)
            fclose(fp);
    }

    free(files);
    return status;
}

/* grep                                                               */

typedef struct {
    int ignore_case;
    int invert;
    int line_number;
    int count;
    int list_files;
    int recursive;
    int extended;
    int fixed;
    int quiet;
    int silent;
    int no_filename;
    int force_filename;
} grep_options_t;

typedef struct {
    int any_match;
    int had_error;
    int stop_all;
} grep_state_t;

static int fixed_contains(const char *haystack, const char *needle,
                          int ignore_case)
{
    if (needle[0] == '\0')
        return 1;
    if (ignore_case)
        return strcasestr(haystack, needle) != NULL;
    return strstr(haystack, needle) != NULL;
}

static int line_matches(const char *line, const grep_options_t *opts,
                        const regex_t *re, const char *pattern)
{
    int matched;

    if (opts->fixed)
        matched = fixed_contains(line, pattern, opts->ignore_case);
    else
        matched = regexec(re, line, 0, NULL, 0) == 0;

    if (opts->invert)
        matched = !matched;
    return matched;
}

static void grep_print_prefix(const char *name, long line_no,
                              const grep_options_t *opts, int show_name)
{
    if (show_name && name != NULL)
        printf("%s:", name);
    if (opts->line_number)
        printf("%ld:", line_no);
}

static int grep_stream(FILE *fp, const char *name, const grep_options_t *opts,
                       const regex_t *re, const char *pattern,
                       grep_state_t *state, int show_name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    long line_no = 0;
    long matches = 0;
    int rc = 0;

    while ((n = getline(&line, &cap, fp)) >= 0) {
        size_t len = (size_t)n;
        int had_newline = len > 0 && line[len - 1] == '\n';
        int matched;

        ++line_no;
        if (had_newline)
            line[--len] = '\0';
        matched = line_matches(line, opts, re, pattern);

        if (matched) {
            ++matches;
            state->any_match = 1;
            if (opts->quiet) {
                state->stop_all = 1;
                break;
            }
            if (opts->list_files) {
                printf("%s\n", name != NULL ? name : "(standard input)");
                break;
            }
            if (!opts->count) {
                grep_print_prefix(name, line_no, opts, show_name);
                fwrite(line, 1, len, stdout);
                putchar('\n');
            }
        }
        if (opts->count)
            continue;
    }

    if (ferror(fp)) {
        if (!opts->silent)
            msh_error("%s: read error", name != NULL ? name : "stdin");
        state->had_error = 1;
        rc = -1;
    }

    if (opts->count && !opts->quiet && !opts->list_files) {
        if (show_name && name != NULL)
            printf("%s:", name);
        printf("%ld\n", matches);
    }

    free(line);
    return rc;
}

static int grep_file(const char *path, const grep_options_t *opts,
                     const regex_t *re, const char *pattern,
                     grep_state_t *state, int show_name)
{
    FILE *fp;
    int rc;

    if (strcmp(path, "-") == 0) {
        return grep_stream(stdin, "(standard input)", opts, re, pattern,
                           state, show_name);
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        if (!opts->silent)
            msh_perror(path);
        state->had_error = 1;
        return -1;
    }
    rc = grep_stream(fp, path, opts, re, pattern, state, show_name);
    fclose(fp);
    return rc;
}

static int grep_directory(const char *dir, const grep_options_t *opts,
                          const regex_t *re, const char *pattern,
                          grep_state_t *state, int show_name)
{
    DIR *dp;
    struct dirent *entry;
    int rc = 0;

    dp = opendir(dir);
    if (dp == NULL) {
        if (!opts->silent)
            msh_perror(dir);
        state->had_error = 1;
        return -1;
    }

    while ((entry = readdir(dp)) != NULL && !state->stop_all) {
        char *path;
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        path = msh_join_path(dir, entry->d_name);
        if (lstat(path, &st) < 0) {
            if (!opts->silent)
                msh_perror(path);
            state->had_error = 1;
            rc = -1;
        } else if (S_ISDIR(st.st_mode)) {
            if (grep_directory(path, opts, re, pattern, state,
                               show_name) < 0)
                rc = -1;
        } else if (S_ISREG(st.st_mode)) {
            if (grep_file(path, opts, re, pattern, state, show_name) < 0)
                rc = -1;
        }
        free(path);
    }
    closedir(dp);
    return rc;
}

static void grep_usage(void)
{
    msh_usage("grep [-i] [-v] [-n] [-c] [-l] [-r] [-E] [-F] [-q] "
              "[-h] [-s] pattern [file ...]");
}

int cmd_grep(int argc, char **argv)
{
    grep_options_t opts;
    grep_state_t state;
    regex_t re;
    regex_t *re_ptr = NULL;
    const char *pattern = NULL;
    char **files = NULL;
    int nfiles = 0;
    int end_options = 0;
    int i;
    int status;
    int show_name;

    memset(&opts, 0, sizeof(opts));
    memset(&state, 0, sizeof(state));
    files = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && arg[0] == '-' && arg[1] != '\0') {
            const char *p;

            if (strcmp(arg, "-e") == 0) {
                if (++i >= argc) {
                    msh_error("grep: option -e requires an argument");
                    free(files);
                    return 2;
                }
                pattern = argv[i];
                continue;
            }
            for (p = arg + 1; *p != '\0'; ++p) {
                switch (*p) {
                case 'i': opts.ignore_case = 1; break;
                case 'v': opts.invert = 1; break;
                case 'n': opts.line_number = 1; break;
                case 'c': opts.count = 1; break;
                case 'l': opts.list_files = 1; break;
                case 'r':
                case 'R': opts.recursive = 1; break;
                case 'E': opts.extended = 1; break;
                case 'F': opts.fixed = 1; break;
                case 'q': opts.quiet = 1; break;
                case 's': opts.silent = 1; break;
                case 'h': opts.no_filename = 1; break;
                case 'H': opts.force_filename = 1; break;
                default:
                    msh_error("grep: invalid option -- '%c'", *p);
                    grep_usage();
                    free(files);
                    return 2;
                }
            }
            continue;
        }
        if (pattern == NULL)
            pattern = arg;
        else
            files[nfiles++] = argv[i];
    }

    if (pattern == NULL) {
        msh_error("grep: no pattern given");
        grep_usage();
        free(files);
        return 2;
    }

    if (opts.recursive && nfiles == 0)
        files[nfiles++] = ".";

    if (!opts.fixed) {
        int flags = 0;
        if (opts.extended)
            flags |= REG_EXTENDED;
        if (opts.ignore_case)
            flags |= REG_ICASE;
        if (regcomp(&re, pattern, flags) != 0) {
            char errbuf[256];
            regerror(0, &re, errbuf, sizeof(errbuf));
            msh_error("grep: invalid regular expression: %s", errbuf);
            free(files);
            return 2;
        }
        re_ptr = &re;
    }

    show_name = opts.force_filename ||
                (!opts.no_filename && (nfiles > 1 || opts.recursive));

    if (nfiles == 0) {
        grep_stream(stdin, "(standard input)", &opts, re_ptr, pattern,
                    &state, opts.force_filename);
    } else {
        for (i = 0; i < nfiles && !state.stop_all; ++i) {
            struct stat st;

            if (opts.recursive && stat(files[i], &st) == 0 &&
                S_ISDIR(st.st_mode)) {
                grep_directory(files[i], &opts, re_ptr, pattern, &state,
                               show_name);
            } else {
                grep_file(files[i], &opts, re_ptr, pattern, &state,
                          show_name);
            }
        }
    }

    if (re_ptr != NULL)
        regfree(re_ptr);

    if (state.had_error)
        status = 2;
    else if (state.any_match)
        status = 0;
    else
        status = 1;

    free(files);
    return status;
}
