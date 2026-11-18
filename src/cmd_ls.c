/*
 * cmd_ls.c - built-in ls
 *
 * Supported options: -a, -l, -R, -1, -h
 */
#include "myshell.h"

#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int show_all;
    int long_format;
    int recursive;
    int one_per_line;
    int human_readable;
} ls_options_t;

typedef struct {
    char **names;
    int count;
    int cap;
} name_list_t;

typedef struct {
    char *name;
    struct stat st;
    char *link_target;
} file_info_t;

static void names_add(name_list_t *nl, const char *name)
{
    if (nl->count == nl->cap) {
        nl->cap = nl->cap ? nl->cap * 2 : 32;
        nl->names = msh_realloc(nl->names,
                                (size_t)nl->cap * sizeof(char *));
    }
    nl->names[nl->count++] = msh_strdup(name);
}

static void names_free(name_list_t *nl)
{
    int i;

    for (i = 0; i < nl->count; ++i)
        free(nl->names[i]);
    free(nl->names);
    memset(nl, 0, sizeof(*nl));
}

static int name_cmp(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;

    return strcmp(*sa, *sb);
}

static int read_directory(const char *path, const ls_options_t *opts,
                          name_list_t *nl)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(path);
    if (dir == NULL) {
        msh_perror(path);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!opts->show_all &&
            (strcmp(entry->d_name, ".") == 0 ||
             strcmp(entry->d_name, "..") == 0))
            continue;
        names_add(nl, entry->d_name);
    }
    closedir(dir);
    qsort(nl->names, (size_t)nl->count, sizeof(char *), name_cmp);
    return 0;
}

static void owner_name(uid_t uid, char *buf, size_t size)
{
    struct passwd *pw = getpwuid(uid);

    if (pw != NULL)
        snprintf(buf, size, "%s", pw->pw_name);
    else
        snprintf(buf, size, "%u", (unsigned)uid);
}

static void group_name(gid_t gid, char *buf, size_t size)
{
    struct group *gr = getgrgid(gid);

    if (gr != NULL)
        snprintf(buf, size, "%s", gr->gr_name);
    else
        snprintf(buf, size, "%u", (unsigned)gid);
}

static char *read_link_target(const char *path, const struct stat *st)
{
    size_t size = st->st_size > 0 ? (size_t)st->st_size + 1 : 256;
    char *buf = msh_malloc(size);
    ssize_t n = readlink(path, buf, size - 1);

    if (n < 0) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static void print_long_entry(const file_info_t *info, size_t user_width,
                             size_t group_width, size_t size_width,
                             int human_readable)
{
    char mode[11];
    char user[64], group[64];
    char date[32];
    char sizebuf[32];
    struct tm tm;
    const char *name = info->name;

    msh_format_mode(info->st.st_mode, mode);
    owner_name(info->st.st_uid, user, sizeof(user));
    group_name(info->st.st_gid, group, sizeof(group));
    localtime_r(&info->st.st_mtime, &tm);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &tm);

    if (human_readable)
        msh_format_size(msh_file_size(&info->st), sizebuf, sizeof(sizebuf));
    else
        snprintf(sizebuf, sizeof(sizebuf), "%lld",
                 msh_file_size(&info->st));

    printf("%s %*lu %-*s %-*s %*s %s %s", mode,
           (int)3, (unsigned long)info->st.st_nlink,
           (int)user_width, user,
           (int)group_width, group,
           (int)size_width, sizebuf,
           date, name);

    if (info->link_target != NULL)
        printf(" -> %s", info->link_target);
    putchar('\n');
}

static void load_info(const char *dir, const char *name, file_info_t *info)
{
    char *path = msh_join_path(dir, name);

    memset(info, 0, sizeof(*info));
    info->name = msh_strdup(name);
    if (lstat(path, &info->st) < 0) {
        msh_perror(path);
        info->st.st_mode = 0;
    } else if (S_ISLNK(info->st.st_mode)) {
        info->link_target = read_link_target(path, &info->st);
    }
    free(path);
}

