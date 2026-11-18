/*
 * cmd_fs.c - built-in mkdir, rmdir, ln, cp, rm, mv
 */
#include "myshell.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Small shared helpers                                               */
/* ------------------------------------------------------------------ */

static int is_option(const char *arg)
{
    return arg != NULL && arg[0] == '-' && arg[1] != '\0' &&
           strcmp(arg, "--") != 0;
}

static int same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

/* Return 1 if child is parent or lies below parent (both real paths). */
static int path_is_within(const char *parent, const char *child)
{
    char *rp = realpath(parent, NULL);
    char *rc = realpath(child, NULL);
    size_t n;
    int inside = 0;

    if (rp == NULL || rc == NULL) {
        free(rp);
        free(rc);
        return 0;
    }
    n = strlen(rp);
    if (strncmp(rc, rp, n) == 0 && (rc[n] == '/' || rc[n] == '\0'))
        inside = 1;
    free(rp);
    free(rc);
    return inside;
}

static int destination_inside_source(const char *src, const char *dst)
{
    struct stat st;
    char *parent;
    int inside;

    if (stat(src, &st) < 0 || !S_ISDIR(st.st_mode))
        return 0;
    if (msh_is_directory(dst))
        return path_is_within(src, dst);
    parent = msh_dirname_dup(dst);
    inside = path_is_within(src, parent);
    free(parent);
    return inside;
}

static int copy_symlink_entry(const char *src, const char *dst, int force,
                              int interactive, int verbose)
{
    struct stat lst;
    char *target;
    ssize_t n;
    size_t size;

    if (lstat(src, &lst) < 0) {
        msh_perror(src);
        return -1;
    }
    if (msh_path_exists(dst)) {
        if (interactive && !force &&
            !msh_confirm("overwrite '%s'?", dst))
            return 0;
        if (force && trash_move(dst) < 0)
            return -1;
        if (!force && unlink(dst) < 0) {
            msh_perror(dst);
            return -1;
        }
    }

    size = lst.st_size > 0 ? (size_t)lst.st_size + 1 : 256;
    target = msh_malloc(size);
    n = readlink(src, target, size - 1);
    if (n < 0) {
        msh_perror(src);
        free(target);
        return -1;
    }
    target[n] = '\0';
    if (symlink(target, dst) < 0) {
        msh_perror(dst);
        free(target);
        return -1;
    }
    if (verbose)
        printf("'%s' -> '%s'\n", src, dst);
    free(target);
    return 0;
}

/* ------------------------------------------------------------------ */
/* mkdir                                                              */
/* ------------------------------------------------------------------ */

int cmd_mkdir(int argc, char **argv)
{
    int parents = 0;
    int verbose = 0;
    int end_options = 0;
    mode_t mode = 0777;
    char **operands;
    int noperands = 0;
    int status = 0;
    int i;

    operands = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            if (strcmp(arg, "-p") == 0) {
                parents = 1;
            } else if (strcmp(arg, "-v") == 0) {
                verbose = 1;
            } else if (strcmp(arg, "-m") == 0) {
                if (++i >= argc ||
                    msh_parse_octal(argv[i], &mode) < 0) {
                    msh_error("mkdir: invalid mode");
                    free(operands);
                    return 2;
                }
            } else if (strncmp(arg, "-m", 2) == 0 &&
                       msh_parse_octal(arg + 2, &mode) == 0) {
                /* -m755 */
            } else {
                msh_error("mkdir: invalid option: %s", arg);
                msh_usage("mkdir [-p] [-v] [-m mode] directory ...");
                free(operands);
                return 2;
            }
            continue;
        }
        operands[noperands++] = argv[i];
    }

    if (noperands == 0) {
        msh_usage("mkdir [-p] [-v] [-m mode] directory ...");
        free(operands);
        return 2;
    }

    for (i = 0; i < noperands; ++i) {
        if (parents) {
            if (msh_mkdir_p(operands[i], mode) < 0) {
                status = 1;
            } else if (verbose) {
                printf("created directory '%s'\n", operands[i]);
            }
        } else if (mkdir(operands[i], mode) < 0) {
            msh_perror(operands[i]);
            status = 1;
        } else if (verbose) {
            printf("created directory '%s'\n", operands[i]);
        }
    }

    free(operands);
    return status;
}

