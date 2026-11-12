#include <tinos/string.h>
#include <tinos/ui.h>

#define PATH_CAP 256
#define INPUT_CAP 128

static tinos_ui_t ui;
static char cwd[PATH_CAP] = "/";
static char input[INPUT_CAP];
static unsigned input_len;
static int foreground;

static void print(const char *text)
{
    tinos_ui_puts(&ui, text);
}

static void path_join(const char *name, char *path)
{
    if (name[0] == '/') {
        tinos_strlcpy(path, name, PATH_CAP);
    } else if (strcmp(name, "..") == 0) {
        tinos_strlcpy(path, cwd, PATH_CAP);
        unsigned length = strlen(path);
        while (length > 1 && path[length - 1] != '/')
            length--;
        path[length > 1 ? length - 1 : 1] = '\0';
    } else {
        tinos_strlcpy(path, cwd, PATH_CAP);
        unsigned length = strlen(path);
        if (length > 1 && length + 1 < PATH_CAP)
            path[length++] = '/';
        tinos_strlcpy(path + length, name, PATH_CAP - length);
    }
}

static void prompt(void)
{
    print("tinos:");
    print(cwd);
    print("$ ");
    print(input);
    tinos_ui_present(&ui);
}

static void list_dir(const char *path)
{
    tinos_dirent_t entry;
    for (unsigned i = 0; i < 128; i++) {
        int result = tinos_readdir(path, i, &entry);
        if (result == 0)
            return;
        if (result < 0) {
            print("ls: cannot open directory\n");
            return;
        }
        print(entry.name);
        if (entry.type == TINOS_DIRENT_DIR)
            print("/");
        print("\n");
    }
}

