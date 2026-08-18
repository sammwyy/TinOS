/* Shell execution engine.
 *
 * A shell owns a working directory (kept as a path string so that it stays
 * meaningful even if another application deletes the directory), a command
 * history and a pending request for the UI layer (clear, exit, launch an
 * application).  Execution walks the parsed line, running each pipeline
 * stage into a stream that becomes the next stage's input.
 */

#include <tinos/heap.h>
#include <tinos/path.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>

struct shell {
    char         cwd[FS_PATH_MAX];
    char         previous_cwd[FS_PATH_MAX];
    char        *history[SH_HISTORY_MAX];
    size_t       history_count;
    sh_request_t request;
    char         request_arg[FS_PATH_MAX];
    int          last_status;
};

/* Lifecycle */

shell_t *shell_create(const char *initial_cwd)
{
    shell_t *sh = kcalloc(1, sizeof(shell_t));
    if (!sh)
        return NULL;
    path_normalize(initial_cwd && initial_cwd[0] ? initial_cwd : "/", sh->cwd,
                   sizeof(sh->cwd));
    strlcpy(sh->previous_cwd, sh->cwd, sizeof(sh->previous_cwd));

    /* Fall back to the root if the requested directory does not exist. */
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), sh->cwd, &node) != FS_OK || node->type != FS_DIR)
        strlcpy(sh->cwd, "/", sizeof(sh->cwd));
    return sh;
}

void shell_destroy(shell_t *sh)
{
    if (!sh)
        return;
    for (size_t i = 0; i < sh->history_count; i++)
        kfree(sh->history[i]);
    kfree(sh);
}

/* Working directory */

const char *shell_cwd(shell_t *sh)
{
    return sh->cwd;
}

fs_node_t *shell_cwd_node(shell_t *sh)
{
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), sh->cwd, &node) == FS_OK && node->type == FS_DIR)
        return node;

    /* The directory vanished (deleted from the file explorer, say): fall
     * back to the nearest surviving ancestor. */
    char probe[FS_PATH_MAX];
    strlcpy(probe, sh->cwd, sizeof(probe));
    while (strcmp(probe, "/") != 0) {
        char parent[FS_PATH_MAX];
        path_dirname(probe, parent, sizeof(parent));
        strlcpy(probe, parent, sizeof(probe));
        if (fs_resolve(fs_root(), probe, &node) == FS_OK &&
            node->type == FS_DIR) {
            strlcpy(sh->cwd, probe, sizeof(sh->cwd));
            return node;
        }
    }
    strlcpy(sh->cwd, "/", sizeof(sh->cwd));
    return fs_root();
}

void shell_set_cwd(shell_t *sh, const char *path)
{
    strlcpy(sh->previous_cwd, sh->cwd, sizeof(sh->previous_cwd));
    path_normalize(path, sh->cwd, sizeof(sh->cwd));
}

/* Used by `cd -`. */
const char *shell_previous_cwd(shell_t *sh)
{
    return sh->previous_cwd;
}

/* Requests to the UI */

void shell_request(shell_t *sh, sh_request_t request, const char *arg)
{
    sh->request = request;
    strlcpy(sh->request_arg, arg ? arg : "", sizeof(sh->request_arg));
}

sh_request_t shell_take_request(shell_t *sh, char *arg, size_t arg_size)
{
    sh_request_t request = sh->request;
    if (arg && arg_size)
        strlcpy(arg, sh->request_arg, arg_size);
    sh->request        = SH_REQ_NONE;
    sh->request_arg[0] = '\0';
    return request;
}

/* History */

static void history_add(shell_t *sh, const char *line)
{
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    if (sh->history_count &&
        strcmp(sh->history[sh->history_count - 1], line) == 0)
        return;

    char *copy = kstrdup(line);
    if (!copy)
        return;
    if (sh->history_count == SH_HISTORY_MAX) {
        kfree(sh->history[0]);
        for (size_t i = 1; i < SH_HISTORY_MAX; i++)
            sh->history[i - 1] = sh->history[i];
        sh->history_count--;
    }
    sh->history[sh->history_count++] = copy;
}

size_t shell_history_count(shell_t *sh)
{
    return sh->history_count;
}

const char *shell_history_at(shell_t *sh, size_t index)
{
    if (index >= sh->history_count)
        return NULL;
    return sh->history[index];
}

/* Execution */

