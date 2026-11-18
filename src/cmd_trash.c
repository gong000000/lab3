/*
 * cmd_trash.c - trash bin for destructive file operations and undo.
 *
 * rm, mv overwrite, and cp -f overwrite move the old file to
 * ~/.myshell_trash before changing the filesystem.  undo restores the
 * most recent entry from the manifest.
 */
#include "myshell.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *trash_directory(int create)
{
    const char *home = getenv("HOME");
    char *dir;

    if (home == NULL || home[0] == '\0')
        home = "/tmp";
    dir = msh_join_path(home, ".myshell_trash");
    if (create && mkdir(dir, 0700) < 0 && errno != EEXIST) {
        msh_perror(dir);
        free(dir);
        return NULL;
    }
    return dir;
}

static char *manifest_path(void)
{
    char *dir = trash_directory(1);
    char *path;

    if (dir == NULL)
        return NULL;
    path = msh_join_path(dir, ".undo");
    free(dir);
    return path;
}

static char *absolute_path(const char *path)
{
    char cwd[4096];

    if (path[0] == '/')
        return msh_strdup(path);
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return NULL;
    return msh_join_path(cwd, path);
}

static char *trash_name(const char *path)
{
    char *dir = trash_directory(1);
    char *base = msh_basename_dup(path);
    char *result;
    char candidate[4096];
    int counter = 0;

    if (dir == NULL) {
        free(base);
        return NULL;
    }

    for (;;) {
        snprintf(candidate, sizeof(candidate), "%s/%ld-%d-%d-%s",
                 dir, (long)time(NULL), (int)getpid(), counter, base);
        if (!msh_path_exists(candidate))
            break;
        ++counter;
    }
    result = msh_strdup(candidate);
    free(dir);
    free(base);
    return result;
}

static int append_manifest(const char *trash, const char *original,
                           const char *undo_extra)
{
    char *manifest = manifest_path();
    int fd;
    char *line;
    size_t len;
    int rc = 0;

    if (manifest == NULL)
        return -1;
    fd = open(manifest, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        msh_perror(manifest);
        free(manifest);
        return -1;
    }
    len = strlen(trash) + strlen(original) +
          (undo_extra != NULL ? strlen(undo_extra) : 0) + 3;
    line = msh_malloc(len + 1);
    snprintf(line, len + 1, "%s\t%s\t%s\n", trash, original,
             undo_extra != NULL ? undo_extra : "");
    if (msh_write_all(fd, line, len) < 0) {
        msh_perror(manifest);
        rc = -1;
    }
    free(line);
    close(fd);
    free(manifest);
    return rc;
}

static int move_path(const char *src, const char *dst)
{
    if (rename(src, dst) == 0)
        return 0;
    if (errno != EXDEV)
        return -1;
    if (msh_copy_tree(src, dst, 1) < 0)
        return -1;
    if (msh_remove_tree(src) < 0)
        return -1;
    return 0;
}

int trash_move_ex(const char *path, const char *undo_extra)
{
    struct stat st;
    char *absolute;
    char *trash;

    if (lstat(path, &st) < 0) {
        msh_perror(path);
        return -1;
    }
    absolute = absolute_path(path);
    if (absolute == NULL) {
        msh_perror("trash");
        return -1;
    }
    trash = trash_name(path);
    if (trash == NULL) {
        free(absolute);
        return -1;
    }

    if (move_path(path, trash) < 0) {
        msh_perror(path);
        free(absolute);
        free(trash);
        return -1;
    }

    if (append_manifest(trash, absolute, undo_extra) < 0) {
        /* Put the file back rather than silently losing track of it. */
        (void)rename(trash, path);
        free(absolute);
        free(trash);
        return -1;
    }

    free(absolute);
    free(trash);
    return 0;
}

int trash_move(const char *path)
{
    return trash_move_ex(path, NULL);
}

static int rewrite_manifest_prefix(const char *manifest, const char *data,
                                   size_t length)
{
    int fd = open(manifest, O_WRONLY | O_TRUNC);
    int rc = 0;

    if (fd < 0) {
        msh_perror(manifest);
        return -1;
    }
    if (length > 0 && msh_write_all(fd, data, length) < 0) {
        msh_perror(manifest);
        rc = -1;
    }
    close(fd);
    return rc;
}

int cmd_undo(int argc, char **argv, shell_state_t *sh)
{
    char *manifest;
    char *data = NULL;
    long size;
    FILE *fp;
    char *last;
    char *tab;
    char *trash;
    char *original;
    char *extra;
    char *tab2;
    char *newline;
    size_t prefix_len;
    int rc = 0;

    (void)sh;
    (void)argv;
    if (argc != 1) {
        msh_usage("undo");
        return 2;
    }

    manifest = manifest_path();
    if (manifest == NULL)
        return 1;
    fp = fopen(manifest, "rb");
    if (fp == NULL) {
        msh_error("undo: nothing to restore");
        free(manifest);
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) < 0) {
        fclose(fp);
        free(manifest);
        return 1;
    }
    size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        msh_error("undo: nothing to restore");
        free(manifest);
        return 1;
    }
    rewind(fp);
    data = msh_malloc((size_t)size + 1);
    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(data);
        free(manifest);
        return 1;
    }
    data[size] = '\0';
    fclose(fp);

    last = data + size;
    while (last > data && (last[-1] == '\n' || last[-1] == '\r'))
        --last;
    newline = last;
    while (newline > data && newline[-1] != '\n')
        --newline;
    prefix_len = (size_t)(newline - data);
    if (newline == last || newline[0] == '\0') {
        free(data);
        free(manifest);
        msh_error("undo: nothing to restore");
        return 1;
    }

    tab = memchr(newline, '\t', (size_t)(last - newline));
    if (tab == NULL) {
        free(data);
        free(manifest);
        msh_error("undo: corrupt manifest");
        return 1;
    }
    trash = msh_strndup(newline, (size_t)(tab - newline));
    tab2 = memchr(tab + 1, '\t', (size_t)(last - (tab + 1)));
    if (tab2 != NULL) {
        original = msh_strndup(tab + 1, (size_t)(tab2 - (tab + 1)));
        extra = msh_strndup(tab2 + 1, (size_t)(last - (tab2 + 1)));
    } else {
        original = msh_strndup(tab + 1, (size_t)(last - (tab + 1)));
        extra = msh_strdup("");
    }

    if (extra[0] != '\0' && msh_path_exists(original)) {
        if (msh_path_exists(extra)) {
            msh_error("undo: '%s' already exists", extra);
            rc = 1;
            goto done;
        }
        if (move_path(original, extra) < 0) {
            msh_perror(extra);
            rc = 1;
            goto done;
        }
    }

    if (msh_is_directory(original)) {
        msh_error("undo: '%s' is a directory", original);
        rc = 1;
    } else if (move_path(trash, original) < 0) {
        msh_perror(original);
        rc = 1;
    } else {
        printf("undo: restored '%s'\n", original);
        if (rewrite_manifest_prefix(manifest, data, prefix_len) < 0)
            rc = 1;
    }

done:
    free(trash);
    free(original);
    free(extra);
    free(data);
    free(manifest);
    return rc;
}
