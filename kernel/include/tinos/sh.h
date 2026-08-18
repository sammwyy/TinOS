/* Shell: command line parsing and execution over the in-memory filesystem.
 *
 * The shell has no notion of a terminal: commands read from and write to
 * byte streams, which is what makes pipelines, output redirection and the
 * host-side unit tests possible.
 */
#ifndef TINOS_SH_H
#define TINOS_SH_H

#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/types.h>

/* Growable byte stream */

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} stream_t;

void stream_init(stream_t *s);
void stream_free(stream_t *s);
void stream_reset(stream_t *s);
void stream_write(stream_t *s, const void *data, size_t len);
void stream_puts(stream_t *s, const char *text);
void stream_putc(stream_t *s, char c);
void stream_printf(stream_t *s, const char *fmt, ...) PRINTFLIKE(2, 3);
/* NUL terminated view of the contents (never NULL). */
const char *stream_text(stream_t *s);
void        stream_take(stream_t *dst, stream_t *src); /* move ownership */

/* Parsing */

typedef enum {
    SH_REDIR_NONE = 0,
    SH_REDIR_WRITE,  /* >  */
    SH_REDIR_APPEND, /* >> */
} sh_redirect_t;

typedef struct {
    char         **argv;     /* heap array, argv[argc] == NULL, grows as needed */
    int            argc;
    int            argv_cap;
    sh_redirect_t  redirect;
    char          *redirect_path;
    char          *input_path; /* < file */
} sh_command_t;

typedef enum {
    SH_JOIN_FIRST = 0, /* first segment of the line   */
    SH_JOIN_SEQ,       /* ";" or "&": always run      */
    SH_JOIN_AND,       /* "&&": run if previous ok    */
    SH_JOIN_OR,        /* "||": run if previous failed */
} sh_join_t;

typedef struct {
    sh_command_t *stages;    /* heap array, grows as needed */
    int           nstages;
    int           stages_cap;
    sh_join_t     join;
} sh_segment_t;

typedef struct {
    sh_segment_t *segments;  /* heap array, grows as needed */
    int           nsegments;
    int           segments_cap;
    char         *storage; /* backing buffer owned by the parsed line */
    char          error[96];
} sh_line_t;

/* Returns false and fills line->error on a syntax error. */
bool sh_parse(const char *input, sh_line_t *line);
void sh_line_free(sh_line_t *line);

/* Shell instance */

#define SH_HISTORY_MAX 64

typedef enum {
    SH_REQ_NONE = 0,
    SH_REQ_CLEAR,
    SH_REQ_EXIT,
    SH_REQ_EXPLORER,
    SH_REQ_EDITOR,
    SH_REQ_ABOUT,
    SH_REQ_SYSINFO,
    SH_REQ_REBOOT,
    SH_REQ_SHUTDOWN,
} sh_request_t;

typedef struct shell shell_t;

shell_t *shell_create(const char *initial_cwd);
void     shell_destroy(shell_t *sh);

/* Runs a whole command line, appending all output to `out`.
 * Returns the exit status of the last command executed. */
int shell_execute(shell_t *sh, const char *line, stream_t *out);

const char *shell_cwd(shell_t *sh);          /* absolute path string */
fs_node_t  *shell_cwd_node(shell_t *sh);     /* resolved, never NULL */
void        shell_set_cwd(shell_t *sh, const char *path);

const char *shell_previous_cwd(shell_t *sh); /* target of `cd -` */

/* UI requests raised by commands such as `clear`, `exit` or `explorer`. */
void         shell_request(shell_t *sh, sh_request_t request, const char *arg);
sh_request_t shell_take_request(shell_t *sh, char *arg, size_t arg_size);

/* Command history (index 0 is the oldest). */
size_t      shell_history_count(shell_t *sh);
const char *shell_history_at(shell_t *sh, size_t index);

/* Tab completion: fills `out` with the completed word, returns true when a
 * single unambiguous completion exists. */
bool shell_complete(shell_t *sh, const char *word, char *out, size_t out_size);

/* Command table (exposed for `help` and for the tests) */

typedef struct {
    const char *name;
    const char *usage;
    const char *summary;
    int (*fn)(shell_t *sh, int argc, char **argv, stream_t *in, stream_t *out,
              stream_t *err);
} sh_command_def_t;

const sh_command_def_t *sh_commands(size_t *count);
const sh_command_def_t *sh_find_command(const char *name);

/* System information provider (kernel and host test shim) */

typedef struct {
    uint32_t uptime_ms;
    char     datetime[40];
    char     cpu[64];
    char     platform[48];
    uint64_t mem_total;
    uint64_t mem_free;
    size_t   heap_total;
    size_t   heap_used;
    int      screen_width;
    int      screen_height;
} sys_info_t;

void sys_info_get(sys_info_t *out);

#endif /* TINOS_SH_H */
