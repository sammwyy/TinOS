/* Host-side unit tests for the portable parts of TinOS: path handling, the
 * in-memory filesystem, the shell parser and the built-in commands.
 *
 * Built and run with `make test`; compiled with AddressSanitizer so that
 * memory errors in the filesystem show up here rather than in the kernel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinos/fs.h>
#include <tinos/codec.h>
#include <tinos/path.h>
#include <tinos/sh.h>

static int checks;
static int failures;
static const char *current_suite = "";

#define CHECK(cond)                                                           \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("  FAIL %s:%d in %s: %s\n", __FILE__, __LINE__,            \
                   current_suite, #cond);                                     \
        }                                                                     \
    } while (0)

#define CHECK_STR(actual, expected)                                           \
    do {                                                                      \
        checks++;                                                             \
        const char *a_ = (actual);                                            \
        const char *e_ = (expected);                                          \
        if (strcmp(a_, e_) != 0) {                                            \
            failures++;                                                       \
            printf("  FAIL %s:%d in %s:\n    expected: %s\n    actual  : %s\n",\
                   __FILE__, __LINE__, current_suite, e_, a_);                \
        }                                                                     \
    } while (0)

#define SUITE(name)                                                           \
    current_suite = name;                                                     \
    printf("- %s\n", name)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Run a command line in a fresh-ish shell and return its output. The caller
 * must free the returned string. */
static char *run(shell_t *sh, const char *line, int *status_out)
{
    stream_t out;
    stream_init(&out);
    int status = shell_execute(sh, line, &out);
    if (status_out)
        *status_out = status;
    char *text = strdup(stream_text(&out));
    stream_free(&out);
    return text;
}

static void check_output(shell_t *sh, const char *line, const char *expected)
{
    char *text = run(sh, line, NULL);
    checks++;
    if (strcmp(text, expected) != 0) {
        failures++;
        printf("  FAIL command '%s' in %s:\n    expected: %s\n    actual  : %s\n",
               line, current_suite, expected, text);
    }
    free(text);
}

static bool output_contains(shell_t *sh, const char *line, const char *needle)
{
    char *text = run(sh, line, NULL);
    bool  hit  = strstr(text, needle) != NULL;
    if (!hit)
        printf("  (output of '%s' was: %s)\n", line, text);
    free(text);
    return hit;
}

/* ------------------------------------------------------------------ */
/* Path normalisation                                                  */
/* ------------------------------------------------------------------ */

static void test_paths(void)
{
    SUITE("path normalisation");
    char buf[256];

    path_normalize("/", buf, sizeof(buf));
    CHECK_STR(buf, "/");
    path_normalize("/home", buf, sizeof(buf));
    CHECK_STR(buf, "/home");
    path_normalize("/home/", buf, sizeof(buf));
    CHECK_STR(buf, "/home");
    path_normalize("//home///user//", buf, sizeof(buf));
    CHECK_STR(buf, "/home/user");
    path_normalize("/home/./user/../other", buf, sizeof(buf));
    CHECK_STR(buf, "/home/other");
    path_normalize("/..", buf, sizeof(buf));
    CHECK_STR(buf, "/");
    path_normalize("/a/b/c/../../..", buf, sizeof(buf));
    CHECK_STR(buf, "/");
    path_normalize("/a/../../b", buf, sizeof(buf));
    CHECK_STR(buf, "/b");

    path_join("/home", "documents", buf, sizeof(buf));
    CHECK_STR(buf, "/home/documents");
    path_join("/home/documents", "..", buf, sizeof(buf));
    CHECK_STR(buf, "/home");
    path_join("/home", "/tmp/x", buf, sizeof(buf));
    CHECK_STR(buf, "/tmp/x");
    path_join("/", "a/b/../c", buf, sizeof(buf));
    CHECK_STR(buf, "/a/c");

    CHECK_STR(path_basename("/home/user/notes.txt"), "notes.txt");
    CHECK_STR(path_basename("notes.txt"), "notes.txt");
    CHECK_STR(path_basename("/"), "/");

    path_dirname("/home/user/notes.txt", buf, sizeof(buf));
    CHECK_STR(buf, "/home/user");
    path_dirname("/notes.txt", buf, sizeof(buf));
    CHECK_STR(buf, "/");

    CHECK(path_has_suffix("readme.TXT", ".txt"));
    CHECK(!path_has_suffix("readme", ".txt"));
}