static int run_stage(shell_t *sh, sh_command_t *cmd, stream_t *in,
                     stream_t *out, stream_t *err)
{
    const sh_command_def_t *def = sh_find_command(cmd->argv[0]);
    if (!def) {
        stream_printf(err, "tinos: %s: command not found\n", cmd->argv[0]);
        stream_puts(err, "      type 'help' to list the built-in commands\n");
        return 127;
    }

    /* "< file" replaces this stage's input. */
    stream_t redirected_in;
    stream_init(&redirected_in);
    if (cmd->input_path) {
        fs_node_t  *file = NULL;
        fs_result_t r = fs_resolve(shell_cwd_node(sh), cmd->input_path, &file);
        if (r != FS_OK) {
            stream_printf(err, "%s: %s: %s\n", cmd->argv[0], cmd->input_path,
                          fs_strerror(r));
            stream_free(&redirected_in);
            return 1;
        }
        if (file->type != FS_FILE) {
            stream_printf(err, "%s: %s: %s\n", cmd->argv[0], cmd->input_path,
                          fs_strerror(FS_ERR_ISDIR));
            stream_free(&redirected_in);
            return 1;
        }
        stream_write(&redirected_in, fs_data(file), fs_size(file));
        in = &redirected_in;
    }

    int status = def->fn(sh, cmd->argc, cmd->argv, in, out, err);
    stream_free(&redirected_in);
    return status;
}

static int run_pipeline(shell_t *sh, sh_segment_t *segment, stream_t *out)
{
    stream_t pipe_in, pipe_out;
    stream_init(&pipe_in);
    int status = 0;

    for (int i = 0; i < segment->nstages; i++) {
        sh_command_t *cmd = &segment->stages[i];
        stream_init(&pipe_out);

        /* Errors always go straight to the terminal, never into the pipe. */
        status = run_stage(sh, cmd, &pipe_in, &pipe_out, out);

        if (cmd->redirect != SH_REDIR_NONE) {
            fs_result_t r = fs_write_path(
                shell_cwd_node(sh), cmd->redirect_path, pipe_out.data,
                pipe_out.size, cmd->redirect == SH_REDIR_APPEND);
            if (r != FS_OK) {
                stream_printf(out, "tinos: %s: %s\n", cmd->redirect_path,
                              fs_strerror(r));
                status = 1;
            }
            stream_reset(&pipe_out);
        }

        if (i + 1 < segment->nstages) {
            stream_take(&pipe_in, &pipe_out);
            stream_free(&pipe_out);
        } else {
            stream_write(out, pipe_out.data, pipe_out.size);
            stream_free(&pipe_out);
        }
    }

    stream_free(&pipe_in);
    return status;
}

int shell_execute(shell_t *sh, const char *input, stream_t *out)
{
    history_add(sh, input);

    sh_line_t line;
    if (!sh_parse(input, &line)) {
        stream_printf(out, "tinos: %s\n", line.error);
        sh_line_free(&line);
        sh->last_status = 2;
        return 2;
    }

    int status = sh->last_status;
    for (int i = 0; i < line.nsegments; i++) {
        sh_segment_t *segment = &line.segments[i];
        if (segment->join == SH_JOIN_AND && status != 0)
            continue;
        if (segment->join == SH_JOIN_OR && status == 0)
            continue;
        status = run_pipeline(sh, segment, out);
    }

    sh_line_free(&line);
    sh->last_status = status;
    return status;
}

/* Completion */

bool shell_complete(shell_t *sh, const char *word, char *out, size_t out_size)
{
    const char *prefix = word;
    char        dir_part[FS_PATH_MAX];
    dir_part[0] = '\0';

    const char *slash = strrchr(word, '/');
    if (slash) {
        size_t n = (size_t)(slash - word) + 1;
        if (n >= sizeof(dir_part))
            return false;
        memcpy(dir_part, word, n);
        dir_part[n] = '\0';
        prefix      = slash + 1;
    }

    const char *match = NULL;
    size_t      matches = 0;
    size_t      prefix_len = strlen(prefix);

    /* Command names, but only for the first word of a line. */
    if (!slash) {
        size_t                  count;
        const sh_command_def_t *cmds = sh_commands(&count);
        for (size_t i = 0; i < count; i++) {
            if (strncmp(cmds[i].name, prefix, prefix_len) == 0) {
                match = cmds[i].name;
                matches++;
            }
        }
    }

    fs_node_t *dir = shell_cwd_node(sh);
    if (dir_part[0] && fs_resolve(dir, dir_part, &dir) != FS_OK)
        dir = NULL;

    if (dir && dir->type == FS_DIR) {
        for (fs_node_t *c = dir->children; c; c = c->next) {
            if (strncmp(c->name, prefix, prefix_len) == 0) {
                match = c->name;
                matches++;
            }
        }
    }

    if (matches != 1 || !match)
        return false;
    ksnprintf(out, out_size, "%s%s", dir_part, match);
    return true;
}
