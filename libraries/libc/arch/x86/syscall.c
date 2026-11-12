#include <tinos/libc.h>
#include <tinos/syscall.h>

static int invoke(unsigned number, unsigned a, unsigned b, unsigned c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

static int invoke4(unsigned number, unsigned a, unsigned b, unsigned c,
                   unsigned d)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return result;
}

static unsigned path_length(const char *path)
{
    if (!path)
        return 0;
    unsigned length = 0;
    while (length < 512 && path[length])
        length++;
    return length == 512 ? 0 : length;
}

int tinos_open(const char *path)
{
    return tinos_open_flags(path, TINOS_O_RDONLY);
}

int tinos_open_flags(const char *path, unsigned flags)
{
    unsigned length = path_length(path);
    if (!length)
        return -1;
    return invoke(TINOS_SYS_OPEN, (unsigned)path, length, flags);
}

int tinos_mkdir(const char *path)
{
    unsigned length = path_length(path);
    return length ? invoke(TINOS_SYS_MKDIR, (unsigned)path, length, 0) : -1;
}

int tinos_unlink(const char *path)
{
    unsigned length = path_length(path);
    return length ? invoke(TINOS_SYS_UNLINK, (unsigned)path, length, 0) : -1;
}

int tinos_close(int fd)
{
    return invoke(TINOS_SYS_CLOSE, (unsigned)fd, 0, 0);
}

int tinos_read(int fd, void *buffer, unsigned length)
{
    return invoke(TINOS_SYS_READ, (unsigned)fd, (unsigned)buffer, length);
}

int tinos_write(int fd, const void *buffer, unsigned length)
{
    int result;
    do {
        result = invoke(TINOS_SYS_WRITE, (unsigned)fd, (unsigned)buffer, length);
    } while (result == 0 && length && (fd == 1 || fd == 2));
    return result;
}

int tinos_spawn(const char *path, unsigned argc, const char *const argv[])
{
    unsigned length = path_length(path);
    if (!length)
        return -1;
    return invoke4(TINOS_SYS_SPAWN, (unsigned)path, length, argc,
                   (unsigned)argv);
}

int tinos_wait(int pid, int *status)
{
    return invoke(TINOS_SYS_WAIT, (unsigned)pid, (unsigned)status, 0);
}

int tinos_getpid(void)
{
    return invoke(TINOS_SYS_GETPID, 0, 0, 0);
}

int tinos_readdir(const char *path, unsigned index, tinos_dirent_t *entry)
{
    unsigned length = path_length(path);
    if (!length)
        return -1;
    return invoke4(TINOS_SYS_READDIR, (unsigned)path, length, index,
                   (unsigned)entry);
}

int tinos_stat(const char *path, tinos_dirent_t *entry)
{
    unsigned length = path_length(path);
    if (!length)
        return -1;
    return invoke(TINOS_SYS_STAT, (unsigned)path, length, (unsigned)entry);
}

int tinos_sysinfo(tinos_system_info_t *info)
{
    return invoke(TINOS_SYS_SYSINFO, (unsigned)info, 0, 0);
}

int tinos_ioctl(int fd, unsigned request, unsigned value)
{
    return invoke(TINOS_SYS_IOCTL, (unsigned)fd, request, value);
}

int tinos_gui_open(const char *title, unsigned width, unsigned height)
{
    unsigned length = path_length(title);
    if (!length)
        return -1;
    return invoke4(TINOS_SYS_GUI_OPEN, (unsigned)title, length, width, height);
}

int tinos_gui_present(int handle, const char *cells, unsigned columns,
                      unsigned rows)
{
    if (!columns || !rows || columns > TINOS_GUI_COLS ||
        rows > TINOS_GUI_ROWS)
        return -1;
    return invoke4(TINOS_SYS_GUI_PRESENT, (unsigned)handle, (unsigned)cells,
                   columns * rows, columns);
}

int tinos_gui_event(int handle, tinos_gui_event_t *event)
{
    return invoke(TINOS_SYS_GUI_EVENT, (unsigned)handle, (unsigned)event, 0);
}

int tinos_gui_close(int handle)
{
    return invoke(TINOS_SYS_GUI_CLOSE, (unsigned)handle, 0, 0);
}