/* ------------------------------------------------------------------ */
/* Filesystem                                                          */
/* ------------------------------------------------------------------ */

static void test_fs_basics(void)
{
    SUITE("filesystem basics");
    fs_init();

    fs_node_t *root = fs_root();
    CHECK(root != NULL);
    CHECK(root->type == FS_DIR);
    CHECK(fs_child(root, "users") != NULL);
    CHECK(fs_child(root, "system") != NULL);
    CHECK(fs_child(root, "tmp") != NULL);
    CHECK(fs_child(root, "README.txt") != NULL);
    CHECK(fs_child(root, "nope") == NULL);

    char buf[FS_PATH_MAX];
    fs_path(root, buf, sizeof(buf));
    CHECK_STR(buf, "/");

    fs_node_t *node = NULL;
    CHECK(fs_resolve(root, TINOS_DEFAULT_HOME, &node) == FS_OK);
    fs_path(node, buf, sizeof(buf));
    CHECK_STR(buf, TINOS_DEFAULT_HOME);

    CHECK(fs_resolve(root, TINOS_DEFAULT_HOME "/documents", &node) == FS_OK);
    fs_path(node, buf, sizeof(buf));
    CHECK_STR(buf, TINOS_DEFAULT_HOME "/documents");
    CHECK(node->type == FS_DIR);

    /* Relative resolution and parent traversal. */
    fs_node_t *home = NULL;
    CHECK(fs_resolve(root, "users/sammy", &home) == FS_OK);
    CHECK(fs_resolve(home, "documents/../documents", &node) == FS_OK);
    CHECK(fs_resolve(home, "../../users/./sammy/documents", &node) == FS_OK);
    CHECK(fs_resolve(home, "..", &node) == FS_OK &&
          node == fs_child(root, "users"));
    CHECK(fs_resolve(root, "..", &node) == FS_OK && node == root);
    CHECK(fs_resolve(root, "/missing", &node) == FS_ERR_NOENT);
    CHECK(fs_resolve(root, "/README.txt/x", &node) == FS_ERR_NOTDIR);

    fs_shutdown();
}

