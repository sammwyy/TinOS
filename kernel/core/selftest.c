/* In-kernel self tests.
 *
 * Run when the kernel command line contains "selftest" (the second GRUB menu
 * entry).  These exercise the same filesystem and shell code the host test
 * harness covers, but inside the real kernel with the real heap, which is
 * what catches allocator problems the host build cannot see.
 *
 * Results go to the serial log; the count of failures is returned so the
 * boot sequence can report it on screen.
 */

#include <tinos/arch.h>
#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/path.h>
#include <tinos/selftest.h>
#include <tinos/sh.h>
#include <tinos/string.h>

static int checks;
static int failures;

static void check(bool condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        klog("[test] FAIL %s\n", what);
    }
}

static void check_str(const char *actual, const char *expected,
                      const char *what)
{
    checks++;
    if (strcmp(actual, expected) != 0) {
        failures++;
        klog("[test] FAIL %s\n       expected \"%s\"\n       actual   \"%s\"\n",
             what, expected, actual);
    }
}

/* Run a command line and compare its complete output. */
static void check_command(shell_t *sh, const char *line, const char *expected)
{
    stream_t out;
    stream_init(&out);
    shell_execute(sh, line, &out);
    check_str(stream_text(&out), expected, line);
    stream_free(&out);
}

static void check_contains(shell_t *sh, const char *line, const char *needle)
{
    stream_t out;
    stream_init(&out);
    shell_execute(sh, line, &out);
    checks++;
    if (!strstr(stream_text(&out), needle)) {
        failures++;
        klog("[test] FAIL '%s' should mention '%s'\n       got: %s\n", line,
             needle, stream_text(&out));
    }
    stream_free(&out);
}

/* ------------------------------------------------------------------ */

static void test_heap(void)
{
    /* Allocate, write and free a range of sizes; the pattern check catches
     * blocks that overlap. */
    void  *blocks[64];
    size_t sizes[64];

    for (int i = 0; i < 64; i++) {
        sizes[i]  = (size_t)(8 + i * 37);
        blocks[i] = kmalloc(sizes[i]);
        check(blocks[i] != NULL, "kmalloc returns memory");
        if (blocks[i])
            memset(blocks[i], i + 1, sizes[i]);
    }
    for (int i = 0; i < 64; i++) {
        if (!blocks[i])
            continue;
        uint8_t *p  = blocks[i];
        bool     ok = true;
        for (size_t j = 0; j < sizes[i]; j++)
            if (p[j] != (uint8_t)(i + 1))
                ok = false;
        check(ok, "heap blocks do not overlap");
    }
    for (int i = 0; i < 64; i += 2)
        kfree(blocks[i]);
    for (int i = 1; i < 64; i += 2)
        kfree(blocks[i]);

    /* A large allocation forces a fresh arena from the page allocator. */
    void *big = kmalloc(3 * 1024 * 1024);
    check(big != NULL, "kmalloc can serve multi-megabyte requests");
    if (big) {
        memset(big, 0xA5, 3 * 1024 * 1024);
        kfree(big);
    }

    /* krealloc must preserve contents. */
    char *s = kmalloc(16);
    strcpy(s, "preserved");
    s = krealloc(s, 4096);
    check(s && strcmp(s, "preserved") == 0, "krealloc keeps the contents");
    kfree(s);
}

static void test_paths(void)
{
    char buf[128];

    path_normalize("/home/./documents/../documents", buf, sizeof(buf));
    check_str(buf, "/home/documents", "path_normalize collapses . and ..");
    path_join("/home", "../tmp/x", buf, sizeof(buf));
    check_str(buf, "/tmp/x", "path_join resolves relative paths");
    path_dirname("/a/b/c.txt", buf, sizeof(buf));
    check_str(buf, "/a/b", "path_dirname");
    check_str(path_basename("/a/b/c.txt"), "c.txt", "path_basename");
}

