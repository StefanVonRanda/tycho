/* Companion C for examples/minesweeper/mine.ty, linked via `tychoc --shim`. Raw,
 * BLOCKING single-byte terminal input (minesweeper is turn-based, so it waits for
 * a key rather than polling). Signatures match tycho's extern emission: tycho `int`
 * == C `long`. Only `./mine play` calls these; the demo stays tty-independent. */
#define _DEFAULT_SOURCE 1   /* expose tcgetattr even under a strict -std=c11 */
#include <termios.h>
#include <unistd.h>

static struct termios mine_oldt;

/* stdin -> raw: no line buffering, no echo, one byte per read, blocking (VMIN=1). */
long mine_raw_on(void) {
    tcgetattr(0, &mine_oldt);
    struct termios t = mine_oldt;
    t.c_lflag &= ~((tcflag_t)(ICANON | ECHO));
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    return 0;
}

long mine_raw_off(void) {
    tcsetattr(0, TCSANOW, &mine_oldt);
    return 0;
}

long mine_read_key(void) {
    unsigned char c;
    if (read(0, &c, 1) == 1) return (long)c;
    return -1;   /* EOF (e.g. piped input exhausted) */
}
