/* The function table a loaded native app's entry point receives.
 *
 * Each app under apps/ is its own ELF binary, linked separately from
 * the kernel, so it cannot call a kernel function by symbol - the two
 * binaries never see each other's symbol tables. apps/lib/shim.c
 * wraps every entry here in a function of the same name as the real kernel
 * call, so an app's own source code just calls e.g. gfx_text() normally;
 * only its build (linked against the shim instead of the real kernel
 * objects) differs.
 *
 * A few real kernel calls are variadic (klog, desktop_notify) and cannot be
 * forwarded through a plain function pointer; the shim formats those
 * locally (every app links printf.c directly - see the Makefile) and calls
 * the pre-formatted variant instead (klog_str, desktop_notify_str).
 *
 * Grow this table only for calls an actual native app needs - it is the
 * one thing that has to stay in sync between the kernel and every native
 * app's build.
 */
#ifndef TINOS_ABI_H
#define TINOS_ABI_H

#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/codec.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/image.h>
#include <tinos/input.h>
#include <tinos/net.h>
#include <tinos/sh.h>
#include <tinos/desktop.h>
#include <tinos/thread.h>
#include <tinos/types.h>
#include <tinos/ui.h>

typedef struct {
    /* Windows and widgets */
    window_t *(*wm_open)(const app_class_t *cls, int width, int height,
                         const char *title, const char *arg);
    void   (*wm_close)(window_t *win);
    void   (*wm_invalidate)(window_t *win);
    void   (*wm_invalidate_all)(void);
    rect_t (*wm_client_rect)(const window_t *win);
    void   (*wm_set_title)(window_t *win, const char *title);
    size_t (*wm_count)(void);
    window_t *(*wm_focused)(void);

    void (*gfx_fill_rect)(int x, int y, int w, int h, color_t color);
    void (*gfx_rect)(int x, int y, int w, int h, color_t color);
    void (*gfx_hline)(int x, int y, int w, color_t color);
    void (*gfx_vline)(int x, int y, int h, color_t color);
    void (*gfx_gradient_v)(int x, int y, int w, int h, color_t top,
                           color_t bottom);
    void (*gfx_blit_image)(rect_t dst, const uint32_t *pixels, int width,
                           int height);
    void (*gfx_text)(int x, int y, const char *s, color_t fg);
    void (*gfx_text_n)(int x, int y, const char *s, size_t n, color_t fg);
    int  (*gfx_text_width)(const char *s);
    bool (*gfx_resize)(int width, int height);
    int  (*gfx_width)(void);
    int  (*gfx_height)(void);
    void (*gfx_clip_push)(rect_t r);
    void (*gfx_clip_pop)(void);

    void (*widget_icon)(int x, int y, int size, icon_t icon);
    void (*widget_button)(rect_t r, const char *label, bool hot,
                          bool pressed);
    void (*widget_button_icon)(rect_t r, icon_t icon, const char *label,
                               bool hot, bool pressed);
    void (*widget_slider)(rect_t r, int value, int maximum);
    int  (*widget_slider_value)(rect_t r, int x, int maximum);
    void (*widget_panel)(rect_t r, color_t fill, color_t border);
    void (*widget_text_centered)(rect_t r, const char *text, color_t color);
    void (*widget_input_frame)(rect_t r);
    void (*widget_scrollbar)(rect_t track, size_t first_visible,
                             size_t visible, size_t total);
    void (*text_input_reset)(text_input_t *input, const char *initial);
    bool (*text_input_key)(text_input_t *input, const key_event_t *ev);
    void (*text_input_draw)(const text_input_t *input, rect_t r,
                            const char *label, bool blink_on);

    /* Applications and the desktop */
    const app_entry_t *(*apps_registry)(size_t *count);
    bool (*apps_register)(const app_entry_t *entry);
    void (*desktop_notify_str)(const char *text);
    void (*desktop_set_wallpaper)(desktop_wallpaper_mode_t mode, color_t top,
                                  color_t bottom);
    void (*desktop_shutdown)(bool reboot);

    /* Filesystem */
    fs_node_t  *(*fs_root)(void);
    fs_result_t (*fs_resolve)(fs_node_t *cwd, const char *path,
                              fs_node_t **out);
    fs_node_t  *(*fs_child)(const fs_node_t *dir, const char *name);
    size_t      (*fs_child_count)(const fs_node_t *dir);
    size_t      (*fs_path)(const fs_node_t *node, char *buf, size_t size);
    const char *(*fs_strerror)(fs_result_t err);
    fs_result_t (*fs_mkdir)(fs_node_t *cwd, const char *path,
                            fs_node_t **out);
    fs_result_t (*fs_create)(fs_node_t *cwd, const char *path,
                             fs_node_t **out);
    fs_result_t (*fs_remove_node)(fs_node_t *node, bool recursive);
    fs_result_t (*fs_rename)(fs_node_t *node, const char *new_name);
    fs_result_t (*fs_move)(fs_node_t *cwd, const char *src, const char *dst);
    fs_result_t (*fs_copy_node)(fs_node_t *src, fs_node_t *dst_dir,
                                const char *new_name, bool recursive,
                                fs_node_t **out);
    fs_result_t (*fs_write)(fs_node_t *file, const void *data, size_t len);
    uint32_t    (*fs_generation)(void);
    size_t      (*fs_node_count)(void);
    size_t      (*fs_total_bytes)(void);
    fs_io_request_t *(*fs_read_all_async)(const fs_node_t *file,
                                          fs_io_complete_fn complete,
                                          void *arg);
    bool           (*fs_io_ok)(const fs_io_request_t *request);
    const uint8_t *(*fs_io_data)(const fs_io_request_t *request);
    size_t         (*fs_io_size)(const fs_io_request_t *request);
    void           (*fs_io_cancel)(fs_io_request_t *request);
    void           (*fs_io_release)(fs_io_request_t *request);

    /* Threads */
    kernel_thread_t *(*thread_spawn)(const char *name, thread_entry_fn entry,
                                     void *arg);
    kernel_thread_t *(*thread_spawn_pinned)(const char *name,
                                            thread_entry_fn entry, void *arg);
    int             (*thread_join)(kernel_thread_t *thread);
    void            (*thread_detach)(kernel_thread_t *thread);
    void            (*thread_cancel)(kernel_thread_t *thread);
    bool            (*thread_cancelled)(void);
    thread_state_t  (*thread_state)(const kernel_thread_t *thread);
    void            (*thread_yield)(void);
    void            (*thread_exit)(void);

    /* Audio */
    bool    (*audio_available)(void);
    void    (*audio_set_volume)(uint8_t volume);
    uint8_t (*audio_get_volume)(void);
    void    (*audio_set_muted)(bool muted);
    bool    (*audio_is_muted)(void);
    bool    (*audio_stream_start)(const audio_format_t *format);
    size_t  (*audio_stream_write)(const int16_t *samples, size_t frames);
    void    (*audio_stream_pause)(bool paused);
    void    (*audio_stream_drain)(void);
    void    (*audio_stream_stop)(void);
    size_t  (*audio_stream_queued_frames)(void);
    size_t  (*audio_stream_capacity_frames)(void);
    void    (*audio_poll)(void);

    audio_decoder_t *(*audio_decoder_open_file)(const char *path,
                                                const fs_node_t *file,
                                                audio_format_t *format,
                                                uint64_t *total_frames,
                                                const audio_codec_t **codec_out);
    size_t (*audio_decoder_read)(audio_decoder_t *decoder, int16_t *stereo,
                                 size_t frames);
    bool   (*audio_decoder_seek)(audio_decoder_t *decoder, uint64_t frame);
    void   (*audio_decoder_close)(audio_decoder_t *decoder);

    /* Images */
    image_result_t (*image_decode)(const uint8_t *data, size_t size,
                                   const char *extension, image_t *out);
    void        (*image_destroy)(image_t *image);
    const char *(*image_result_string)(image_result_t result);

    /* Networking */
    bool               (*net_available)(void);
    const net_config_t *(*net_config)(void);
    bool               (*net_request_dhcp)(void);
    bool               (*net_configure_manual)(uint32_t address,
                                               uint32_t netmask,
                                               uint32_t gateway,
                                               uint32_t dns);
    void               (*net_format_address)(uint32_t address, char *out,
                                             size_t size);
    bool               (*net_parse_address)(const char *text, uint32_t *out);
    bool               (*net_ping)(uint32_t address, uint32_t timeout_ms,
                                   uint32_t *elapsed_ms);
    bool               (*net_dns_lookup)(const char *name, uint32_t *address,
                                         uint32_t timeout_ms);
    bool               (*net_async_take_result)(net_job_result_t *result);

    /* Input */
    keyboard_layout_t (*keyboard_get_layout)(void);
    void (*keyboard_set_layout)(keyboard_layout_t layout);
    void (*mouse_set_bounds)(int width, int height);

    /* Event bus */
    bool (*bus_subscribe)(bus_event_type_t type, bus_listener_fn listener,
                          void *arg);
    void (*bus_unsubscribe)(bus_listener_fn listener, void *arg);

    /* Timer, shell and system info */
    uint64_t (*timer_ticks)(void);

    shell_t     *(*shell_create)(const char *initial_cwd);
    void         (*shell_destroy)(shell_t *sh);
    int          (*shell_execute)(shell_t *sh, const char *line,
                                  stream_t *out);
    const char  *(*shell_cwd)(shell_t *sh);
    sh_request_t (*shell_take_request)(shell_t *sh, char *arg,
                                       size_t arg_size);
    size_t       (*shell_history_count)(shell_t *sh);
    const char  *(*shell_history_at)(shell_t *sh, size_t index);
    bool         (*shell_complete)(shell_t *sh, const char *word, char *out,
                                   size_t out_size);
    void         (*sys_info_get)(sys_info_t *out);

    void (*klog_str)(const char *text);

    /* Heap */
    void *(*kmalloc)(size_t size);
    void *(*kcalloc)(size_t count, size_t size);
    void *(*krealloc)(void *ptr, size_t size);
    void  (*kfree)(void *ptr);
    void  (*heap_get_stats)(heap_stats_t *out);
} tinos_abi_t;

/* Populated with the kernel's own current addresses; safe to call once,
 * any time after the subsystems above are up. */
tinos_abi_t tinos_abi_build(void);

#endif /* TINOS_ABI_H */