/* ------------------------------------------------------------------ */
/* rmdir                                                              */
/* ------------------------------------------------------------------ */

static void rmdir_parents(const char *path, int verbose)
{
    char *copy = msh_strdup(path);
    char *p = copy;

    for (;;) {
        char *slash = strrchr(p, '/');

        if (slash == NULL)
            break;
        if (slash == p)
            slash[1] = '\0';
        else
            *slash = '\0';
        if (rmdir(p) < 0)
            break;
        if (verbose)
            printf("removed directory '%s'\n", p);
    }
    free(copy);
}

int cmd_rmdir(int argc, char **argv)
{
    int parents = 0;
    int verbose = 0;
    int end_options = 0;
    int status = 0;
    int noperands = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            if (strcmp(arg, "-p") == 0)
                parents = 1;
            else if (strcmp(arg, "-v") == 0)
                verbose = 1;
            else {
                msh_error("rmdir: invalid option: %s", arg);
                msh_usage("rmdir [-p] [-v] directory ...");
                return 2;
            }
            continue;
        }

        ++noperands;
        if (rmdir(arg) < 0) {
            msh_perror(arg);
            status = 1;
        } else {
            if (verbose)
                printf("removed directory '%s'\n", arg);
            if (parents)
                rmdir_parents(arg, verbose);
        }
    }

    if (noperands == 0)
        msh_usage("rmdir [-p] [-v] directory ...");
    return status;
}

/* ------------------------------------------------------------------ */
/* ln                                                                 */
/* ------------------------------------------------------------------ */

static int create_link(const char *target, const char *linkname,
                       int symbolic, int force, int verbose)
{
    if (msh_path_exists(linkname)) {
        if (!force) {
            msh_error("%s: file exists", linkname);
            return -1;
        }
        if (unlink(linkname) < 0) {
            msh_perror(linkname);
            return -1;
        }
    }

    if (symbolic) {
        if (symlink(target, linkname) < 0) {
            msh_perror(linkname);
            return -1;
        }
    } else {
        if (link(target, linkname) < 0) {
            msh_perror(linkname);
            return -1;
        }
    }
    if (verbose)
        printf("'%s' -> '%s'\n", linkname, target);
    return 0;
}

int cmd_ln(int argc, char **argv)
{
    int symbolic = 0;
    int force = 0;
    int verbose = 0;
    int end_options = 0;
    char **operands;
    int noperands = 0;
    int status = 0;
    int i;

    operands = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            const char *p;

            for (p = arg + 1; *p != '\0'; ++p) {
                if (*p == 's')
                    symbolic = 1;
                else if (*p == 'f')
                    force = 1;
                else if (*p == 'v')
                    verbose = 1;
                else {
                    msh_error("ln: invalid option -- '%c'", *p);
                    msh_usage("ln [-s] [-f] [-v] target [linkname]");
                    free(operands);
                    return 2;
                }
            }
            continue;
        }
        operands[noperands++] = argv[i];
    }

    if (noperands == 0 || noperands == 1) {
        if (noperands == 1) {
            char *base = msh_basename_dup(operands[0]);
            if (create_link(operands[0], base, symbolic, force, verbose) < 0)
                status = 1;
            free(base);
        } else {
            msh_usage("ln [-s] [-f] [-v] target [linkname]");
            status = 2;
        }
        free(operands);
        return status;
    }

    if (noperands > 2) {
        const char *dest = operands[noperands - 1];

        if (!msh_is_directory(dest)) {
            msh_error("ln: target '%s' is not a directory", dest);
            free(operands);
            return 1;
        }
        for (i = 0; i < noperands - 1; ++i) {
            char *base = msh_basename_dup(operands[i]);
            char *linkname = msh_join_path(dest, base);

            if (create_link(operands[i], linkname, symbolic, force,
                            verbose) < 0)
                status = 1;
            free(base);
            free(linkname);
        }
        free(operands);
        return status;
    }

    {
        const char *target = operands[0];
        const char *linkname = operands[1];

        if (msh_is_directory(linkname)) {
            char *base = msh_basename_dup(target);
            char *path = msh_join_path(linkname, base);

            if (create_link(target, path, symbolic, force, verbose) < 0)
                status = 1;
            free(base);
            free(path);
        } else if (create_link(target, linkname, symbolic, force,
                               verbose) < 0) {
            status = 1;
        }
    }

    free(operands);
    return status;
}

