/* Forwards every call a native app makes to the real kernel function of the
 * same name through the ABI table the loader handed to app_main(). App
 * source code stays unchanged: it still calls wm_open(), gfx_text() and so
 * on directly, it just links against this instead of the real kernel
 * objects.
 */
#include <tinos/abi.h>
#include <tinos/printf.h>

static const tinos_abi_t *abi;

void tinos_abi_install(const tinos_abi_t *table)
{
    abi = table;
}

/* Windows and widgets */

window_t *wm_open(const app_class_t *cls, int width, int height,
                  const char *title, const char *arg)
{
    return abi->wm_open(cls, width, height, title, arg);
}

void wm_close(window_t *win)
{
    abi->wm_close(win);
}

void wm_invalidate(window_t *win)
{
    abi->wm_invalidate(win);
}

void wm_invalidate_all(void)
{
    abi->wm_invalidate_all();
}

rect_t wm_client_rect(const window_t *win)
{
    return abi->wm_client_rect(win);
}

void wm_set_title(window_t *win, const char *title)
{
    abi->wm_set_title(win, title);
}

size_t wm_count(void)
{
    return abi->wm_count();
}

window_t *wm_focused(void)
{
    return abi->wm_focused();
}

void gfx_fill_rect(int x, int y, int w, int h, color_t color)
{
    abi->gfx_fill_rect(x, y, w, h, color);
}

void gfx_rect(int x, int y, int w, int h, color_t color)
{
    abi->gfx_rect(x, y, w, h, color);
}

void gfx_hline(int x, int y, int w, color_t color)
{
    abi->gfx_hline(x, y, w, color);
}

void gfx_vline(int x, int y, int h, color_t color)
{
    abi->gfx_vline(x, y, h, color);
}

void gfx_gradient_v(int x, int y, int w, int h, color_t top, color_t bottom)
{
    abi->gfx_gradient_v(x, y, w, h, top, bottom);
}

void gfx_blit_image(rect_t dst, const uint32_t *pixels, int width, int height)
{
    abi->gfx_blit_image(dst, pixels, width, height);
}

void gfx_text(int x, int y, const char *s, color_t fg)
{
    abi->gfx_text(x, y, s, fg);
}

void gfx_text_n(int x, int y, const char *s, size_t n, color_t fg)
{
    abi->gfx_text_n(x, y, s, n, fg);
}

int gfx_text_width(const char *s)
{
    return abi->gfx_text_width(s);
}

bool gfx_resize(int width, int height)
{
    return abi->gfx_resize(width, height);
}

int gfx_width(void)
{
    return abi->gfx_width();
}

int gfx_height(void)
{
    return abi->gfx_height();
}

void gfx_clip_push(rect_t r)
{
    abi->gfx_clip_push(r);
}

void gfx_clip_pop(void)
{
    abi->gfx_clip_pop();
}

void widget_icon(int x, int y, int size, icon_t icon)
{
    abi->widget_icon(x, y, size, icon);
}

void widget_button(rect_t r, const char *label, bool hot, bool pressed)
{
    abi->widget_button(r, label, hot, pressed);
}

void widget_button_icon(rect_t r, icon_t icon, const char *label, bool hot,
                        bool pressed)
{
    abi->widget_button_icon(r, icon, label, hot, pressed);
}

void widget_slider(rect_t r, int value, int maximum)
{
    abi->widget_slider(r, value, maximum);
}

int widget_slider_value(rect_t r, int x, int maximum)
{
    return abi->widget_slider_value(r, x, maximum);
}

void widget_panel(rect_t r, color_t fill, color_t border)
{
    abi->widget_panel(r, fill, border);
}

void widget_text_centered(rect_t r, const char *text, color_t color)
{
    abi->widget_text_centered(r, text, color);
}

void widget_input_frame(rect_t r)
{
    abi->widget_input_frame(r);
}

void widget_scrollbar(rect_t track, size_t first_visible, size_t visible,
                      size_t total)
{
    abi->widget_scrollbar(track, first_visible, visible, total);
}

void text_input_reset(text_input_t *input, const char *initial)
{
    abi->text_input_reset(input, initial);
}

bool text_input_key(text_input_t *input, const key_event_t *ev)
{
    return abi->text_input_key(input, ev);
}

void text_input_draw(const text_input_t *input, rect_t r, const char *label,
                     bool blink_on)
{
    abi->text_input_draw(input, r, label, blink_on);
}

/* Applications and the desktop */

const app_entry_t *apps_registry(size_t *count)
{
    return abi->apps_registry(count);
}

bool apps_register(const app_entry_t *entry)
{
    return abi->apps_register(entry);
}

void desktop_notify_str(const char *text)
{
    abi->desktop_notify_str(text);
}

void desktop_notify(const char *fmt, ...)
{
    char    buf[96];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    abi->desktop_notify_str(buf);
}

