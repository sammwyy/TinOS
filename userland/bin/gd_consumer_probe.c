#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);
typedef unsigned *(*pointer_fn)(void);

int main(void)
{
    int gd = tinos_dlopen("libtinosgd.so");
    int consumer = tinos_dlopen("libtinosconsumer.so");
    next_fn gd_next = (next_fn)tinos_dlsym(gd, "tinos_gd_next");
    next_fn consumer_next = (next_fn)tinos_dlsym(
        consumer, "tinos_gd_consumer_next");
    pointer_fn consumer_pointer = (pointer_fn)tinos_dlsym(
        consumer, "tinos_gd_consumer_pointer");
    unsigned *counter = tinos_dlsym(gd, "tinos_gd_counter");
    if (gd < 1 || consumer < 1 || !gd_next || !consumer_next ||
        !consumer_pointer || !counter || *counter != 3 ||
        consumer_pointer() != counter || consumer_next() != 4 ||
        gd_next() != 5 || tinos_dlclose(gd) != 0 ||
        consumer_next() != 6 || *counter != 6 ||
        tinos_dlclose(consumer) != 0)
        return 1;
    gd = tinos_dlopen("libtinosgd.so");
    counter = tinos_dlsym(gd, "tinos_gd_counter");
    if (gd < 1 || !counter || *counter != 3 ||
        tinos_dlclose(gd) != 0)
        return 2;
    const char message[] = "ring3: TLS imports use dependency state\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