/* ------------------------------------------------------------------ */
/* cp                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int recursive;
    int force;
    int interactive;
    int preserve;
    int verbose;
} cp_options_t;

static int copy_regular(const char *src, const char *dst,
                        const struct stat *sst, const cp_options_t *opts)
{
    struct stat dst_st;

    if (stat(dst, &dst_st) == 0 && same_file(sst, &dst_st)) {
        msh_error("%s and %s are the same file", src, dst);
        return -1;
    }
    if (opts->interactive && msh_path_exists(dst) &&
        !msh_confirm("overwrite '%s'?", dst))
        return 0;
    if (opts->force && msh_path_exists(dst) &&
        trash_move(dst) < 0)
        return -1;

    if (msh_copy_file_contents(src, dst, sst->st_mode, opts->force) < 0)
        return -1;

    if (opts->preserve) {
        struct timespec times[2] = { sst->st_atim, sst->st_mtim };
        (void)utimensat(AT_FDCWD, dst, times, 0);
    }
    if (opts->verbose)
        printf("'%s' -> '%s'\n", src, dst);
    return 0;
}

static int copy_entry(const char *src, const char *dst,
                      const cp_options_t *opts);

static int copy_directory(const char *src, const char *dst,
                          const cp_options_t *opts)
{
    struct stat sst;
    DIR *dir;
    struct dirent *entry;
    int rc = 0;

    if (lstat(src, &sst) < 0) {
        msh_perror(src);
        return -1;
    }

    if (msh_path_exists(dst)) {
        if (!msh_is_directory(dst)) {
            msh_error("%s: not a directory", dst);
            return -1;
        }
    } else if (mkdir(dst, sst.st_mode & 07777) < 0) {
        msh_perror(dst);
        return -1;
    }

    dir = opendir(src);
    if (dir == NULL) {
        msh_perror(src);
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char *s, *d;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        s = msh_join_path(src, entry->d_name);
        d = msh_join_path(dst, entry->d_name);
        if (copy_entry(s, d, opts) < 0)
            rc = -1;
        free(s);
        free(d);
    }
    closedir(dir);

    if (chmod(dst, sst.st_mode & 07777) < 0) {
        msh_perror(dst);
        rc = -1;
    }
    if (opts->preserve) {
        struct timespec times[2] = { sst.st_atim, sst.st_mtim };
        (void)utimensat(AT_FDCWD, dst, times, 0);
    }
    if (opts->verbose && rc == 0)
        printf("'%s' -> '%s'\n", src, dst);
    return rc;
}

static int copy_entry(const char *src, const char *dst,
                      const cp_options_t *opts)
{
    struct stat sst;

    if (lstat(src, &sst) < 0) {
        msh_perror(src);
        return -1;
    }

    if (S_ISLNK(sst.st_mode)) {
        if (opts->recursive)
            return copy_symlink_entry(src, dst, opts->force,
                                      opts->interactive, opts->verbose);
        if (stat(src, &sst) < 0) {
            msh_perror(src);
            return -1;
        }
    }

    if (S_ISDIR(sst.st_mode)) {
        if (!opts->recursive) {
            msh_error("%s: omitting directory", src);
            return -1;
        }
        return copy_directory(src, dst, opts);
    }

    if (S_ISREG(sst.st_mode))
        return copy_regular(src, dst, &sst, opts);

    msh_error("%s: unsupported file type", src);
    return -1;
}

static int cp_top_level(const char *src, const char *dst,
                        const cp_options_t *opts)
{
    if (destination_inside_source(src, dst)) {
        msh_error("%s: cannot copy a directory into itself", src);
        return -1;
    }
    if (msh_is_directory(dst)) {
        char *base = msh_basename_dup(src);
        char *target = msh_join_path(dst, base);
        int rc = copy_entry(src, target, opts);

        free(base);
        free(target);
        return rc;
    }
    return copy_entry(src, dst, opts);
}

int cmd_cp(int argc, char **argv)
{
    cp_options_t opts;
    char **operands;
    int noperands = 0;
    int end_options = 0;
    int status = 0;
    int i;

    memset(&opts, 0, sizeof(opts));
    operands = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            const char *p;

            for (p = arg + 1; *p != '\0'; ++p) {
                switch (*p) {
                case 'r':
                case 'R':
                    opts.recursive = 1;
                    break;
                case 'f':
                    opts.force = 1;
                    opts.interactive = 0;
                    break;
                case 'i':
                    opts.interactive = 1;
                    opts.force = 0;
                    break;
                case 'p':
                    opts.preserve = 1;
                    break;
                case 'v':
                    opts.verbose = 1;
                    break;
                default:
                    msh_error("cp: invalid option -- '%c'", *p);
                    msh_usage("cp [-rR] [-f] [-i] [-p] [-v] "
                              "source ... destination");
                    free(operands);
                    return 2;
                }
            }
            continue;
        }
        operands[noperands++] = argv[i];
    }

    if (noperands < 2) {
        msh_usage("cp [-rR] [-f] [-i] [-p] [-v] source ... destination");
        free(operands);
        return 2;
    }

    if (noperands > 2) {
        const char *dest = operands[noperands - 1];

        if (!msh_is_directory(dest)) {
            msh_error("cp: target '%s' is not a directory", dest);
            free(operands);
            return 1;
        }
        for (i = 0; i < noperands - 1; ++i) {
            if (cp_top_level(operands[i], dest, &opts) < 0)
                status = 1;
        }
    } else if (cp_top_level(operands[0], operands[1], &opts) < 0) {
        status = 1;
    }

    free(operands);
    return status;
}

/* ------------------------------------------------------------------ */
/* rm                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int recursive;
    int force;
    int interactive;
    int verbose;
    int no_trash;
} rm_options_t;

static int rm_one(const char *path, const rm_options_t *opts)
{
    struct stat st;

    if (strcmp(path, "/") == 0 || strcmp(path, ".") == 0 ||
        strcmp(path, "..") == 0) {
        msh_error("rm: refusing to remove '%s'", path);
        return -1;
    }

    if (lstat(path, &st) < 0) {
        if (errno == ENOENT && opts->force)
            return 0;
        msh_perror(path);
        return -1;
    }

    if (S_ISDIR(st.st_mode) && !opts->recursive) {
        msh_error("%s: is a directory", path);
        return -1;
    }

    if (opts->interactive && !opts->force &&
        !msh_confirm("remove '%s'?", path))
        return 0;

    if (!opts->no_trash) {
        if (trash_move(path) < 0)
            return -1;
        if (opts->verbose)
            printf("trashed '%s' (undo available)\n", path);
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        if (msh_remove_tree(path) < 0)
            return -1;
    } else if (unlink(path) < 0) {
        if (errno != ENOENT || !opts->force) {
            msh_perror(path);
            return -1;
        }
    }

    if (opts->verbose)
        printf("removed '%s'\n", path);
    return 0;
}

int cmd_rm(int argc, char **argv)
{
    rm_options_t opts;
    char **operands;
    int noperands = 0;
    int end_options = 0;
    int status = 0;
    int i;

    memset(&opts, 0, sizeof(opts));
    operands = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && strcmp(arg, "--no-trash") == 0) {
            opts.no_trash = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            const char *p;

            for (p = arg + 1; *p != '\0'; ++p) {
                switch (*p) {
                case 'r':
                case 'R':
                    opts.recursive = 1;
                    break;
                case 'f':
                    opts.force = 1;
                    opts.interactive = 0;
                    break;
                case 'i':
                    opts.interactive = 1;
                    opts.force = 0;
                    break;
                case 'v':
                    opts.verbose = 1;
                    break;
                default:
                    msh_error("rm: invalid option -- '%c'", *p);
                    msh_usage("rm [-rR] [-f] [-i] [-v] [--no-trash] file ...");
                    free(operands);
                    return 2;
                }
            }
            continue;
        }
        operands[noperands++] = argv[i];
    }

    if (noperands == 0) {
        if (opts.force) {
            free(operands);
            return 0;
        }
        msh_usage("rm [-rR] [-f] [-i] [-v] file ...");
        free(operands);
        return 2;
    }

    for (i = 0; i < noperands; ++i) {
        if (rm_one(operands[i], &opts) < 0)
            status = 1;
    }

    free(operands);
    return status;
}

/* ------------------------------------------------------------------ */
/* mv                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int force;
    int interactive;
    int no_clobber;
    int verbose;
} mv_options_t;

static int copy_for_move(const char *src, const char *dst)
{
    struct stat st;

    if (lstat(src, &st) < 0) {
        msh_perror(src);
        return -1;
    }
    if (S_ISLNK(st.st_mode))
        return copy_symlink_entry(src, dst, 1, 0, 0);
    if (S_ISDIR(st.st_mode))
        return msh_copy_tree(src, dst, 1);
    if (S_ISREG(st.st_mode))
        return msh_copy_file_contents(src, dst, st.st_mode, 1);
    msh_error("%s: unsupported file type", src);
    return -1;
}

static int mv_one(const char *src, const char *dst,
                  const mv_options_t *opts)
{
    struct stat sst, dst_st;
    char *target;
    int rc;

    if (lstat(src, &sst) < 0) {
        msh_perror(src);
        return -1;
    }

    if (msh_is_directory(dst)) {
        char *base = msh_basename_dup(src);
        target = msh_join_path(dst, base);
        free(base);
    } else {
        target = msh_strdup(dst);
    }

    if (destination_inside_source(src, target)) {
        msh_error("%s: cannot move a directory into itself", src);
        free(target);
        return -1;
    }

    if (stat(target, &dst_st) == 0) {
        if (same_file(&sst, &dst_st)) {
            msh_error("'%s' and '%s' are the same file", src, target);
            free(target);
            return -1;
        }
        if (opts->no_clobber) {
            free(target);
            return 0;
        }
        if (opts->interactive && !opts->force &&
            !msh_confirm("overwrite '%s'?", target)) {
            free(target);
            return 0;
        }
        if (S_ISDIR(dst_st.st_mode) && !S_ISDIR(sst.st_mode)) {
            msh_error("cannot overwrite directory '%s' with non-directory",
                      target);
            free(target);
            return -1;
        }
        if (!S_ISDIR(dst_st.st_mode) && S_ISDIR(sst.st_mode)) {
            msh_error("cannot overwrite non-directory '%s' with directory",
                      target);
            free(target);
            return -1;
        }
        if (!S_ISDIR(dst_st.st_mode) &&
            trash_move_ex(target, src) < 0) {
            free(target);
            return -1;
        }
    }

    if (rename(src, target) == 0) {
        if (opts->verbose)
            printf("'%s' -> '%s'\n", src, target);
        free(target);
        return 0;
    }

    if (errno != EXDEV) {
        msh_perror(target);
        free(target);
        return -1;
    }

    rc = copy_for_move(src, target);
    if (rc == 0) {
        if (S_ISDIR(sst.st_mode))
            rc = msh_remove_tree(src);
        else if (unlink(src) < 0) {
            msh_perror(src);
            rc = -1;
        }
    }
    if (rc == 0 && opts->verbose)
        printf("'%s' -> '%s'\n", src, target);
    free(target);
    return rc;
}

int cmd_mv(int argc, char **argv)
{
    mv_options_t opts;
    char **operands;
    int noperands = 0;
    int end_options = 0;
    int status = 0;
    int i;

    memset(&opts, 0, sizeof(opts));
    operands = msh_calloc((size_t)argc, sizeof(char *));

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }
        if (!end_options && is_option(arg)) {
            const char *p;

            for (p = arg + 1; *p != '\0'; ++p) {
                switch (*p) {
                case 'f':
                    opts.force = 1;
                    opts.interactive = 0;
                    opts.no_clobber = 0;
                    break;
                case 'i':
                    opts.interactive = 1;
                    opts.force = 0;
                    opts.no_clobber = 0;
                    break;
                case 'n':
                    opts.no_clobber = 1;
                    break;
                case 'v':
                    opts.verbose = 1;
                    break;
                default:
                    msh_error("mv: invalid option -- '%c'", *p);
                    msh_usage("mv [-f] [-i] [-n] [-v] "
                              "source ... destination");
                    free(operands);
                    return 2;
                }
            }
            continue;
        }
        operands[noperands++] = argv[i];
    }

    if (noperands < 2) {
        msh_usage("mv [-f] [-i] [-n] [-v] source ... destination");
        free(operands);
        return 2;
    }

    if (noperands > 2 && !msh_is_directory(operands[noperands - 1])) {
        msh_error("mv: target '%s' is not a directory",
                  operands[noperands - 1]);
        free(operands);
        return 1;
    }

    for (i = 0; i < noperands - 1; ++i) {
        if (mv_one(operands[i], operands[noperands - 1], &opts) < 0)
            status = 1;
    }

    free(operands);
    return status;
}
