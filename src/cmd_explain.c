/*
 * cmd_explain.c - show the parser's interpretation without executing it.
 */
#include "myshell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *join_arguments(int argc, char **argv)
{
    size_t total = 1;
    int i;
    char *text;
    char *p;

    for (i = 1; i < argc; ++i)
        total += strlen(argv[i]) + 1;
    text = msh_malloc(total);
    p = text;
    for (i = 1; i < argc; ++i) {
        size_t len = strlen(argv[i]);

        if (i > 1)
            *p++ = ' ';
        memcpy(p, argv[i], len);
        p += len;
    }
    *p = '\0';
    return text;
}

static void print_command(const command_t *cmd, int last)
{
    int i;

    printf("  %s─ cmd: ", last ? "└" : "├");
    for (i = 0; i < cmd->argc; ++i)
        printf("%s%s", i ? " " : "", cmd->argv[i]);
    putchar('\n');

    if (cmd->redir.in_path != NULL)
        printf("  %s    ├─ stdin  ← %s\n", last ? " " : "│",
               cmd->redir.in_path);
    if (cmd->redir.out_path != NULL)
        printf("  %s    ├─ stdout → %s (%s)\n", last ? " " : "│",
               cmd->redir.out_path,
               cmd->redir.out_append ? "append" : "truncate");
    if (cmd->redir.err_path != NULL)
        printf("  %s    ├─ stderr → %s (%s)\n", last ? " " : "│",
               cmd->redir.err_path,
               cmd->redir.err_append ? "append" : "truncate");
    if (cmd->redir.err_to_out)
        printf("  %s    ├─ stderr → stdout\n", last ? " " : "│");
    if (cmd->redir.merge_err)
        printf("  %s    ├─ stderr → stdout (same file)\n", last ? " " : "│");
}

static const char *connector_text(connector_t connector)
{
    switch (connector) {
    case CONN_AND:
        return "after success (&&)";
    case CONN_OR:
        return "after failure (||)";
    default:
        return "always";
    }
}

int cmd_explain(int argc, char **argv, shell_state_t *sh)
{
    char *text;
    char *error = NULL;
    command_list_t list;
    int i;

    if (argc < 2) {
        msh_usage("explain 'command line'");
        return 2;
    }

    text = join_arguments(argc, argv);
    if (parse_line(text, sh, &list, &error) < 0) {
        msh_error("explain: %s", error != NULL ? error : "syntax error");
        free(error);
        free(text);
        return 2;
    }
    free(text);

    printf("explain: %d pipeline(s)\n", list.npipelines);
    for (i = 0; i < list.npipelines; ++i) {
        pipeline_t *pl = &list.pipelines[i];
        int j;

        printf("\n[pipeline %d] %s", i + 1,
               pl->background ? "background" : "foreground");
        if (i == 0)
            printf(" (first)");
        else
            printf(" (%s)", connector_text(list.connectors[i]));
        putchar('\n');

        for (j = 0; j < pl->ncommands; ++j)
            print_command(&pl->commands[j], j == pl->ncommands - 1);
    }

    free_command_list(&list);
    return 0;
}
