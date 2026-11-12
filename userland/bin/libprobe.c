#include <tinos/bmp.h>
#include <tinos/libc.h>
#include <tinos/wav.h>

typedef int (*bmp_decode_fn)(tinos_bmp_reader_t, void *, uint32_t *,
                              unsigned, unsigned *, unsigned *);

int main(void)
{
    int bmp = tinos_dlopen("libtinosbmp.so");
    int wav = tinos_dlopen("libtinoswav.so");
    bmp_decode_fn decode = (bmp_decode_fn)tinos_dlsym(bmp, "tinos_bmp_decode");
    if (bmp <= 0 || wav <= 0 || bmp == wav || !decode ||
        decode(0, 0, 0, 0, 0, 0) != -1 ||
        tinos_dlopen("libtinosbmp.so") != bmp ||
        !tinos_dlsym(wav, "tinos_wav_open") ||
        tinos_dlsym(bmp, "tinos_wav_open") ||
        tinos_dlsym(wav, "tinos_bmp_decode") ||
        tinos_dlsym(0, "tinos_bmp_decode") ||
        tinos_dlopen("../libtinosbmp.so") != -1 ||
        tinos_dlopen("libmissing.so") != -1 ||
        tinos_open_flags("/apps/lib/libtinosbmp.so", TINOS_O_WRONLY) != -1)
        return 1;
    const char message[] = "ring3: dlopen, dlsym and library isolation OK\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
