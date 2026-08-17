#include <unistd.h>

long tycho_life_sleep(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