void desktop_set_wallpaper(desktop_wallpaper_mode_t mode, color_t top,
                           color_t bottom)
{
    abi->desktop_set_wallpaper(mode, top, bottom);
}

void desktop_shutdown(bool reboot)
{
    abi->desktop_shutdown(reboot);
    for (;;) /* desktop_shutdown() never returns; this just satisfies the
              * compiler about this wrapper's own (non-noreturn) type. */
        ;
}

/* Filesystem */

fs_node_t *fs_root(void)
{
    return abi->fs_root();
}

fs_result_t fs_resolve(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    return abi->fs_resolve(cwd, path, out);
}

fs_node_t *fs_child(const fs_node_t *dir, const char *name)
{
    return abi->fs_child(dir, name);
}

size_t fs_child_count(const fs_node_t *dir)
{
    return abi->fs_child_count(dir);
}

size_t fs_path(const fs_node_t *node, char *buf, size_t size)
{
    return abi->fs_path(node, buf, size);
}

const char *fs_strerror(fs_result_t err)
{
    return abi->fs_strerror(err);
}

fs_result_t fs_mkdir(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    return abi->fs_mkdir(cwd, path, out);
}

fs_result_t fs_create(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    return abi->fs_create(cwd, path, out);
}

fs_result_t fs_remove_node(fs_node_t *node, bool recursive)
{
    return abi->fs_remove_node(node, recursive);
}

fs_result_t fs_rename(fs_node_t *node, const char *new_name)
{
    return abi->fs_rename(node, new_name);
}

fs_result_t fs_move(fs_node_t *cwd, const char *src, const char *dst)
{
    return abi->fs_move(cwd, src, dst);
}

fs_result_t fs_copy_node(fs_node_t *src, fs_node_t *dst_dir,
                         const char *new_name, bool recursive,
                         fs_node_t **out)
{
    return abi->fs_copy_node(src, dst_dir, new_name, recursive, out);
}

fs_result_t fs_write(fs_node_t *file, const void *data, size_t len)
{
    return abi->fs_write(file, data, len);
}

uint32_t fs_generation(void)
{
    return abi->fs_generation();
}

size_t fs_node_count(void)
{
    return abi->fs_node_count();
}

size_t fs_total_bytes(void)
{
    return abi->fs_total_bytes();
}

fs_io_request_t *fs_read_all_async(const fs_node_t *file,
                                   fs_io_complete_fn complete, void *arg)
{
    return abi->fs_read_all_async(file, complete, arg);
}

bool fs_io_ok(const fs_io_request_t *request)
{
    return abi->fs_io_ok(request);
}

const uint8_t *fs_io_data(const fs_io_request_t *request)
{
    return abi->fs_io_data(request);
}

size_t fs_io_size(const fs_io_request_t *request)
{
    return abi->fs_io_size(request);
}

void fs_io_cancel(fs_io_request_t *request)
{
    abi->fs_io_cancel(request);
}

void fs_io_release(fs_io_request_t *request)
{
    abi->fs_io_release(request);
}

/* Threads */

kernel_thread_t *thread_spawn(const char *name, thread_entry_fn entry,
                              void *arg)
{
    return abi->thread_spawn(name, entry, arg);
}

kernel_thread_t *thread_spawn_pinned(const char *name, thread_entry_fn entry,
                                     void *arg)
{
    return abi->thread_spawn_pinned(name, entry, arg);
}

int thread_join(kernel_thread_t *thread)
{
    return abi->thread_join(thread);
}

void thread_detach(kernel_thread_t *thread)
{
    abi->thread_detach(thread);
}

void thread_cancel(kernel_thread_t *thread)
{
    abi->thread_cancel(thread);
}

bool thread_cancelled(void)
{
    return abi->thread_cancelled();
}

thread_state_t thread_state(const kernel_thread_t *thread)
{
    return abi->thread_state(thread);
}

void thread_yield(void)
{
    abi->thread_yield();
}

void thread_exit(void)
{
    abi->thread_exit();
    for (;;) /* thread_exit() never returns; this just satisfies the
              * compiler about this wrapper's own (non-noreturn) type. */
        ;
}

/* Audio */

bool audio_available(void)
{
    return abi->audio_available();
}

void audio_set_volume(uint8_t volume)
{
    abi->audio_set_volume(volume);
}

uint8_t audio_get_volume(void)
{
    return abi->audio_get_volume();
}

void audio_set_muted(bool muted)
{
    abi->audio_set_muted(muted);
}

bool audio_is_muted(void)
{
    return abi->audio_is_muted();
}

bool audio_stream_start(const audio_format_t *format)
{
    return abi->audio_stream_start(format);
}

size_t audio_stream_write(const int16_t *samples, size_t frames)
{
    return abi->audio_stream_write(samples, frames);
}

void audio_stream_pause(bool paused)
{
    abi->audio_stream_pause(paused);
}