int tinos_gui_blit(int handle, const uint32_t *pixels,
                   unsigned width, unsigned height)
{
    return invoke4(TINOS_SYS_GUI_BLIT, (unsigned)handle, (unsigned)pixels,
                   width, height);
}

int tinos_gui_timer(int handle, unsigned interval_ms)
{
    return invoke(TINOS_SYS_GUI_TIMER, (unsigned)handle, interval_ms, 0);
}

int tinos_audio_open(unsigned sample_rate)
{
    return invoke(TINOS_SYS_AUDIO_OPEN, sample_rate, 0, 0);
}

int tinos_audio_write(int handle, const int16_t *samples, unsigned frames)
{
    return invoke(TINOS_SYS_AUDIO_WRITE, (unsigned)handle,
                  (unsigned)samples, frames);
}

int tinos_audio_control(int handle, unsigned command)
{
    return invoke(TINOS_SYS_AUDIO_CONTROL, (unsigned)handle, command, 0);
}

int tinos_pcm_open(const tinos_pcm_format_t *format)
{
    return invoke(TINOS_SYS_PCM_OPEN, (unsigned)format, 0, 0);
}

int tinos_pcm_write(int handle, const void *data, unsigned frames)
{
    return invoke(TINOS_SYS_AUDIO_WRITE, (unsigned)handle,
                  (unsigned)data, frames);
}

int tinos_pcm_wait(int handle, unsigned maximum_queued_frames)
{
    return invoke(TINOS_SYS_PCM_WAIT, (unsigned)handle,
                  maximum_queued_frames, 0);
}

int tinos_dlopen(const char *name)
{
    unsigned length = path_length(name);
    if (!length)
        return -1;
    int handle = invoke(TINOS_SYS_DLOPEN, (unsigned)name, length, 0);
    if (handle > 0) {
        for (;;) {
            void (*initializer)(void) = (void (*)(void))(unsigned)
                invoke(TINOS_SYS_DL_INIT_NEXT, 0, 0, 0);
            if (!initializer)
                break;
            initializer();
        }
    }
    return handle;
}

int tinos_dlclose(int handle)
{
    int pending = invoke(TINOS_SYS_DL_CLOSE_PREPARE, (unsigned)handle, 0, 0);
    if (pending < 0)
        return -1;
    if (pending)
        for (;;) {
            void (*finalizer)(void) = (void (*)(void))(unsigned)
                invoke(TINOS_SYS_DL_CLOSE_FINI_NEXT, 0, 0, 0);
            if (!finalizer)
                break;
            finalizer();
        }
    return invoke(TINOS_SYS_DLCLOSE, (unsigned)handle, 0, 0);
}

void *tinos_dlsym(int handle, const char *symbol)
{
    unsigned length = path_length(symbol);
    return length ? (void *)invoke(TINOS_SYS_DLSYM, (unsigned)handle,
                                   (unsigned)symbol, length) : 0;
}

int tinos_gui_console_attach(int handle)
{
    return invoke(TINOS_SYS_GUI_CONSOLE_ATTACH, (unsigned)handle, 0, 0);
}

int tinos_gui_console_read(int handle, char *buffer, unsigned length)
{
    return invoke(TINOS_SYS_GUI_CONSOLE_READ, (unsigned)handle,
                  (unsigned)buffer, length);
}

int tinos_tty_send(int child_pid, char ch)
{
    return invoke(TINOS_SYS_TTY_SEND, (unsigned)child_pid,
                  (unsigned)(unsigned char)ch, 0);
}

int tinos_kill(int child_pid)
{
    return invoke(TINOS_SYS_KILL, (unsigned)child_pid, 0, 0);
}

__attribute__((noreturn)) void tinos_exit(int status)
{
    for (;;) {
        void (*finalizer)(void) = (void (*)(void))(unsigned)
            invoke(TINOS_SYS_DL_FINI_NEXT, 0, 0, 0);
        if (!finalizer)
            break;
        finalizer();
    }
    invoke(TINOS_SYS_EXIT, (unsigned)status, 0, 0);
    for (;;)
        ;
}