static void print_long_listing(const char *dir, const name_list_t *nl,
                               const ls_options_t *opts)
{
    file_info_t *infos;
    int i;
    long long total = 0;
    size_t user_width = 1, group_width = 1;
    size_t size_width = 1;
    size_t nlink_width = 1;

    if (nl->count == 0)
        return;

    infos = msh_calloc((size_t)nl->count, sizeof(*infos));
    for (i = 0; i < nl->count; ++i) {
        char user[64], group[64], sizebuf[32];

        load_info(dir, nl->names[i], &infos[i]);
        if (infos[i].st.st_mode != 0)
            total += (long long)infos[i].st.st_blocks / 2;

        owner_name(infos[i].st.st_uid, user, sizeof(user));
        group_name(infos[i].st.st_gid, group, sizeof(group));
        if (strlen(user) > user_width)
            user_width = strlen(user);
        if (strlen(group) > group_width)
            group_width = strlen(group);
        snprintf(sizebuf, sizeof(sizebuf), "%lld",
                 msh_file_size(&infos[i].st));
        if (strlen(sizebuf) > size_width)
            size_width = strlen(sizebuf);
        {
            char nbuf[32];
            snprintf(nbuf, sizeof(nbuf), "%lu",
                     (unsigned long)infos[i].st.st_nlink);
            if (strlen(nbuf) > nlink_width)
                nlink_width = strlen(nbuf);
        }
    }

    printf("total %lld\n", total);
    for (i = 0; i < nl->count; ++i) {
        if (infos[i].st.st_mode != 0)
            print_long_entry(&infos[i], user_width, group_width,
                             size_width, opts->human_readable);
        free(infos[i].name);
        free(infos[i].link_target);
    }
    free(infos);
    (void)nlink_width;
}

static void print_simple_listing(const name_list_t *nl)
{
    int i;

    for (i = 0; i < nl->count; ++i)
        printf("%s\n", nl->names[i]);
}

static void print_single(const char *path, const ls_options_t *opts)
{
    struct stat st;

    if (lstat(path, &st) < 0) {
        msh_perror(path);
        return;
    }

    if (opts->long_format) {
        file_info_t info;

        memset(&info, 0, sizeof(info));
        info.name = msh_strdup(path);
        info.st = st;
        if (S_ISLNK(st.st_mode))
            info.link_target = read_link_target(path, &st);
        print_long_entry(&info, 8, 8, 8, opts->human_readable);
        free(info.name);
        free(info.link_target);
    } else {
        printf("%s\n", path);
    }
}

static int list_directory(const char *path, const ls_options_t *opts,
                          int print_header, int *first)
{
    name_list_t nl = { 0 };
    int i;

    if (read_directory(path, opts, &nl) < 0)
        return -1;

    if (print_header) {
        if (!*first)
            putchar('\n');
        printf("%s:\n", path);
        *first = 0;
    }

    if (opts->long_format)
        print_long_listing(path, &nl, opts);
    else
        print_simple_listing(&nl);

    if (opts->recursive) {
        for (i = 0; i < nl.count; ++i) {
            char *child;
            struct stat st;

            if (strcmp(nl.names[i], ".") == 0 ||
                strcmp(nl.names[i], "..") == 0)
                continue;
            child = msh_join_path(path, nl.names[i]);
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
                list_directory(child, opts, 1, first);
            }
            free(child);
        }
    }

    names_free(&nl);
    return 0;
}

static void print_ls_usage(void)
{
    msh_usage("ls [-a] [-l] [-R] [-1] [-h] [file ...]");
}

int cmd_ls(int argc, char **argv)
{
    ls_options_t opts;
    char **paths = NULL;
    int npaths = 0;
    int i;
    int end_options = 0;
    int status = 0;
    int first = 1;

    memset(&opts, 0, sizeof(opts));

    paths = msh_calloc((size_t)argc, sizeof(char *));

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
                case 'a': opts.show_all = 1; break;
                case 'l': opts.long_format = 1; break;
                case 'R': opts.recursive = 1; break;
                case '1': opts.one_per_line = 1; break;
                case 'h': opts.human_readable = 1; break;
                default:
                    msh_error("ls: invalid option -- '%c'", *p);
                    print_ls_usage();
                    free(paths);
                    return 2;
                }
            }
            continue;
        }
        paths[npaths++] = argv[i];
    }

    if (npaths == 0)
        paths[npaths++] = ".";

    for (i = 0; i < npaths; ++i) {
        struct stat st;

        if (stat(paths[i], &st) < 0) {
            msh_perror(paths[i]);
            status = 1;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            int header = (npaths > 1 || opts.recursive);
            if (list_directory(paths[i], &opts, header, &first) < 0)
                status = 1;
        } else {
            print_single(paths[i], &opts);
        }
    }

    free(paths);
    return status;
}