static void test_filesystem(void)
{
    fs_node_t *node = NULL;

    check(fs_resolve(fs_root(), "/home", &node) == FS_OK, "/home exists");
    check(fs_resolve(fs_root(), "/system/motd.txt", &node) == FS_OK,
          "/system/motd.txt exists");
    check(fs_resolve(fs_root(), "/nope", &node) == FS_ERR_NOENT,
          "missing paths report FS_ERR_NOENT");

    check(fs_mkdir_p(fs_root(), "/tmp/selftest/a/b", NULL) == FS_OK,
          "mkdir -p creates intermediate directories");
    check(fs_create(fs_root(), "/tmp/selftest/file.txt", &node) == FS_OK,
          "file creation");
    check(fs_write(node, "hello kernel", 12) == FS_OK, "file write");
    check(fs_size(node) == 12, "file size after write");

    check(fs_copy(fs_root(), "/tmp/selftest/file.txt", "/tmp/selftest/copy.txt",
                  false) == FS_OK,
          "file copy");
    fs_node_t *copy = NULL;
    check(fs_resolve(fs_root(), "/tmp/selftest/copy.txt", &copy) == FS_OK,
          "copy exists");
    check(copy && fs_size(copy) == 12, "copy preserves the contents");

    check(fs_move(fs_root(), "/tmp/selftest/copy.txt",
                  "/tmp/selftest/moved.txt") == FS_OK,
          "file rename through move");
    check(fs_resolve(fs_root(), "/tmp/selftest/copy.txt", NULL) ==
              FS_ERR_NOENT,
          "the old name is gone after a move");

    check(fs_remove(fs_root(), "/tmp/selftest", false) == FS_ERR_NOTEMPTY,
          "non-empty directories are not removed without -r");
    check(fs_remove(fs_root(), "/tmp/selftest", true) == FS_OK,
          "recursive remove");
    check(fs_resolve(fs_root(), "/tmp/selftest", NULL) == FS_ERR_NOENT,
          "the tree is gone afterwards");
}

static void test_shell(void)
{
    shell_t *sh = shell_create("/tmp");
    check(sh != NULL, "shell_create");
    if (!sh)
        return;

    check_command(sh, "pwd", "/tmp\n");
    check_command(sh, "echo hello", "hello\n");
    check_command(sh, "mkdir shtest", "");
    check_command(sh, "cd shtest", "");
    check_command(sh, "pwd", "/tmp/shtest\n");
    check_command(sh, "echo kernel test > notes.txt", "");
    check_command(sh, "cat notes.txt", "kernel test\n");
    check_command(sh, "cat notes.txt | grep kernel", "kernel test\n");
    check_command(sh, "cat notes.txt | grep missing", "");
    check_command(sh, "cp notes.txt backup.txt", "");
    check_command(sh, "mv backup.txt archive.txt", "");
    check_command(sh, "cat archive.txt", "kernel test\n");
    check_command(sh, "echo more >> notes.txt", "");
    check_command(sh, "cat notes.txt", "kernel test\nmore\n");
    check_command(sh, "wc -l < notes.txt", "       2\n");
    check_contains(sh, "ls", "archive.txt");
    check_command(sh, "rm archive.txt", "");
    check_contains(sh, "cat archive.txt", "no such file");
    check_contains(sh, "nosuchcommand", "command not found");
    check_command(sh, "echo a && echo b", "a\nb\n");
    check_command(sh, "cd /nope || echo recovered",
                  "cd: /nope: no such file or directory\nrecovered\n");
    check_command(sh, "cd /tmp", "");
    check_command(sh, "rm -r shtest", "");

    shell_destroy(sh);
}

int selftest_run(void)
{
    checks   = 0;
    failures = 0;

    klog("[test] ---- TinOS kernel self-test ----\n");
    uint64_t start = timer_ticks();

    test_heap();
    test_paths();
    test_filesystem();
    test_shell();

    uint32_t elapsed = (uint32_t)(timer_ticks() - start);
    klog("[test] %d checks, %d failures, %u ms\n", checks, failures, elapsed);
    klog("[test] ---- self-test %s ----\n", failures ? "FAILED" : "passed");
    return failures;
}