static void read_file(const char *path)
{
    int fd = tinos_open(path);
    if (fd < 0) {
        print("cat: cannot open file\n");
        return;
    }
    char buffer[256];
    int count;
    while ((count = tinos_read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        for (int i = 0; i < count; i++)
            if (buffer[i] < 32 && buffer[i] != '\n')
                buffer[i] = ' ';
        buffer[count] = '\0';
        print(buffer);
    }
    tinos_close(fd);
    print("\n");
}

static int execute(char *command)
{
    char *args[8];
    unsigned argc = 0;
    while (*command && argc < 8) {
        while (*command == ' ')
            command++;
        if (!*command)
            break;
        args[argc++] = command;
        while (*command && *command != ' ')
            command++;
        if (*command)
            *command++ = '\0';
    }
    if (!argc)
        return 0;
    char path[PATH_CAP];
    if (strcmp(args[0], "help") == 0)
        print("help ls cd pwd cat echo touch mkdir rm edit view play run clear exit\n");
    else if (strcmp(args[0], "pwd") == 0) {
        print(cwd);
        print("\n");
    } else if (strcmp(args[0], "ls") == 0) {
        path_join(argc > 1 ? args[1] : ".", path);
        list_dir(path);
    } else if (strcmp(args[0], "cd") == 0) {
        path_join(argc > 1 ? args[1] : "/", path);
        tinos_dirent_t entry;
        if (tinos_stat(path, &entry) == 1 && entry.type == TINOS_DIRENT_DIR)
            tinos_strlcpy(cwd, path, sizeof(cwd));
        else
            print("cd: not a directory\n");
    } else if (strcmp(args[0], "cat") == 0 && argc > 1) {
        path_join(args[1], path);
        read_file(path);
    } else if (strcmp(args[0], "echo") == 0) {
        unsigned end = argc;
        int fd = -1;
        for (unsigned i = 1; i + 1 < argc; i++) {
            if (strcmp(args[i], ">") != 0)
                continue;
            path_join(args[i + 1], path);
            fd = tinos_open_flags(path, TINOS_O_WRONLY |
                                        TINOS_O_CREAT | TINOS_O_TRUNC);
            end = i;
            break;
        }
        if (end != argc && fd < 0)
            print("echo: cannot create file\n");
        for (unsigned i = 1; i < end; i++) {
            if (i != 1) {
                if (fd >= 0)
                    tinos_write(fd, " ", 1);
                else if (end == argc)
                    print(" ");
            }
            if (fd >= 0)
                tinos_write(fd, args[i], strlen(args[i]));
            else if (end == argc)
                print(args[i]);
        }
        if (fd >= 0) {
            tinos_write(fd, "\n", 1);
            tinos_close(fd);
        } else if (end == argc) {
            print("\n");
        }
    } else if (strcmp(args[0], "touch") == 0 && argc > 1) {
        path_join(args[1], path);
        int fd = tinos_open_flags(path, TINOS_O_WRONLY | TINOS_O_CREAT);
        if (fd < 0)
            print("touch: cannot create file\n");
        else
            tinos_close(fd);
    } else if (strcmp(args[0], "mkdir") == 0 && argc > 1) {
        path_join(args[1], path);
        if (tinos_mkdir(path) < 0)
            print("mkdir: cannot create directory\n");
    } else if (strcmp(args[0], "rm") == 0 && argc > 1) {
        path_join(args[1], path);
        if (tinos_unlink(path) < 0)
            print("rm: cannot remove path\n");
    } else if (strcmp(args[0], "edit") == 0 && argc > 1) {
        path_join(args[1], path);
        const char *argv[] = { "/apps/gui/editor.elf", path };
        if (tinos_spawn(argv[0], 2, argv) < 0)
            print("edit: cannot start editor\n");
    } else if (strcmp(args[0], "view") == 0 && argc > 1) {
        path_join(args[1], path);
        const char *argv[] = { "/apps/gui/image_viewer.elf", path };
        if (tinos_spawn(argv[0], 2, argv) < 0)
            print("view: cannot start image viewer\n");
    } else if (strcmp(args[0], "play") == 0 && argc > 1) {
        path_join(args[1], path);
        const char *argv[] = { "/apps/gui/media_player.elf", path };
        if (tinos_spawn(argv[0], 2, argv) < 0)
            print("play: cannot start media player\n");
    } else if (strcmp(args[0], "clear") == 0) {
        tinos_ui_clear(&ui);
    } else if (strcmp(args[0], "run") == 0 && argc > 1) {
        path_join(args[1], path);
        for (unsigned i = 1; i + 1 < argc; i++)
            args[i] = args[i + 1];
        args[0] = path;
        int pid = tinos_spawn(path, argc - 1, (const char *const *)args);
        if (pid < 0)
            print("run: failed to start\n");
        else
            foreground = pid;
    } else if (strcmp(args[0], "exit") == 0) {
        return 1;
    } else {
        print("unknown command (try help)\n");
    }
    return 0;
}

int main(void)
{
    if (tinos_ui_open(&ui, "Terminal", 500, 320) < 0)
        return 1;
    if (tinos_gui_console_attach(ui.handle) < 0)
        return 1;
    print("TinOS terminal - type help for commands\n");
    prompt();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE)
            break;
        if (event.type == TINOS_GUI_OUTPUT) {
            char buffer[1025];
            int count;
            while ((count = tinos_gui_console_read(ui.handle, buffer, 1024)) > 0) {
                for (int i = 0; i < count; i++)
                    if (buffer[i] < 32 && buffer[i] != '\n')
                        buffer[i] = ' ';
                buffer[count] = '\0';
                print(buffer);
            }
            tinos_ui_present(&ui);
            continue;
        }
        if (event.type == TINOS_GUI_CHILD_EXIT) {
            int pid = (int)event.code;
            tinos_wait(pid, 0);
            if (pid == foreground) {
                foreground = 0;
                print("\n");
                prompt();
            }
            continue;
        }
        if (event.type != TINOS_GUI_KEY)
            continue;
        if (foreground) {
            if ((event.mods & TINOS_MOD_CTRL) &&
                (event.ch == 'c' || event.ch == 'C'))
                tinos_kill(foreground);
            else if (event.code == TINOS_KEY_ENTER)
                tinos_tty_send(foreground, '\n');
            else if (event.code == TINOS_KEY_BACKSPACE)
                tinos_tty_send(foreground, '\b');
            else if (event.ch >= 32 && event.ch < 127)
                tinos_tty_send(foreground, event.ch);
            continue;
        }
        if (event.code == TINOS_KEY_ENTER) {
            print("\n");
            int exit_requested = execute(input);
            if (exit_requested)
                break;
            input_len = 0;
            input[0] = '\0';
            if (foreground)
                tinos_ui_present(&ui);
            else
                prompt();
        } else if (event.code == TINOS_KEY_BACKSPACE && input_len) {
            input[--input_len] = '\0';
            if (ui.x) {
                ui.cells[ui.y * ui.columns + --ui.x] = ' ';
                tinos_ui_present(&ui);
            }
        } else if (event.ch >= 32 && event.ch < 127 &&
                   input_len + 1 < INPUT_CAP) {
            input[input_len++] = event.ch;
            input[input_len] = '\0';
            char one[] = { (char)event.ch, '\0' };
            print(one);
            tinos_ui_present(&ui);
        }
    }
    if (foreground)
        tinos_kill(foreground);
    tinos_gui_close(ui.handle);
    return 0;
}
