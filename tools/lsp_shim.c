#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read one line (through the newline) from stdin; "" on EOF. */
char *tycho_read_line(void) {
    static char buf[65536];
    if (!fgets(buf, sizeof buf, stdin)) return "";
    return buf;
}

/* Read EXACTLY n bytes from stdin (looping over short reads); the JSON body. */
char *tycho_read_n(long n) {
    static char *buf = NULL;
    static long cap = 0;
    if (n + 1 > cap) {
        char *nb = realloc(buf, (size_t)(n + 1));
        if (!nb) return "";
        buf = nb;
        cap = n + 1;
    }
    long got = 0;
    while (got < n) {
        size_t r = fread(buf + got, 1, (size_t)(n - got), stdin);
        if (r == 0) break;                 /* EOF */
        got += (long)r;
    }
    buf[got] = 0;
    return buf;
}

/* Write a string to stdout and flush (so the client sees the reply immediately). */
long tycho_write(char *s) {
    size_t len = strlen(s);
    fwrite(s, 1, len, stdout);
    fflush(stdout);
    return 0;
}

/* Run a command (to invoke the compiler on a buffer for diagnostics). */
/* system() is cmd.exe on Windows, and cmd cannot execute a command NAME
 * written with forward slashes: "./tychoc --version" dies with "'.' is not
 * recognized as an internal or external command" before the compiler is ever
 * reached. That is how the LSP lost every compiler-backed answer on Windows --
 * diagnostics, hover, completion, document/workspace symbols, inlay hints and
 * signature help all came back empty while the server still framed replies
 * correctly and looked healthy.
 *
 * Only the first token is rewritten. Arguments keep their forward slashes,
 * which the CRT accepts, so the "/tmp/..." paths the caller builds still work.
 * A command name containing a space would need quoting; the LSP never builds
 * one (it passes $TYCHOC or the bare word "tychoc"). */
long tycho_run(char *cmd) {
#ifdef _WIN32
    char *copy = strdup(cmd);
    if (copy) {
        for (char *q = copy; *q && *q != ' ' && *q != '\t'; q++)
            if (*q == '/') *q = '\\';
        long r = (long)system(copy);
        free(copy);
        return r;
    }
#endif
    return (long)system(cmd);
}

/* Windows needs a different null device ("nul", not "/dev/null") and a temp
 * root that exists: tycho_run is system(), which on Windows is cmd.exe, and a
 * native PE resolves the LSP's "/tmp/..." constants to C:\tmp. Every
 * compiler-backed feature (diagnostics, --symbols) shells out through those
 * two, so without this the LSP answered every semantic request with nothing
 * on Windows while still looking healthy. */
long tycho_is_windows(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}

/* Mirror the .ty files of `dir` into `mir`, replacing whatever was there.
 *
 * This was a shell one-liner that recreated M and copied every .ty file from D.
 * system() is cmd.exe on Windows, which has none of rm/mkdir -p/cp, does not
 * take `;`, and reads a leading `/` as a switch -- so the mirror was never
 * built there and every package-aware LSP answer (sibling resolution against a
 * live buffer) came back empty. Doing it in C removes the shell from the path
 * on both platforms instead of maintaining two spellings of the same command.
 *
 * Only regular *.ty files one level deep are copied -- the same set the old
 * The old copy glob matched only D's .ty files, so no recursion and no subdirectories. Returns 0
 * on success, non-zero if the mirror directory could not be made. A file that
 * cannot be read is skipped, exactly as the old `2>/dev/null` swallowed it. */
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define ty_mkdir(p) _mkdir(p)
#else
#define ty_mkdir(p) mkdir((p), 0700)
#endif

static void ty_copy_one(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) break;      /* short write: leave what landed */
    }
    fclose(in);
    fclose(out);
}

long tycho_pkg_mirror(char *dir, char *mir) {
    /* Clear the mirror: unlink its entries, then the directory itself. Ignore
     * failures -- a first run has nothing to clear. */
    DIR *d = opendir(mir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[4096];
            if (snprintf(p, sizeof p, "%s/%s", mir, e->d_name) < (int)sizeof p) remove(p);
        }
        closedir(d);
    }
    if (ty_mkdir(mir) != 0) {
        struct stat st;
        if (stat(mir, &st) != 0 || !S_ISDIR(st.st_mode)) return 1;   /* not there and not creatable */
    }
    d = opendir(dir);
    if (!d) return 0;                                 /* nothing to mirror is not an error */
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 4 || strcmp(e->d_name + len - 3, ".ty") != 0) continue;
        char src[4096], dst[4096];
        if (snprintf(src, sizeof src, "%s/%s", dir, e->d_name) >= (int)sizeof src) continue;
        if (snprintf(dst, sizeof dst, "%s/%s", mir, e->d_name) >= (int)sizeof dst) continue;
        struct stat st;
        if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;   /* skip subdirectories */
        ty_copy_one(src, dst);
    }
    closedir(d);
    return 0;
}
