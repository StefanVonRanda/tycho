/* Companion C for examples/snake/snake.hi, linked via `hierc --shim`. Provides raw
 * (non-canonical, non-blocking) terminal input + a millisecond sleep, which hier has
 * no builtins for. Signatures match hier's extern emission: hier `int` == C `long`.
 * Only the interactive `./snake play` mode calls these; the deterministic demo does
 * not, so it stays tty-independent. */
#define _DEFAULT_SOURCE 1   /* expose usleep / tcgetattr even under a strict -std=c11 */
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

static struct termios snake_oldt;
static int snake_oldfl;

/* stdin -> raw: no line buffering, no echo, reads return immediately (VMIN=VTIME=0),
 * and O_NONBLOCK so a read with no key waiting returns 0 instead of blocking. */
long snake_raw_on(void) {
    tcgetattr(0, &snake_oldt);
    struct termios t = snake_oldt;
    t.c_lflag &= ~((tcflag_t)(ICANON | ECHO));
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    snake_oldfl = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, snake_oldfl | O_NONBLOCK);
    return 0;
}

long snake_raw_off(void) {
    tcsetattr(0, TCSANOW, &snake_oldt);
    fcntl(0, F_SETFL, snake_oldfl);
    return 0;
}

long snake_read_key(void) {
    unsigned char c;
    long n = (long)read(0, &c, 1);
    return n == 1 ? (long)c : -1;
}

long snake_sleep_ms(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
