#include <tinos/libc.h>
#include <tinos/string.h>

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--playlist") == 0) {
        int ok = tinos_copy("/apps/media/demo.wav",
                            "/users/sammy/one.wav") == 0 &&
                 tinos_copy("/apps/media/demo.wav",
                            "/users/sammy/two.wav") == 0;
        const char *message = ok ? "ring3: playlist ready\n" :
                                   "ring3: playlist FAILED\n";
        tinos_write(1, message, strlen(message));
        return ok ? 0 : 1;
    }
    const char *dir = "/users/sammy/actions-probe";
    const char *source = "/users/sammy/actions-probe/source";
    const char *copy = "/users/sammy/actions-probe/copy";
    const char *moved = "/users/sammy/actions-probe/moved";
    int ok = tinos_mkdir(dir) == 0;
    int fd = tinos_open_flags(source, TINOS_O_WRONLY | TINOS_O_CREAT);
    ok &= fd >= 0;
    if (fd >= 0) {
        ok &= tinos_write(fd, "seed", 4) == 4;
        tinos_close(fd);
    }
    ok &= tinos_copy(source, copy) == 0;
    fd = tinos_open(copy);
    char text[8] = {0};
    ok &= fd >= 0;
    if (fd >= 0) {
        ok &= tinos_seek(fd, 1) == 1;
        ok &= tinos_read(fd, text, 3) == 3;
        tinos_close(fd);
    }
    ok &= strcmp(text, "eed") == 0;
    ok &= tinos_rename(copy, moved) == 0;
    ok &= tinos_copy(source, moved) == -1;
    ok &= tinos_rename(source, "/apps/forbidden") == -1;
    ok &= tinos_rename(source, "/system/forbidden") == -1;
    ok &= tinos_rename(source, "/drive/forbidden") == -1;
    int invalid;
    __asm__ volatile("int $0x80" : "=a"(invalid)
                     : "a"(TINOS_SYS_COPY), "b"(source),
                       "c"(strlen(source)), "d"(0xfffffffcu), "S"(8u)
                     : "memory");
    ok &= invalid == -1;
    __asm__ volatile("int $0x80" : "=a"(invalid)
                     : "a"(TINOS_SYS_CLIPBOARD_SET),
                       "b"(0xfffffffcu), "c"(8u),
                       "d"(TINOS_CLIPBOARD_TEXT) : "memory");
    ok &= invalid == -1;
    unsigned kind = 0;
    ok &= tinos_clipboard_set("hello", 5, TINOS_CLIPBOARD_TEXT) == 0;
    ok &= tinos_clipboard_get(text, sizeof(text), &kind) == 5;
    ok &= kind == TINOS_CLIPBOARD_TEXT &&
          memcmp(text, "hello", 5) == 0;
    const char *sample = "/users/sammy/actions-probe/demo.wav";
    tinos_dirent_t original, copied;
    ok &= tinos_stat("/apps/media/demo.wav", &original) == 1 &&
          original.size > 64 * 1024;
    ok &= tinos_copy("/apps/media/demo.wav", sample) == 0;
    ok &= tinos_stat(sample, &copied) == 1 &&
          copied.size == original.size;
    fd = tinos_open(sample);
    ok &= fd >= 0;
    if (fd >= 0) {
        ok &= tinos_seek(fd, 44) == 44;
        tinos_close(fd);
    }
    tinos_audio_settings_t audio;
    if (tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET,
                    (unsigned)(uintptr_t)&audio) == 0) {
        tinos_audio_settings_t bad = audio;
        bad.volume = 101;
        ok &= tinos_ioctl(0, TINOS_IOCTL_AUDIO_SET,
                          (unsigned)(uintptr_t)&bad) == -1;
        tinos_audio_settings_t after;
        ok &= tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET,
                          (unsigned)(uintptr_t)&after) == 0 &&
              after.volume == audio.volume;
    }
    tinos_net_settings_t network, after_network;
    ok &= tinos_ioctl(0, TINOS_IOCTL_NET_GET,
                      (unsigned)(uintptr_t)&network) == 0;
    tinos_net_settings_t invalid_network = network;
    invalid_network.address = 0x0a000002u;
    invalid_network.netmask = 0xff00ff00u;
    ok &= tinos_ioctl(0, TINOS_IOCTL_NET_MANUAL,
                      (unsigned)(uintptr_t)&invalid_network) == -1;
    ok &= tinos_ioctl(0, TINOS_IOCTL_NET_GET,
                      (unsigned)(uintptr_t)&after_network) == 0 &&
          memcmp(&after_network, &network, sizeof(network)) == 0;
    ok &= tinos_ioctl(0, TINOS_IOCTL_NET_GET, 0xfffffffcu) == -1;
    ok &= tinos_ioctl(0, TINOS_IOCTL_WALLPAPER, 99) == -1;
    ok &= tinos_ioctl(0, TINOS_IOCTL_DISPLAY_MODE, 0) == -1;
    tinos_unlink(sample);
    tinos_unlink(moved);
    tinos_unlink(source);
    tinos_unlink(dir);
    const char *result = ok ? "ring3: actions isolated\n" :
                              "ring3: actions FAILED\n";
    tinos_write(1, result, strlen(result));
    return ok ? 0 : 1;
}