static void test_fs_mutation(void)
{
    SUITE("filesystem mutation");
    fs_init();
    fs_node_t *root = fs_root();
    fs_node_t *node = NULL;

    CHECK(fs_mkdir(root, "/work", &node) == FS_OK);
    CHECK(fs_mkdir(root, "/work", NULL) == FS_ERR_EXIST);
    CHECK(fs_mkdir(root, "/a/b/c", NULL) == FS_ERR_NOENT);
    CHECK(fs_mkdir_p(root, "/a/b/c", &node) == FS_OK);
    CHECK(fs_resolve(root, "/a/b/c", &node) == FS_OK);
    CHECK(fs_mkdir_p(root, "/a/b/c", NULL) == FS_OK); /* idempotent */

    CHECK(fs_create(root, "/work/notes.txt", &node) == FS_OK);
    CHECK(fs_write(node, "hello", 5) == FS_OK);
    CHECK(fs_size(node) == 5);
    CHECK(memcmp(fs_data(node), "hello", 5) == 0);
    CHECK(fs_append(node, " world", 6) == FS_OK);
    CHECK(fs_size(node) == 11);
    CHECK(memcmp(fs_data(node), "hello world", 11) == 0);

    /* Large content forces the buffer to grow repeatedly. */
    char big[5000];
    memset(big, 'x', sizeof(big));
    CHECK(fs_write(node, big, sizeof(big)) == FS_OK);
    CHECK(fs_size(node) == sizeof(big));
    CHECK(fs_write(node, "small", 5) == FS_OK);
    CHECK(fs_size(node) == 5);

    /* Rename keeps contents and ordering. */
    CHECK(fs_rename(node, "renamed.txt") == FS_OK);
    CHECK(fs_resolve(root, "/work/renamed.txt", &node) == FS_OK);
    CHECK(fs_size(node) == 5);
    CHECK(fs_resolve(root, "/work/notes.txt", NULL) == FS_ERR_NOENT);
    CHECK(fs_rename(fs_root(), "x") == FS_ERR_ROOT);
    CHECK(fs_rename(node, "bad/name") == FS_ERR_INVAL);

    /* Copy duplicates content, not the node. */
    CHECK(fs_copy(root, "/work/renamed.txt", "/work/copy.txt", false) == FS_OK);
    fs_node_t *copy = NULL;
    CHECK(fs_resolve(root, "/work/copy.txt", &copy) == FS_OK);
    CHECK(copy != node);
    CHECK(fs_size(copy) == 5);
    CHECK(fs_write(copy, "changed!", 8) == FS_OK);
    CHECK(fs_size(node) == 5); /* the original is untouched */

    /* Copying a directory needs -r semantics. */
    CHECK(fs_copy(root, "/work", "/work2", false) == FS_ERR_ISDIR);
    CHECK(fs_copy(root, "/work", "/work2", true) == FS_OK);
    CHECK(fs_resolve(root, "/work2/renamed.txt", &node) == FS_OK);
    CHECK(fs_copy(root, "/work", "/work/inner", true) == FS_ERR_LOOP);

    /* Move into a directory and onto a new name. */
    CHECK(fs_move(root, "/work2/copy.txt", "/tmp") == FS_OK);
    CHECK(fs_resolve(root, "/tmp/copy.txt", &node) == FS_OK);
    CHECK(fs_move(root, "/tmp/copy.txt", "/tmp/moved.txt") == FS_OK);
    CHECK(fs_resolve(root, "/tmp/moved.txt", &node) == FS_OK);
    CHECK(fs_move(root, "/work", "/work/sub") == FS_ERR_LOOP);
    CHECK(fs_move(root, "/nope", "/tmp") == FS_ERR_NOENT);

    /* Removal rules. */
    CHECK(fs_remove(root, "/work2", false) == FS_ERR_NOTEMPTY);
    CHECK(fs_remove(root, "/work2", true) == FS_OK);
    CHECK(fs_resolve(root, "/work2", NULL) == FS_ERR_NOENT);
    CHECK(fs_remove(root, "/tmp/moved.txt", false) == FS_OK);
    CHECK(fs_remove(root, "/", true) == FS_ERR_ROOT);
    CHECK(fs_remove(root, "/nope", false) == FS_ERR_NOENT);

    /* Children stay sorted by name. */
    fs_mkdir(root, "/sorted", NULL);
    fs_create(root, "/sorted/zebra", NULL);
    fs_create(root, "/sorted/apple", NULL);
    fs_create(root, "/sorted/mango", NULL);
    fs_node_t *dir = NULL;
    fs_resolve(root, "/sorted", &dir);
    CHECK_STR(dir->children->name, "apple");
    CHECK_STR(dir->children->next->name, "mango");
    CHECK_STR(dir->children->next->next->name, "zebra");
    CHECK(fs_child_count(dir) == 3);

    fs_shutdown();
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

static void test_parser(void)
{
    SUITE("shell parser");
    sh_line_t line;

    CHECK(sh_parse("echo hello world", &line));
    CHECK(line.nsegments == 1);
    CHECK(line.segments[0].nstages == 1);
    CHECK(line.segments[0].stages[0].argc == 3);
    CHECK_STR(line.segments[0].stages[0].argv[0], "echo");
    CHECK_STR(line.segments[0].stages[0].argv[2], "world");
    sh_line_free(&line);

    CHECK(sh_parse("   ", &line));
    CHECK(line.nsegments == 0);
    sh_line_free(&line);

    CHECK(sh_parse("echo 'quoted string' \"double quoted\"", &line));
    CHECK(line.segments[0].stages[0].argc == 3);
    CHECK_STR(line.segments[0].stages[0].argv[1], "quoted string");
    CHECK_STR(line.segments[0].stages[0].argv[2], "double quoted");
    sh_line_free(&line);

    CHECK(sh_parse("echo a\\ b", &line));
    CHECK(line.segments[0].stages[0].argc == 2);
    CHECK_STR(line.segments[0].stages[0].argv[1], "a b");
    sh_line_free(&line);

    CHECK(sh_parse("cat file.txt | grep needle | wc -l", &line));
    CHECK(line.nsegments == 1);
    CHECK(line.segments[0].nstages == 3);
    CHECK_STR(line.segments[0].stages[2].argv[0], "wc");
    sh_line_free(&line);

    CHECK(sh_parse("echo hi > out.txt", &line));
    CHECK(line.segments[0].stages[0].redirect == SH_REDIR_WRITE);
    CHECK_STR(line.segments[0].stages[0].redirect_path, "out.txt");
    CHECK(line.segments[0].stages[0].argc == 2);
    sh_line_free(&line);

    CHECK(sh_parse("echo hi>>out.txt", &line));
    CHECK(line.segments[0].stages[0].redirect == SH_REDIR_APPEND);
    CHECK_STR(line.segments[0].stages[0].redirect_path, "out.txt");
    sh_line_free(&line);

    CHECK(sh_parse("grep x < in.txt", &line));
    CHECK_STR(line.segments[0].stages[0].input_path, "in.txt");
    sh_line_free(&line);

    CHECK(sh_parse("ls & pwd ; echo done", &line));
    CHECK(line.nsegments == 3);
    CHECK(line.segments[1].join == SH_JOIN_SEQ);
    sh_line_free(&line);

    CHECK(sh_parse("false && echo yes || echo no", &line));
    CHECK(line.nsegments == 3);
    CHECK(line.segments[1].join == SH_JOIN_AND);
    CHECK(line.segments[2].join == SH_JOIN_OR);
    sh_line_free(&line);

    CHECK(sh_parse("cd ~/documents", &line));
    CHECK_STR(line.segments[0].stages[0].argv[1], TINOS_DEFAULT_HOME "/documents");
    sh_line_free(&line);

    CHECK(sh_parse("echo ~", &line));
    CHECK_STR(line.segments[0].stages[0].argv[1], TINOS_DEFAULT_HOME);
    sh_line_free(&line);

    /* Syntax errors are reported, not crashed on. */
    CHECK(!sh_parse("echo 'unterminated", &line));
    CHECK(strlen(line.error) > 0);
    sh_line_free(&line);

    CHECK(!sh_parse("echo hello >", &line));
    sh_line_free(&line);

    CHECK(!sh_parse("| grep x", &line));
    sh_line_free(&line);

    CHECK(!sh_parse("ls |", &line));
    sh_line_free(&line);

    CHECK(sh_parse("ls &", &line)); /* trailing separator is fine */
    sh_line_free(&line);
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void test_commands(void)
{
    SUITE("shell commands");
    fs_init();
    shell_t *sh = shell_create(TINOS_DEFAULT_HOME);
    CHECK(sh != NULL);

    check_output(sh, "pwd", TINOS_DEFAULT_HOME "\n");
    check_output(sh, "echo hello", "hello\n");
    check_output(sh, "beep 440 1", "beep: 440 Hz for 1 ms\n");
    check_output(sh, "doremi", "doremi: AC'97 audio test unavailable\n");
    check_output(sh, "echo -n hello", "hello");
    check_output(sh, "echo   spaced   out", "spaced out\n");

    /* Navigation. */
    check_output(sh, "cd /", "");
    check_output(sh, "pwd", "/\n");
    check_output(sh, "cd users/sammy", "");
    check_output(sh, "pwd", TINOS_DEFAULT_HOME "\n");
    check_output(sh, "cd ../..", "");
    check_output(sh, "pwd", "/\n");
    CHECK(output_contains(sh, "cd /nowhere", "no such file"));
    CHECK(output_contains(sh, "cd /README.txt", "not a directory"));
    check_output(sh, "cd " TINOS_DEFAULT_HOME, "");
    check_output(sh, "cd -", "");
    check_output(sh, "pwd", "/\n");

    /* Redirection and file contents. */
    check_output(sh, "cd /tmp", "");
    check_output(sh, "echo hello > greeting.txt", "");
    check_output(sh, "cat greeting.txt", "hello\n");
    check_output(sh, "echo again >> greeting.txt", "");
    check_output(sh, "cat greeting.txt", "hello\nagain\n");
    check_output(sh, "echo replaced > greeting.txt", "");
    check_output(sh, "cat greeting.txt", "replaced\n");

    /* Pipelines. */
    check_output(sh, "echo one two three | wc -w", "       3\n");
    check_output(sh, "cat greeting.txt | grep replaced", "replaced\n");
    check_output(sh, "cat greeting.txt | grep nothing", "");
    check_output(sh, "echo alpha | grep -c alpha", "1\n");

    /* Unknown commands report an error and a non-zero status. */
    int   status = 0;
    char *text   = run(sh, "definitely-not-a-command", &status);
    CHECK(status == 127);
    CHECK(strstr(text, "command not found") != NULL);
    free(text);

    /* The full example sequence from the project brief. */
    check_output(sh, "cd " TINOS_DEFAULT_HOME, "");
    check_output(sh, "mkdir projects", "");
    check_output(sh, "cd projects", "");
    check_output(sh, "touch notes.txt", "");
    check_output(sh, "echo kernel test > notes.txt", "");
    check_output(sh, "cat notes.txt", "kernel test\n");
    check_output(sh, "cp notes.txt backup.txt", "");
    check_output(sh, "cat backup.txt", "kernel test\n");
    check_output(sh, "mv backup.txt archive.txt", "");
    check_output(sh, "cat archive.txt", "kernel test\n");
    CHECK(output_contains(sh, "ls", "archive.txt"));
    check_output(sh, "rm archive.txt", "");
    CHECK(!output_contains(sh, "ls", "archive.txt"));

    /* grep options. */
    check_output(sh, "echo Hello > case.txt", "");
    check_output(sh, "grep -i hello case.txt", "Hello\n");
    check_output(sh, "grep hello case.txt", "");
    check_output(sh, "grep -n Hello case.txt", "1:Hello\n");
    check_output(sh, "grep -v Hello case.txt", "");

    /* grep regex support: '.', '*', '+', '?', '^', '$' and [classes]. */
    check_output(sh, "echo cat1 > animals.txt", "");
    check_output(sh, "echo dog2 >> animals.txt", "");
    check_output(sh, "echo bird3 >> animals.txt", "");
    check_output(sh, "grep '^cat' animals.txt", "cat1\n");
    check_output(sh, "grep '3$' animals.txt", "bird3\n");
    check_output(sh, "grep 'c.t' animals.txt", "cat1\n");
    check_output(sh, "grep 'ca+t' animals.txt", "cat1\n");
    check_output(sh, "grep 'cats?1' animals.txt", "cat1\n");
    check_output(sh, "grep '^[bcd]' animals.txt",
                 "cat1\ndog2\nbird3\n");
    check_output(sh, "grep '^[^cd]' animals.txt", "bird3\n");
    check_output(sh, "grep '[a-c]at' animals.txt", "cat1\n");

    /* Parser limits grow instead of truncating: past the old hardcoded caps
     * (32 args, 8 pipeline stages, 8 ';'-separated commands), all still
     * work. */
    {
        char big[4096];
        char *p = big;
        p += sprintf(p, "echo");
        for (int i = 0; i < 60; i++)
            p += sprintf(p, " w%d", i);
        sh_line_t line;
        CHECK(sh_parse(big, &line));
        CHECK(line.segments[0].stages[0].argc == 61);
        sh_line_free(&line);

        p = big;
        p += sprintf(p, "cat");
        for (int i = 0; i < 20; i++)
            p += sprintf(p, " | cat");
        CHECK(sh_parse(big, &line));
        CHECK(line.segments[0].nstages == 21);
        sh_line_free(&line);

        p = big;
        p += sprintf(p, "pwd");
        for (int i = 0; i < 20; i++)
            p += sprintf(p, " ; pwd");
        CHECK(sh_parse(big, &line));
        CHECK(line.nsegments == 21);
        sh_line_free(&line);
    }

    /* Error reporting. */
    CHECK(output_contains(sh, "cat missing.txt", "no such file"));
    CHECK(output_contains(sh, "cat " TINOS_DEFAULT_HOME, "is a directory"));
    CHECK(output_contains(sh, "mkdir", "missing operand"));
    CHECK(output_contains(sh, "cp onlyone", "missing destination"));
    CHECK(output_contains(sh, "rm " TINOS_DEFAULT_HOME, "is a directory"));
    CHECK(output_contains(sh, "ls -Z", "unknown option"));
    CHECK(output_contains(sh, "mkdir " TINOS_DEFAULT_HOME "/projects", "file exists"));
    CHECK(output_contains(sh, "mv /nope /tmp", "no such file"));

    /* Sequencing operators. */
    check_output(sh, "echo a & echo b", "a\nb\n");
    check_output(sh, "echo a && echo b", "a\nb\n");
    check_output(sh, "cd /nope || echo recovered",
                 "cd: /nope: no such file or directory\nrecovered\n");

    /* Directory copy and recursive remove. */
    check_output(sh, "cd /tmp", "");
    check_output(sh, "mkdir -p deep/a/b", "");
    check_output(sh, "echo x > deep/a/b/file.txt", "");
    check_output(sh, "cp -r deep deep2", "");
    check_output(sh, "cat deep2/a/b/file.txt", "x\n");
    CHECK(output_contains(sh, "rm deep2", "is a directory"));
    check_output(sh, "rm -r deep2", "");
    CHECK(output_contains(sh, "ls", "deep/"));

    /* Working directory survives having its directory deleted. */
    check_output(sh, "mkdir /tmp/doomed", "");
    check_output(sh, "cd /tmp/doomed", "");
    CHECK(fs_remove(fs_root(), "/tmp/doomed", true) == FS_OK);
    check_output(sh, "pwd", "/tmp\n");

    /* Requests raised for the desktop. */
    check_output(sh, "clear", "");
    char arg[FS_PATH_MAX];
    CHECK(shell_take_request(sh, arg, sizeof(arg)) == SH_REQ_CLEAR);
    CHECK(shell_take_request(sh, arg, sizeof(arg)) == SH_REQ_NONE);

    /* History. */
    CHECK(shell_history_count(sh) > 10);
    CHECK(output_contains(sh, "history", "pwd"));

    /* Completion. */
    char completed[FS_PATH_MAX];
    check_output(sh, "cd " TINOS_DEFAULT_HOME, "");
    CHECK(shell_complete(sh, "proj", completed, sizeof(completed)));
    CHECK_STR(completed, "projects");
    CHECK(shell_complete(sh, "hist", completed, sizeof(completed)));
    CHECK_STR(completed, "history");

    shell_destroy(sh);
    fs_shutdown();
}

static void test_shared_state(void)
{
    SUITE("shared filesystem state");
    fs_init();

    /* Two shells (as two terminal windows would be) see each other's work. */
    shell_t *a = shell_create(TINOS_DEFAULT_HOME);
    shell_t *b = shell_create("/");

    check_output(a, "mkdir " TINOS_DEFAULT_HOME "/shared", "");
    check_output(a, "echo from-a > " TINOS_DEFAULT_HOME "/shared/file.txt", "");
    check_output(b, "cat " TINOS_DEFAULT_HOME "/shared/file.txt", "from-a\n");
    check_output(b, "echo from-b >> " TINOS_DEFAULT_HOME "/shared/file.txt", "");
    check_output(a, "cat " TINOS_DEFAULT_HOME "/shared/file.txt", "from-a\nfrom-b\n");

    /* And the same nodes are what a file explorer would walk. */
    fs_node_t *node = NULL;
    CHECK(fs_resolve(fs_root(), TINOS_DEFAULT_HOME "/shared/file.txt", &node) == FS_OK);
    CHECK(fs_size(node) == strlen("from-a\nfrom-b\n"));

    shell_destroy(a);
    shell_destroy(b);
    fs_shutdown();
}

/* Feed the parser and the commands a pile of hostile input; the point is
 * simply that nothing crashes or corrupts the tree. */
static void test_robustness(void)
{
    SUITE("robustness");
    fs_init();
    shell_t *sh = shell_create("/");

    static const char *nasty[] = {
        "", " ", "\t", "|", "||", "&&", ">", ">>", "<", "&", ";",
        "echo >", "echo <", "> file", "cat |", "| cat", "cd ../../../..",
        "ls ////", "cd .", "cd ..", "cat .", "rm -r /", "rm /",
        "mkdir ''", "touch ''", "cp", "mv", "grep", "echo 'unclosed",
        "echo \"unclosed", "ls -", "ls --", "cd /home/../../home",
        "mkdir a && cd a && mkdir b && cd b && cd ../.. && rm -r a",
        "echo x > /", "cat /", "mv / /tmp", "cp / /tmp",
        "echo a | echo b | echo c | echo d | echo e | echo f | echo g",
        "touch \"name with spaces.txt\"", "ls", "rm \"name with spaces.txt\"",
        "echo $HOME", "echo ~", "echo ~root", "ls ~",
        "echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };

    for (size_t i = 0; i < sizeof(nasty) / sizeof(nasty[0]); i++) {
        char *text = run(sh, nasty[i], NULL);
        free(text);
    }

    /* The tree must still be intact and usable. */
    CHECK(fs_root() != NULL);
    CHECK(fs_child(fs_root(), "users") != NULL);
    check_output(sh, "cd " TINOS_DEFAULT_HOME, "");
    check_output(sh, "echo still-works > /tmp/ok.txt", "");
    check_output(sh, "cat /tmp/ok.txt", "still-works\n");

    /* Deep nesting and long names. */
    for (int i = 0; i < 40; i++)
        check_output(sh, "mkdir -p /tmp/deep/a/a/a/a/a/a/a/a/a/a", "");
    char longname[300];
    memset(longname, 'n', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "touch /tmp/%s", longname);
    CHECK(output_contains(sh, cmd, "name too long"));

    shell_destroy(sh);
    fs_shutdown();
}

static void test_audio_codecs(void)
{
    SUITE("audio codecs");
    static const uint8_t wav[] = {
        'R','I','F','F', 36,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x44,0xac,0,0,
        0x88,0x58,1,0, 2,0, 16,0,
        'd','a','t','a', 2,0,0,0, 0,0,
    };
    static const uint8_t bad[] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E' };
    audio_codecs_init();
    CHECK(audio_codec_for("tone.wav", wav, sizeof(wav)) != NULL);
    CHECK(audio_codec_for("tone.wav", bad, sizeof(bad)) != NULL);
    audio_format_t format;
    uint64_t frames;
    const audio_codec_t *codec = NULL;
    audio_decoder_t *decoder = audio_decoder_open("tone.wav", wav, sizeof(wav),
                                                   &format, &frames, &codec);
    CHECK(decoder != NULL);
    CHECK(codec != NULL && strcmp(codec->name, "wav") == 0);
    CHECK(format.sample_rate == 44100 && format.channels == 2 && frames == 1);
    int16_t samples[2] = { 1, 1 };
    CHECK(audio_decoder_read(decoder, samples, 1) == 1);
    CHECK(samples[0] == 0 && samples[1] == 0);
    audio_decoder_close(decoder);
    CHECK(audio_decoder_open("bad.wav", bad, sizeof(bad), &format, &frames,
                             &codec) == NULL);
}

int main(void)
{
    printf("TinOS host unit tests\n\n");

    test_paths();
    test_fs_basics();
    test_fs_mutation();
    test_parser();
    test_commands();
    test_shared_state();
    test_robustness();
    test_audio_codecs();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