void audio_stream_drain(void)
{
    abi->audio_stream_drain();
}

void audio_stream_stop(void)
{
    abi->audio_stream_stop();
}

size_t audio_stream_queued_frames(void)
{
    return abi->audio_stream_queued_frames();
}

size_t audio_stream_capacity_frames(void)
{
    return abi->audio_stream_capacity_frames();
}

void audio_poll(void)
{
    abi->audio_poll();
}

audio_decoder_t *audio_decoder_open_file(const char *path,
                                         const fs_node_t *file,
                                         audio_format_t *format,
                                         uint64_t *total_frames,
                                         const audio_codec_t **codec_out)
{
    return abi->audio_decoder_open_file(path, file, format, total_frames,
                                        codec_out);
}

size_t audio_decoder_read(audio_decoder_t *decoder, int16_t *stereo,
                          size_t frames)
{
    return abi->audio_decoder_read(decoder, stereo, frames);
}

bool audio_decoder_seek(audio_decoder_t *decoder, uint64_t frame)
{
    return abi->audio_decoder_seek(decoder, frame);
}

void audio_decoder_close(audio_decoder_t *decoder)
{
    abi->audio_decoder_close(decoder);
}

/* Images */

image_result_t image_decode(const uint8_t *data, size_t size,
                            const char *extension, image_t *out)
{
    return abi->image_decode(data, size, extension, out);
}

void image_destroy(image_t *image)
{
    abi->image_destroy(image);
}

const char *image_result_string(image_result_t result)
{
    return abi->image_result_string(result);
}

/* Networking */

bool net_available(void)
{
    return abi->net_available();
}

const net_config_t *net_config(void)
{
    return abi->net_config();
}

bool net_request_dhcp(void)
{
    return abi->net_request_dhcp();
}

bool net_configure_manual(uint32_t address, uint32_t netmask,
                          uint32_t gateway, uint32_t dns)
{
    return abi->net_configure_manual(address, netmask, gateway, dns);
}

void net_format_address(uint32_t address, char *out, size_t size)
{
    abi->net_format_address(address, out, size);
}

bool net_parse_address(const char *text, uint32_t *out)
{
    return abi->net_parse_address(text, out);
}

bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    return abi->net_ping(address, timeout_ms, elapsed_ms);
}

bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms)
{
    return abi->net_dns_lookup(name, address, timeout_ms);
}

bool net_async_take_result(net_job_result_t *result)
{
    return abi->net_async_take_result(result);
}

/* Input */

keyboard_layout_t keyboard_get_layout(void)
{
    return abi->keyboard_get_layout();
}

void keyboard_set_layout(keyboard_layout_t layout)
{
    abi->keyboard_set_layout(layout);
}

void mouse_set_bounds(int width, int height)
{
    abi->mouse_set_bounds(width, height);
}

/* Event bus */

bool bus_subscribe(bus_event_type_t type, bus_listener_fn listener,
                   void *arg)
{
    return abi->bus_subscribe(type, listener, arg);
}

void bus_unsubscribe(bus_listener_fn listener, void *arg)
{
    abi->bus_unsubscribe(listener, arg);
}

/* Timer, shell and system info */

uint64_t timer_ticks(void)
{
    return abi->timer_ticks();
}

shell_t *shell_create(const char *initial_cwd)
{
    return abi->shell_create(initial_cwd);
}

void shell_destroy(shell_t *sh)
{
    abi->shell_destroy(sh);
}

int shell_execute(shell_t *sh, const char *line, stream_t *out)
{
    return abi->shell_execute(sh, line, out);
}

const char *shell_cwd(shell_t *sh)
{
    return abi->shell_cwd(sh);
}

sh_request_t shell_take_request(shell_t *sh, char *arg, size_t arg_size)
{
    return abi->shell_take_request(sh, arg, arg_size);
}

size_t shell_history_count(shell_t *sh)
{
    return abi->shell_history_count(sh);
}

const char *shell_history_at(shell_t *sh, size_t index)
{
    return abi->shell_history_at(sh, index);
}

bool shell_complete(shell_t *sh, const char *word, char *out,
                    size_t out_size)
{
    return abi->shell_complete(sh, word, out, out_size);
}

void sys_info_get(sys_info_t *out)
{
    abi->sys_info_get(out);
}

void klog(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    abi->klog_str(buf);
}

/* Heap */

void *kmalloc(size_t size)
{
    return abi->kmalloc(size);
}

void *kcalloc(size_t count, size_t size)
{
    return abi->kcalloc(count, size);
}

void *krealloc(void *ptr, size_t size)
{
    return abi->krealloc(ptr, size);
}

void kfree(void *ptr)
{
    abi->kfree(ptr);
}

void heap_get_stats(heap_stats_t *out)
{
    abi->heap_get_stats(out);
}
