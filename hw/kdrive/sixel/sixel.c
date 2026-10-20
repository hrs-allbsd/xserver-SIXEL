/*
 * Copyright © 2004 PillowElephantBadgerBankPond
 * Copyright © 2014 Sergii Pylypenko
 * Copyright © 2014 Hayaki Saito
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of PillowElephantBadgerBankPond not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.    PillowElephantBadgerBankPond makes no
 * representations about the suitability of this software for any purpose.    It
 * is provided "as is" without express or implied warranty.
 *
 * PillowElephantBadgerBankPond DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL PillowElephantBadgerBankPond BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * It's really not my fault - see it was the elephants!!
 *    - jaymz
 *
 */
#include <dix-config.h>
#include "os/cmdline.h"
#include "os/ddx_priv.h"
#include "sixel-config.h"
#include "hostcursor.h"
#include "dix/dix_priv.h"
#include "dix/property_priv.h"
#include "property.h"
#include "windowstr.h"
#include <X11/Xatom.h>
#ifdef HAVE_CHAFA
#include <chafa.h>
/* Ensure we use a non-premultiplied BGRA format; fallback aliases for older API names */
#ifndef CHAFA_PIXEL_BGRA8_UNASSOCIATED
#  ifdef CHAFA_PIXEL_BGRA8_UNPREMULTIPLIED
#    define CHAFA_PIXEL_BGRA8_UNASSOCIATED CHAFA_PIXEL_BGRA8_UNPREMULTIPLIED
#  else
#    define CHAFA_PIXEL_BGRA8_UNASSOCIATED CHAFA_PIXEL_BGRA8_PREMULTIPLIED
#  endif
#endif
#endif
#include "kdrive.h"
#ifdef HAVE_LIBSIXEL
#include <sixel.h>
#endif
#include <termios.h>
#include <X11/keysym.h>
#include <sys/wait.h>
#if USE_MUTEX
# include <pthread.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

/* Multiplexer wrapping leaves direct terminal rendering unchanged. */
static Bool sixelTmux;
static Bool sixelScreen;
static Bool sixelIterm2Compat;
static Bool sixelIterm2Terminal; /* Startup identity, independent of render mode. */

/* These identifiers may survive SSH/screen startup; TERM alone is not enough.
 * Check once, without terminal queries or subprocesses. Later screen -x viewers
 * cannot be discovered from this environment, so retain the explicit option. */
static void
sixelDetectIterm2(void)
{
    const char *program = getenv("TERM_PROGRAM");
    const char *terminal = getenv("LC_TERMINAL");

    if ((program && !strcmp(program, "iTerm.app")) ||
        (terminal && !strcmp(terminal, "iTerm2")))
        sixelIterm2Compat = sixelIterm2Terminal = TRUE;
}

static void
sixelTmuxOption(const char *pane, const char *option, char *value, size_t capacity)
{
    int fds[2], status;
    pid_t pid, waited;
    ssize_t n;
    size_t used = 0;

    if (!pane || !*pane || pipe(fds) < 0)
        FatalError("Xsixel: cannot check tmux option (TMUX_PANE missing or pipe failed)\n");
    pid = fork();
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(fds[1]);
        execlp("tmux", "tmux", "show-options", "-A",
               strcmp(option, "focus-events") == 0 ? "-s" : "-p",
               "-v", "-t", pane,
               option, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        FatalError("Xsixel: cannot query tmux\n");
    }
    while (used < capacity - 1) {
        n = read(fds[0], value + used, capacity - 1 - used);
        if (n > 0)
            used += n;
        else if (n == 0 || errno != EINTR)
            break;
    }
    close(fds[0]);
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    value[used] = '\0';
    value[strcspn(value, "\r\n")] = '\0';
    if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        FatalError("Xsixel: cannot query tmux setting; check tmux and TMUX_PANE\n");
}

static void
sixelCheckTmux(void)
{
    const char *tmux = getenv("TMUX");
    const char *pane = getenv("TMUX_PANE");
    char value[32];

    if (!tmux || !*tmux)
        return;
    sixelTmuxOption(pane, "allow-passthrough", value, sizeof(value));
    if (strcmp(value, "on") != 0 && strcmp(value, "all") != 0)
        FatalError("Xsixel: tmux allow-passthrough is off. Run in this pane:\n"
                   "  tmux set-option -p allow-passthrough on\n"
                   "Then restart Xsixel. To enable it persistently, add to ~/.tmux.conf:\n"
                   "  set -g allow-passthrough on\n");
    sixelTmuxOption(pane, "focus-events", value, sizeof(value));
    if (strcmp(value, "on") != 0)
        FatalError("Xsixel: tmux focus-events is off. Run:\n"
                   "  tmux set-option -g focus-events on\n"
                   "Then restart Xsixel. To enable it persistently, add to ~/.tmux.conf:\n"
                   "  set -g focus-events on\n");
    sixelTmux = TRUE;
}

/* GNU screen has a 768-byte DCS buffer. Keep packets below 256 bytes,
 * including delimiters, and split ESC-backslash across packets so it cannot
 * terminate the enclosing passthrough string. No ESC doubling is used. */
static void
sixelScreenWrite(const char *data, size_t length)
{
    while (length) {
        size_t n = min(length, 252), i;

        for (i = 0; i + 1 < n; i++) {
            if (data[i] == '\033' && data[i + 1] == '\\') {
                n = i + 1;
                break;
            }
        }
        fputs("\033P", stdout);
        fwrite(data, 1, n, stdout);
        fputs("\033\\", stdout);
        data += n;
        length -= n;
    }
}

/* Multiplexers need distinct envelopes for terminal-specific controls. */
static void
sixelTerminalControl(const char *s)
{
    if (sixelScreen) {
        sixelScreenWrite(s, strlen(s));
        return;
    }
    if (!sixelTmux) {
        fputs(s, stdout);
        return;
    }
    fputs("\033Ptmux;", stdout);
    for (; *s; s++) {
        if (*s == '\033')
            putchar('\033');
        putchar(*s);
    }
    fputs("\033\\", stdout);
}

static Bool sixelDecMouse = FALSE;

char *kdExecuteCommand = NULL;
void KdExecuteChildCommand(void);
extern Bool kdRawPointerCoordinates;

static void sixelFini(void);
static void sixelStopResizeMonitor(void);
static Bool sixelScreenInit(KdScreenInfo *screen);
static Bool sixelFinishInitScreen(ScreenPtr pScreen);
static Bool sixelCreateRes(ScreenPtr pScreen);
static Bool sixelMapFramebuffer(KdScreenInfo *screen);

static void sixelKeyboardFini(KdKeyboardInfo *ki);
static Status sixelKeyboardInit(KdKeyboardInfo *ki);
static Status sixelKeyboardEnable(KdKeyboardInfo *ki);
static void sixelKeyboardDisable(KdKeyboardInfo *ki);
static void sixelKeyboardLeds(KdKeyboardInfo *ki, int leds);
static void sixelKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration);

static Bool sixelMouseInit(KdPointerInfo *pi);
static void sixelMouseFini(KdPointerInfo *pi);
static Status sixelMouseEnable(KdPointerInfo *pi);
static void sixelMouseDisable(KdPointerInfo *pi);

KdKeyboardInfo *sixelKeyboard = NULL;
KdPointerInfo *sixelPointer = NULL;
#if USE_MUTEX
pthread_mutex_t sixel_mutex;
#endif

/* Debug tracing (disabled by default). Define XSIXEL_TRACE to enable. */
#ifdef XSIXEL_TRACE
#include <stdio.h>
#define TRACE(s) fprintf(stderr, s)
#define TRACE1(s, a1) fprintf(stderr, s, a1)
#define TRACE2(s, a1,a2) fprintf(stderr, s, a1,a2)
#define TRACE3(s, a1,a2,a3) fprintf(stderr, s, a1,a2,a3)
#define TRACE4(s, a1,a2,a3,a4) fprintf(stderr, s, a1,a2,a3,a4)
#define TRACE5(s, a1,a2,a3,a4,a5) fprintf(stderr, s, a1,a2,a3,a4,a5)
#else
#define TRACE(s) do{}while(0)
#define TRACE1(s,a1) do{}while(0)
#define TRACE2(s,a1,a2) do{}while(0)
#define TRACE3(s,a1,a2,a3) do{}while(0)
#define TRACE4(s,a1,a2,a3,a4) do{}while(0)
#define TRACE5(s,a1,a2,a3,a4,a5) do{}while(0)
#endif

KdKeyboardDriver sixelKeyboardDriver = {
    .name = "keyboard",
    .Init = sixelKeyboardInit,
    .Fini = sixelKeyboardFini,
    .Enable = sixelKeyboardEnable,
    .Disable = sixelKeyboardDisable,
    .Leds = sixelKeyboardLeds,
    .Bell = sixelKeyboardBell,
};

KdPointerDriver sixelMouseDriver = {
    .name = "mouse",
    .Init = sixelMouseInit,
    .Fini = sixelMouseFini,
    .Enable = sixelMouseEnable,
    .Disable = sixelMouseDisable,
};


KdCardFuncs sixelFuncs = {
    .scrinit = sixelScreenInit,    /* scrinit */
    .finishInitScreen = sixelFinishInitScreen, /* finishInitScreen */
    .createRes = sixelCreateRes,    /* createRes */
};

int mouseState = 0;

enum { SIXEL_FRAME_INTERVAL = 8 };

typedef struct
{
    int w;
    int h;
    int pitch;
    int pixel_w, pixel_h;
    int reported_pixel_w, reported_pixel_h;
    int cell_w, cell_h;
    int cell_pixel_w, cell_pixel_h;
    Bool auto_size;
    Bool pixel_size_reported;
    Bool pixel_size_reply; /* Explicit 14t reply, rather than ioctl fallback. */
    KdScreenInfo *screen;
    Rotation randr;
    Bool shadow;
    unsigned char *buffer;
    unsigned char *bitmap;
    /* libsixel backend */
#ifdef HAVE_LIBSIXEL
    sixel_dither_t *dither;
    sixel_output_t *output;
    unsigned char *encoded;
    size_t encoded_size, encoded_capacity;
    Bool encode_failed;
#endif
    /* chafa backend */
#ifdef HAVE_CHAFA
    ChafaCanvasConfig *chafa_cfg;
    ChafaCanvas *chafa_canvas;
    ChafaTermInfo *chafa_tinfo;
#endif
} SIXEL_Driver;

static SIXEL_Driver *g_driver = NULL;
static OsTimerPtr sixelFrameTimer;
static Bool sixelFramePending;
static RegionRec sixelPendingDamage;
static Bool sixelDamageInitialized;
static int sixelHostCursorFd = -1;

/* Screen coordinates and damage rectangles use signed 16-bit coordinates. */
static Bool
sixelValidSize(int width, int height)
{
    return width > 0 && height > 0 && width <= SHRT_MAX && height <= SHRT_MAX;
}

/* screen preserves xterm's ioctl pixel size including its internal border,
 * whereas SIXEL is clipped to the character grid. Use whole integer-sized
 * cells, keeping the original report for replies arriving in either order.
 * This adds neither terminal queries nor work to the frame rendering path. */
static void
sixelNormalizeScreenPixels(SIXEL_Driver *driver)
{
    if (!sixelScreen || !driver->pixel_size_reported)
        return;
    driver->pixel_w = driver->reported_pixel_w;
    driver->pixel_h = driver->reported_pixel_h;
    if (driver->cell_w > 0 && driver->pixel_w >= driver->cell_w)
        driver->pixel_w -= driver->pixel_w % driver->cell_w;
    if (driver->cell_h > 0 && driver->pixel_h >= driver->cell_h)
        driver->pixel_h -= driver->pixel_h % driver->cell_h;
}

static void
sixelReadTerminalSize(SIXEL_Driver *driver)
{
    struct winsize ws = {0};

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (sixelValidSize(ws.ws_xpixel, ws.ws_ypixel)) {
            driver->pixel_size_reported = TRUE;
            driver->pixel_w = ws.ws_xpixel;
            driver->pixel_h = ws.ws_ypixel;
            driver->reported_pixel_w = ws.ws_xpixel;
            driver->reported_pixel_h = ws.ws_ypixel;
        }
        if (sixelValidSize(ws.ws_col, ws.ws_row)) {
            driver->cell_w = ws.ws_col;
            driver->cell_h = ws.ws_row;
        }
    }
    sixelNormalizeScreenPixels(driver);
    if (!driver->cell_w || !driver->cell_h)
        printf("\033[18t");
    /* Windows ssh.exe can preserve a plausible but stale 640x480 pixel size.
     * Use ioctl immediately, then let terminal replies correct it. tmux's valid
     * pane dimensions stay authoritative; screen must pass queries outside. */
    /* iTerm2's ioctl may use Retina backing pixels, while 14t includes window
     * chrome. Cell dimensions describe the SIXEL/mouse coordinate grid. Keep
     * tmux's pane geometry authoritative; query through screen as usual. */
    if (sixelIterm2Terminal && !sixelTmux)
        sixelTerminalControl("\033[16t");
    else if (!sixelTmux || !driver->pixel_w || !driver->pixel_h)
        sixelTerminalControl("\033[14t\033[16t");
    fflush(stdout);
}

static Bool
sixelAllocateBuffers(SIXEL_Driver *driver, int width, int height)
{
    size_t pixels;

    if (!sixelValidSize(width, height))
        return FALSE;
    pixels = (size_t) width * height;
    driver->buffer = calloc(pixels, 4);
    driver->bitmap = calloc(pixels, 3);
    if (!driver->buffer || !driver->bitmap) {
        free(driver->buffer);
        free(driver->bitmap);
        driver->buffer = driver->bitmap = NULL;
        return FALSE;
    }
    driver->w = width;
    driver->h = height;
    driver->pitch = width * 4;
    return TRUE;
}

typedef enum {
    RENDERER_DEFAULT = 0,
    RENDERER_CHAFA,
    RENDERER_LIBSIXEL
} sixel_renderer_t;

static sixel_renderer_t g_renderer = RENDERER_DEFAULT;

#define SIXEL_UP                (1 << 12 | ('A' - '@'))
#define SIXEL_DOWN              (1 << 12 | ('B' - '@'))
#define SIXEL_RIGHT             (1 << 12 | ('C' - '@'))
#define SIXEL_LEFT              (1 << 12 | ('D' - '@'))
#define SIXEL_END               (1 << 12 | ('F' - '@'))
#define SIXEL_HOME              (1 << 12 | ('H' - '@'))
#define SIXEL_FOCUSIN           (1 << 12 | ('I' - '@'))
#define SIXEL_FOCUSOUT          (1 << 12 | ('O' - '@'))
#define SIXEL_F1                (1 << 12 | ('P' - '@'))
#define SIXEL_F2                (1 << 12 | ('Q' - '@'))
#define SIXEL_F3                (1 << 12 | ('R' - '@'))
#define SIXEL_F4                (1 << 12 | ('S' - '@'))
#define SIXEL_FKEYS             (1 << 12 | ('~' - '@'))
#define SIXEL_MOUSE_SGR         (1 << 12 | ('<' - ';') << 4 << 6 | ('M' - '@'))
#define SIXEL_MOUSE_SGR_RELEASE (1 << 12 | ('<' - ';') << 4 << 6 | ('m' - '@'))
#define SIXEL_MOUSE_DEC         (1 << 12 | ('&' - 0x1f) << 6 | ('w' - '@'))
#define SIXEL_DTTERM_SEQS       (1 << 12 | ('t' - '@'))
#define SIXEL_UNKNOWN           (513)

typedef struct _key {
    int params[256];
    int nparams;
    int value;
} sixel_key_t;

enum _state {
    STATE_GROUND = 0,
    STATE_ESC = 1,
    STATE_CSI = 2,
    STATE_CSI_IGNORE = 3,
    STATE_CSI_PARAM = 4,
};

static int get_input(char *buf, int size) {
    fd_set fdset;
    struct timeval timeout;
    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1;
    if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == 1)
        return read(STDIN_FILENO, buf, size);
    return 0;
}

static int getkeys(char *buf, int nread, sixel_key_t *keys)
{
    int i, c;
    int size = 0;
    static int state = STATE_GROUND;
    static int ibytes = 0;
    static int pbytes = 0;
    static sixel_key_t pending;

    for (i = 0; i < nread; i++) {
        c = buf[i];
restart:
        switch (state) {
        case STATE_GROUND:
            switch (c) {
            case 0x1b:
                state = STATE_ESC;
                break;
            default:
                keys[size].nparams = 0;
                keys[size++].value = c;
                break;
            }
            break;
        case STATE_ESC:
            switch (c) {
            case 'O':
            case '[':
                pending.nparams = 0;
                pbytes = 0;
                state = STATE_CSI;
                break;
            default:
                keys[size].nparams = 0;
                keys[size++].value = 0x1b;
                state = STATE_GROUND;
                goto restart;
            }
            break;
        case STATE_CSI:
            switch (c) {
            case '\x1b':
                state = STATE_ESC;
                break;
            case '\x00'...'\x1a':
            case '\x1c'...'\x1f':
            case '\x7f':
                break;
            case ' '...'/':
                ibytes = c - ' ';
                pbytes = 0;
                state = STATE_CSI_PARAM;
                break;
            case '0'...'9':
                ibytes = 0;
                pbytes = c - '0';
                pending.nparams = 0;
                state = STATE_CSI_PARAM;
                break;
            case '<'...'?':
                ibytes = (c - ';') << 4;
                pending.nparams = 0;
                state = STATE_CSI_PARAM;
                break;
            case '@'...'~':
                pending.nparams = 0;
                pending.value = 1 << 12 | (c - '@');
                keys[size++] = pending;
                state = STATE_GROUND;
                break;
            default:
                state = STATE_GROUND;
                break;
            }
            break;
        case STATE_CSI_PARAM:
            switch (c) {
            case '\x1b':
                state = STATE_ESC;
                break;
            case '\x00'...'\x1a':
            case '\x1c'...'\x1f':
            case '\x7f':
                break;
            case ' '...'/':
                ibytes |= c - 0x1f;
                state = STATE_CSI_PARAM;
                break;
            case '0'...'9':
                pbytes = pbytes > (INT_MAX - (c - '0')) / 10 ? INT_MAX :
                         pbytes * 10 + c - '0';
                state = STATE_CSI_PARAM;
                break;
            case ':'...';':
                if (pending.nparams < sizeof(pending.params) / sizeof(*pending.params)) {
                    pending.params[pending.nparams++] = pbytes;
                    pbytes = 0;
                }
                break;
            case '@'...'~':
                if (pending.nparams < sizeof(pending.params) / sizeof(*pending.params)) {
                    pending.params[pending.nparams++] = pbytes;
                    pending.value = 1 << 12 | ibytes << 6 | (c - '@');
                    keys[size++] = pending;
                }
                state = STATE_GROUND;
                break;
            default:
                state = STATE_GROUND;
                break;
            }
            break;
        }
    }
    return size;
}



static int SendModifierKey(int state, uint8_t press_state)
{
    int posted = 0;

    if (state & 1) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 42+8, press_state);
    }
    if (state & 2) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 56+8, press_state);
    }
    if (state & 4) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 29+8, press_state);
    }

    return posted;
}

static int GetScancode(int code)
{
    static u_char tbl[] = {
         0,  0,  0,  0,  0,  0,  0,  0, 14, 15, 36,  0,  0, 28,  0,  0, /* 0x00 - 0x0f */
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0, /* 0x10 - 0x1f */
        57,  2, 40,  4,  5,  6,  8, 40, 10, 11,  9, 13, 51, 12, 52, 53, /* 0x20 - 0x2f */
        11,  2,  3,  4,  5,  6,  7,  8,  9, 10, 39, 39, 51, 13, 52, 53, /* 0x30 - 0x3f */
         3, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, /* 0x40 - 0x4f */
        25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 26, 43, 27,  7, 12, /* 0x50 - 0x5f */
        41, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, /* 0x60 - 0x6f */
        25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 26, 43, 27, 41, 14, /* 0x70 - 0x7f */
    };

    if(code <= 0x7f && tbl[code] > 0) {
        return tbl[code] + 8;
    } else {
        return 0;
    }
}


static int GetState(int code)
{
    if (code <= 0x7f) {
        static u_char tbl[] = {
            4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 0, 4, 4, /* 0x00 - 0x0f */
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 4, 4, 4, 4, /* 0x10 - 0x1f */
            0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, /* 0x20 - 0x2f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, /* 0x30 - 0x3f */
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x40 - 0x4f */
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, /* 0x50 - 0x5f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x60 - 0x6f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, /* 0x70 - 0x7f */
        };

        /* Shift */
        return tbl[code];
    }
    return 0;
}

static int TranslateKey(int value)
{
    /* Set the keysym information */
    int scancode = GetScancode(value);

    if (scancode == 0 && value < 0x20) {
        /* It seems Ctrl+N key */
        scancode = GetScancode(value + 0x60);
    }
    return scancode;
}

struct termios orig_termios;

static void tty_raw(void)
{
    struct termios raw;

    if (tcgetattr(fileno(stdin), &orig_termios) < 0) {
        perror("can't set raw mode");
    }
    raw = orig_termios;
    raw.c_iflag &= ~(/*BRKINT |*/ ICRNL /*| INPCK | ISTRIP | IXON*/);
    raw.c_lflag &= ~(ECHO | ICANON /*| IEXTEN | ISIG*/);
    raw.c_lflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    raw.c_cc[VINTR] = 0; raw.c_cc[VKILL] = 0; raw.c_cc[VQUIT] = 0;
    raw.c_cc[VSTOP] = 0; raw.c_cc[VSUSP] = 0;
    if (tcsetattr(fileno(stdin), TCSAFLUSH, &raw) < 0) {
        perror("can't set raw mode");
    }
}

static void tty_restore(void)
{
    tcsetattr(fileno(stdin), TCSADRAIN, &orig_termios);
}

#ifdef HAVE_LIBSIXEL
/* Blank SIXEL columns/rows advance the raster without touching existing pixels
 * when DCS parameter Pb is 1. This permits exact pixel offsets with DECSDM set. */
static void
sixelHorizontalOffset(FILE *out, int x)
{
    if (x)
        fprintf(out, "!%d?", x);
}

static Bool
sixelRenderRect(SIXEL_Driver *driver, BoxPtr damage)
{
    int x1 = max(0, damage->x1), y1 = max(0, damage->y1);
    int x2 = min(driver->w, damage->x2), y2 = min(driver->h, damage->y2);
    int x, y, width, height;
    size_t body, end, chunk, i;
    SIXELSTATUS status;
    FILE *out = stdout;
    char *screen_data = NULL;
    size_t screen_length = 0;

    if (x1 >= x2 || y1 >= y2)
        return TRUE;
    /* iTerm2 treats gaps as opaque and replaces whole character cells at image
     * edges. A partial image leaves black cell padding even without gaps.
     * Keep its image bounds fixed at the screen edges; screen -x viewers may
     * have different cell sizes, so rounding to one viewer's cells is unsafe.
     * The normal mode retains its cropped fast path. */
    if (sixelIterm2Compat) {
        x1 = y1 = 0;
        x2 = driver->w;
        y2 = driver->h;
    }
    y1 = y1 / 6 * 6;
    y2 = min(driver->h, (y2 + 5) / 6 * 6);
    width = x2 - x1;
    height = y2 - y1;
    for (y = y1; y < y2; y++) {
        unsigned char *src = driver->buffer + (size_t)y * driver->pitch + x1 * 4;
        unsigned char *dst = driver->bitmap + (size_t)(y - y1) * width * 3;
        for (x = 0; x < width; x++, src += 4, dst += 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
    }
    driver->encoded_size = 0;
    driver->encode_failed = FALSE;
    status = sixel_encode(driver->bitmap, width, height, 3,
                          driver->dither, driver->output);
    if (SIXEL_FAILED(status) || driver->encode_failed)
        return FALSE;
    /* Keep libsixel's palette and pixel data, replacing only the envelope and
     * raster attributes. No RGB pixels are allocated for the transparent gap. */
    end = driver->encoded_size;
    if (end < 4 || driver->encoded[0] != '\033' || driver->encoded[1] != 'P' ||
        driver->encoded[end - 2] != '\033' || driver->encoded[end - 1] != '\\')
        return FALSE;
    body = 2;
    while (body < end && driver->encoded[body] != 'q')
        body++;
    if (++body >= end - 2)
        return FALSE;
    if (driver->encoded[body] == '"') {
        body++;
        while (body < end - 2 &&
               ((driver->encoded[body] >= '0' && driver->encoded[body] <= '9') ||
                driver->encoded[body] == ';'))
            body++;
    }
    if (sixelScreen) {
        out = open_memstream(&screen_data, &screen_length);
        if (!out)
            return FALSE;
        fputs("\033[H", out);
    }
    if (sixelTmux) {
        /* Home and DECSDM must reach the outer terminal together with the image.
         * The image body contains SIXEL ASCII only, so only the DCS delimiters
         * require ESC doubling. */
        fputs("\033Ptmux;\033\033[H\033\033P", out);
        fprintf(out, "0;1q\"1;1;%d;%d", x2, y2);
    } else
        fprintf(out, "\033P0;1q\"1;1;%d;%d", x2, y2);
    for (y = 0; y < y1; y += 6)
        putc('-', out);
    sixelHorizontalOffset(out, x1);
    chunk = body;
    for (i = body; i < end - 2; i++) {
        if (driver->encoded[i] == '$' || driver->encoded[i] == '-') {
            fwrite(driver->encoded + chunk, 1, i + 1 - chunk, out);
            sixelHorizontalOffset(out, x1);
            chunk = i + 1;
        }
    }
    fwrite(driver->encoded + chunk, 1, end - 2 - chunk, out);
    if (sixelTmux)
        fputs("\033\033\\\033\\", out);
    else
        fprintf(out, "\033\\");
    if (sixelScreen) {
        if (fclose(out) != 0) {
            free(screen_data);
            return FALSE;
        }
        sixelScreenWrite(screen_data, screen_length);
        free(screen_data);
    }
    return TRUE;
}
#endif

static int sixelRenderFrame(SIXEL_Driver *driver)
{
#ifdef HAVE_CHAFA
    if (g_renderer == RENDERER_CHAFA &&
        (driver->cell_w <= 0 || driver->cell_h <= 0 ||
         driver->pixel_w < driver->cell_w || driver->pixel_h < driver->cell_h))
        return 0;
#endif
#if USE_MUTEX
    pthread_mutex_lock(&sixel_mutex);
#endif
#ifdef HAVE_LIBSIXEL
    if (g_renderer == RENDERER_LIBSIXEL) {
        BoxRec full = {0, 0, driver->w, driver->h};
        if (!sixelRenderRect(driver, &full))
            ErrorF("Xsixel: could not encode full frame\n");
    }
#endif
#ifdef HAVE_CHAFA
    if (g_renderer == RENDERER_CHAFA) {
        /* Convert 32bpp XRGB to packed RGB8 to avoid alpha issues */
        int x, y;
        unsigned char *dst3 = driver->bitmap;
        for (y = 0; y < driver->h; ++y) {
            unsigned char *src32 = driver->buffer + y * driver->pitch;
            for (x = 0; x < driver->w; ++x) {
                unsigned char b = src32[0];
                unsigned char g = src32[1];
                unsigned char r = src32[2];
                dst3[0] = r; dst3[1] = g; dst3[2] = b;
                src32 += 4;
                dst3  += 3;
            }
        }
        chafa_canvas_config_set_cell_geometry(driver->chafa_cfg,
                                              driver->pixel_w / driver->cell_w,
                                              driver->pixel_h / driver->cell_h);
        chafa_canvas_config_set_geometry(driver->chafa_cfg,
                                         driver->cell_w,
                                         driver->cell_h);
        driver->chafa_canvas = chafa_canvas_new(g_driver->chafa_cfg);

        chafa_canvas_draw_all_pixels(driver->chafa_canvas,
                                     CHAFA_PIXEL_RGB8,
                                     driver->bitmap,
                                     driver->w,
                                     driver->h,
                                     driver->w * 3);
        GString *s = chafa_canvas_print(driver->chafa_canvas, driver->chafa_tinfo);
        if (s) {
            printf("\033[H");
            if (sixelTmux || sixelScreen)
                sixelTerminalControl(s->str);
            else
                fwrite(s->str, 1, s->len, stdout);
            g_string_free(s, TRUE);
        }
        chafa_canvas_unref(driver->chafa_canvas);
        driver->chafa_canvas = NULL;
    }
#endif
#if USE_MUTEX
    pthread_mutex_unlock(&sixel_mutex);
#endif

    return 0;
}


/* Poll shadow damage cheaply; encode only the changed pixels. */
static CARD32
sixelFrameTimeout(OsTimerPtr timer, CARD32 now, void *arg)
{
    if (g_driver) {
        /* Also collect damage when no client is issuing GetImage requests. */
        if (g_driver->screen->pScreen)
            shadowRedisplay(g_driver->screen->pScreen);
        if (sixelFramePending) {
            sixelFramePending = FALSE;
            sixelRenderFrame(g_driver);
            RegionEmpty(&sixelPendingDamage);
            fflush(stdout);
        } else if (RegionNotEmpty(&sixelPendingDamage)) {
#ifdef HAVE_LIBSIXEL
            if (g_renderer == RENDERER_LIBSIXEL) {
                if (!sixelRenderRect(g_driver, RegionExtents(&sixelPendingDamage)))
                    ErrorF("Xsixel: could not encode damaged region\n");
            } else
#endif
                sixelRenderFrame(g_driver);
            RegionEmpty(&sixelPendingDamage);
            fflush(stdout);
        }
    }
    return SIXEL_FRAME_INTERVAL;
}

static void
SIXEL_Flip(SIXEL_Driver *driver)
{
    sixelFramePending = TRUE;
    if (!sixelFrameTimer) {
        sixelFrameTimer = TimerSet(NULL, 0, SIXEL_FRAME_INTERVAL,
                                   sixelFrameTimeout, NULL);
        if (!sixelFrameTimer) {
            sixelFramePending = FALSE;
            sixelRenderFrame(driver);
            fflush(stdout);
        }
    }
}

/* The root property is both the current state and the runtime control API.
 * Intercept only this property and commit through DIX before changing modes.
 * All other requests follow the normal path, with no per-frame property reads. */
static Atom sixelRenderModeAtom;

static void
sixelRenderModeRoot(CallbackListPtr *list, void *closure, void *data)
{
    ScreenPtr screen = data;
    const char *mode = sixelIterm2Compat ? "iterm2" : "fast";

    if (!g_driver || screen != g_driver->screen->pScreen)
        return;
    sixelRenderModeAtom = MakeAtom("_XSIXEL_RENDER_MODE",
                                   strlen("_XSIXEL_RENDER_MODE"), TRUE);
    if (!sixelRenderModeAtom ||
        dixChangeWindowProperty(serverClient, screen->root, sixelRenderModeAtom,
                                XA_STRING, 8, PropModeReplace, strlen(mode), mode,
                                FALSE) != Success)
        FatalError("Xsixel: could not initialize _XSIXEL_RENDER_MODE\n");
}

static void
sixelRenderModeProperty(CallbackListPtr *list, void *closure, void *data)
{
    PropertyFilterParam *request = data;
    WindowPtr root, window;
    Bool compat;
    size_t i;

    if (request->skip || !sixelRenderModeAtom || !g_driver ||
        !g_driver->screen->pScreen)
        return;
    root = g_driver->screen->pScreen->root;
    if (!root || request->window != root->drawable.id)
        return;
    /* Rotating or deleting the control would make its value disagree with the
     * active mode. Keep the readable state present and reject such requests. */
    for (i = 0; i < request->nAtoms; i++) {
        if (request->atoms[i] == sixelRenderModeAtom) {
            request->skip = TRUE;
            request->status = BadAccess;
            return;
        }
    }
    if (request->property != sixelRenderModeAtom)
        return;
    if (request->delete || (request->access_mode & DixRemoveAccess)) {
        request->skip = TRUE;
        request->status = BadAccess;
        return;
    }
    if (!(request->access_mode & DixWriteAccess))
        return;
    request->skip = TRUE;
    request->status = BadValue;
    if (request->type != XA_STRING || request->format != 8) {
        request->status = BadMatch;
        return;
    }
    if (request->mode != PropModeReplace)
        return;
    if (request->len == 4 && !memcmp(request->value, "fast", 4))
        compat = FALSE;
    else if (request->len == 6 && !memcmp(request->value, "iterm2", 6))
        compat = TRUE;
    else
        return;
    request->status = dixLookupWindow(&window, request->window, request->client,
                                      DixSetPropAccess);
    if (request->status != Success)
        return;
    request->status = dixChangeWindowProperty(request->client, window,
        request->property, request->type, request->format, request->mode,
        request->len, request->value, request->sendevent);
    if (request->status == Success && sixelIterm2Compat != compat) {
        sixelIterm2Compat = compat;
        SIXEL_Flip(g_driver);
    }
}

static void
sixelStopRenderModeControl(void)
{
    DeleteCallback(&RootWindowFinalizeCallback, sixelRenderModeRoot, NULL);
    DeleteCallback(&PropertyFilterCallback, sixelRenderModeProperty, NULL);
    sixelRenderModeAtom = None;
}

static void
sixelStartRenderModeControl(void)
{
    if (!AddCallback(&RootWindowFinalizeCallback, sixelRenderModeRoot, NULL) ||
        !AddCallback(&PropertyFilterCallback, sixelRenderModeProperty, NULL)) {
        sixelStopRenderModeControl();
        FatalError("Xsixel: could not register render mode control\n");
    }
}

/* screen has no focus notifications. Resend a frame periodically, without
 * terminal queries: their replies can be interpreted as commands after Ctrl-a.
 * screen forwards passthrough output only to displays showing this window. */
static OsTimerPtr sixelScreenTimer;

enum { SIXEL_SCREEN_REFRESH_INTERVAL = 2000 };

static void
sixelStopScreenMonitor(void)
{
    if (sixelScreenTimer) {
        TimerFree(sixelScreenTimer);
        sixelScreenTimer = NULL;
    }
}

static CARD32
sixelScreenPoll(OsTimerPtr timer, CARD32 now, void *arg)
{
    if (g_driver) {
        /* Reattachment may use a different terminal with default modes. */
        sixelTerminalControl("\033[?80h");
        SIXEL_Flip(g_driver);
    }
    return SIXEL_SCREEN_REFRESH_INTERVAL;
}

static void
sixelStartScreenMonitor(void)
{
    if (!sixelScreen)
        return;
    sixelScreenTimer = TimerSet(NULL, 0, SIXEL_SCREEN_REFRESH_INTERVAL,
                                sixelScreenPoll, NULL);
    if (!sixelScreenTimer)
        ErrorF("Xsixel: could not start screen automatic redraw monitor\n");
}

static Bool sixelMapFramebuffer(KdScreenInfo *screen)
{
    SIXEL_Driver *driver = screen->driver;
    KdPointerMatrix m;

    /* Always use a shadow framebuffer managed by KDrive/fb (32bpp).
     * We convert to RGB for SIXEL in the update path. */
    driver->shadow = TRUE;

    screen->width = driver->w;
    screen->height = driver->h;
    KdComputePointerMatrix(&m, driver->randr, screen->width, screen->height);
    KdSetPointerMatrix(&m);

    TRACE2("%s: shadow %d\n", __func__, driver->shadow);

    if (!KdShadowFbAlloc(screen,
                         driver->randr & (RR_Rotate_90|RR_Rotate_270)))
        return FALSE;

    return TRUE;
}

static void
sixelSetScreenSizes(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;

    if (driver->randr & (RR_Rotate_0|RR_Rotate_180))
    {
        pScreen->width = driver->w;
        pScreen->height = driver->h;
        pScreen->mmWidth = screen->width_mm;
        pScreen->mmHeight = screen->height_mm;
    }
    else
    {
        pScreen->width = driver->h;
        pScreen->height = driver->w;
        pScreen->mmWidth = screen->height_mm;
        pScreen->mmHeight = screen->width_mm;
    }
}

static int sixel_write(char *data, int size, void *priv)
{
    SIXEL_Driver *driver = priv;
    size_t needed = driver->encoded_size + (size_t)size;
    if (driver->encode_failed || size < 0 || needed < driver->encoded_size) {
        driver->encode_failed = TRUE;
        return 0;
    }
    if (needed > driver->encoded_capacity) {
        size_t capacity = max(needed, driver->encoded_capacity * 2);
        unsigned char *encoded = realloc(driver->encoded, capacity);
        if (!encoded) {
            driver->encode_failed = TRUE;
            return 0;
        }
        driver->encoded = encoded;
        driver->encoded_capacity = capacity;
    }
    memcpy(driver->encoded + driver->encoded_size, data, size);
    driver->encoded_size = needed;
    return size;
}

static Bool sixelScreenInit(KdScreenInfo *screen)
{
    SIXEL_Driver *driver;
    TRACE1("%s\n", __func__);

    driver = calloc(1, sizeof(*driver));
    if (!driver)
        return FALSE;
    driver->screen = screen;
    driver->auto_size = !screen->width && !screen->height;
    sixelReadTerminalSize(driver);
    if (driver->auto_size && sixelValidSize(driver->pixel_w, driver->pixel_h)) {
        screen->width = driver->pixel_w;
        screen->height = driver->pixel_h;
    }
    if (!screen->width || !screen->height) {
        screen->width = 640;
        screen->height = 480;
    }
    if (!sixelAllocateBuffers(driver, screen->width, screen->height)) {
        free(driver);
        return FALSE;
    }
    g_driver = driver;
    RegionInit(&sixelPendingDamage, NullBox, 0);
    sixelDamageInitialized = TRUE;
    /* Force a sane default pixel format (depth 24, bpp 32, XRGB8888). */
    screen->fb.depth = 24;
    screen->fb.bitsPerPixel = 32;
    screen->fb.visuals = (1 << TrueColor);
    screen->fb.redMask   = 0x00ff0000;
    screen->fb.greenMask = 0x0000ff00;
    screen->fb.blueMask  = 0x000000ff;

    /* Select default renderer if not set explicitly */
#if defined(HAVE_LIBSIXEL)
    if (g_renderer == RENDERER_DEFAULT)
        g_renderer = RENDERER_LIBSIXEL;
#elif defined(HAVE_CHAFA)
    if (g_renderer == RENDERER_DEFAULT)
        g_renderer = RENDERER_CHAFA;
#endif

#ifdef HAVE_CHAFA
    if (g_renderer == RENDERER_CHAFA) {
        driver->chafa_cfg = chafa_canvas_config_new();
        chafa_canvas_config_set_pixel_mode(driver->chafa_cfg, CHAFA_PIXEL_MODE_SIXELS);
        chafa_canvas_config_set_canvas_mode(driver->chafa_cfg, CHAFA_CANVAS_MODE_TRUECOLOR);
        driver->chafa_tinfo = NULL;
    }
#endif

#ifdef HAVE_LIBSIXEL
    if (g_renderer == RENDERER_LIBSIXEL) {
        if (SIXEL_FAILED(sixel_output_new(&driver->output, sixel_write, driver, NULL)))
            return FALSE;
        sixel_output_set_8bit_availability(driver->output, 0);
        driver->dither = sixel_dither_get(BUILTIN_XTERM256);
    }
#endif

    driver->randr = screen->randr;
    screen->driver = driver;

    TRACE3("Set %dx%d/%dbpp mode\n", driver->w, driver->h, screen->fb.depth);

    /* Keep visuals/masks/bpp consistent with XRGB8888 */
    screen->fb.visuals = (1 << TrueColor);
    screen->fb.redMask = 0x00ff0000;
    screen->fb.greenMask = 0x0000ff00;
    screen->fb.blueMask = 0x000000ff;
    screen->fb.bitsPerPixel = 32;
    /* Keep shadow enabled so KDrive issues shadow updates */
    screen->fb.shadow = TRUE;
    screen->rate = 8;  /* 60 is too intense for CPU */

    printf("\033]1;Freedesktop.org X server on SIXEL\007");
    return sixelMapFramebuffer(screen);
}

static void sixelShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    if (driver->shadow)
        shadowUpdatePacked(pScreen, pBuf);
    RegionUnion(&sixelPendingDamage, &sixelPendingDamage, &pBuf->pDamage->damage);
}

static void *sixelShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode, CARD32 *size, void *closure)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;

    if (!pScreenPriv->enabled) {
        return NULL;
    }

    *size = driver->pitch;

    TRACE1("%s\n", __func__);

    return (void *)((CARD8 *)driver->buffer + row * (*size) + offset);
}


static Bool sixelCreateRes(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;

    TRACE1("%s\n", __func__);

    /*
     * Hack: Kdrive assumes we have dumb videobuffer, which updates automatically,
     * and does not call update callback if shadow flag is not set.
     */
    screen->fb.shadow = TRUE;
    return KdShadowSet(pScreen, driver->randr, sixelShadowUpdate, sixelShadowWindow);
}


#ifdef RANDR
static Bool sixelRandRGetInfo(ScreenPtr pScreen, Rotation *rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    RRScreenSizePtr pSize;
    Rotation randr;
    int n;

    TRACE1("%s", __func__);

    *rotations = RR_Rotate_All|RR_Reflect_All;

    for (n = 0; n < pScreen->numDepths; n++)
        if (pScreen->allowedDepths[n].numVids)
            break;
    if (n == pScreen->numDepths) {
        return FALSE;
    }

    pSize = RRRegisterSize(pScreen,
                           screen->width,
                           screen->height,
                           screen->width_mm,
                           screen->height_mm);

    randr = KdSubRotation(driver->randr, screen->randr);

    RRSetCurrentConfig(pScreen, randr, 0, pSize);

    return TRUE;
}

static Bool sixelRandRSetConfig(ScreenPtr pScreen,
                                Rotation randr,
                                int rate,
                                RRScreenSizePtr pSize)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    SIXEL_Driver oldDriver = *driver, next = *driver;
    KdScreenInfo oldScreen = *screen, candidate = *screen;
    Bool wasEnabled = pScreenPriv->enabled;
    PixmapPtr pixmap = pScreen->GetScreenPixmap(pScreen);
    int width = pSize->width, height = pSize->height;
    int oldmmwidth = pScreen->mmWidth, oldmmheight = pScreen->mmHeight;
    KdPointerMatrix matrix;

    if (screen->randr & (RR_Rotate_90 | RR_Rotate_270)) {
        width = pSize->height;
        height = pSize->width;
    }
    next.randr = KdAddRotation(screen->randr, randr);
    if (!sixelAllocateBuffers(&next, width, height))
        return FALSE;

    /* Allocate everything before detaching the live framebuffer. */
    candidate.width = width;
    candidate.height = height;
    candidate.fb.frameBuffer = NULL;
    candidate.fb.shadow = FALSE;
    candidate.width_mm = max(1, (int)((double) oldScreen.width_mm * width / oldScreen.width));
    candidate.height_mm = max(1, (int)((double) oldScreen.height_mm * height / oldScreen.height));
    if (!KdShadowFbAlloc(&candidate, next.randr & (RR_Rotate_90 | RR_Rotate_270))) {
        free(next.buffer);
        free(next.bitmap);
        return FALSE;
    }
    if (wasEnabled)
        KdDisableScreen(pScreen);
    KdShadowUnset(pScreen);
    *driver = next;
    *screen = candidate;
    sixelSetScreenSizes(pScreen);
    if (!pScreen->ModifyPixmapHeader(pixmap, pScreen->width, pScreen->height,
                                    screen->fb.depth, screen->fb.bitsPerPixel,
                                    screen->fb.byteStride, screen->fb.frameBuffer) ||
        !sixelCreateRes(pScreen)) {
        KdShadowUnset(pScreen);
        KdShadowFbFree(&candidate);
        free(next.buffer);
        free(next.bitmap);
        *driver = oldDriver;
        *screen = oldScreen;
        sixelSetScreenSizes(pScreen);
        pScreen->mmWidth = oldmmwidth;
        pScreen->mmHeight = oldmmheight;
        pScreen->ModifyPixmapHeader(pixmap, pScreen->width, pScreen->height,
                                   screen->fb.depth, screen->fb.bitsPerPixel,
                                   screen->fb.byteStride, screen->fb.frameBuffer);
        if (!sixelCreateRes(pScreen))
            FatalError("Xsixel: could not restore shadow framebuffer\n");
        if (wasEnabled)
            KdEnableScreen(pScreen);
        return FALSE;
    }

    KdShadowFbFree(&oldScreen);
    free(oldDriver.buffer);
    free(oldDriver.bitmap);
    KdComputePointerMatrix(&matrix, driver->randr, screen->width, screen->height);
    KdSetPointerMatrix(&matrix);
    KdSetSubpixelOrder(pScreen, driver->randr);
    if (wasEnabled)
        KdEnableScreen(pScreen);
    RRGetInfo(pScreen, TRUE);
    RRScreenSizeNotify(pScreen);
    printf("\033[H\033[2J");
    SIXEL_Flip(driver);
    fflush(stdout);
    return TRUE;
}

static Bool sixelRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pScrPriv;

    TRACE1("%s", __func__);

    if (!RRScreenInit(pScreen)) {
        return FALSE;
    }

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = sixelRandRGetInfo;
    pScrPriv->rrSetConfig = sixelRandRSetConfig;
    return TRUE;
}
#endif


static Bool sixelFinishInitScreen(ScreenPtr pScreen)
{
    if (!shadowSetup(pScreen)) {
        return FALSE;
    }

#ifdef RANDR
    if (!sixelRandRInit(pScreen)) {
        return FALSE;
    }
#endif
    SIXEL_Flip(g_driver);
    return TRUE;
}

static void sixelKeyboardFini(KdKeyboardInfo *ki)
{
    TRACE1("sixelKeyboardFini() %p\n", ki);
    sixelKeyboard = NULL;
}

static Status sixelKeyboardInit(KdKeyboardInfo *ki)
{
    ki->minScanCode = 8;
    ki->maxScanCode = 255;
    free(ki->name);
    free(ki->xkbRules);
    free(ki->xkbModel);
    free(ki->xkbLayout);
    ki->name = strdup("mlterm keyboard");
    ki->xkbRules = strdup("evdev");
    ki->xkbModel = strdup("pc105");
    ki->xkbLayout = strdup("us");
    sixelKeyboard = ki;
    TRACE1("sixelKeyboardInit() %p\n", ki);
    return Success;
}

static Status sixelKeyboardEnable(KdKeyboardInfo *ki)
{
    return Success;
}

static void sixelKeyboardDisable(KdKeyboardInfo *ki)
{
}

static void sixelKeyboardLeds(KdKeyboardInfo *ki, int leds)
{
}

static void sixelKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration)
{
}

static Status sixelMouseInit(KdPointerInfo *pi)
{
    pi->nButtons = 7;
    free(pi->name);
    pi->name = strdup("SIXEL terminal pointer");
    sixelPointer = pi;
    TRACE1("sixelMouseInit() %p\n", pi);
    return Success;
}

static void sixelMouseFini(KdPointerInfo *pi)
{
    TRACE1("sixelMouseFini() %p\n", pi);
    sixelPointer = NULL;
}

static Status sixelMouseEnable(KdPointerInfo *pi)
{
    /* TODO */
    return Success;
}

static void sixelMouseDisable(KdPointerInfo *pi)
{
    /* TODO */
    return;
}

void InitCard(char *name)
{
    KdCardInfoAdd(&sixelFuncs,  0);
    TRACE1("InitCard: %s\n", name);
}

void InitOutput(int argc, char **argv)
{
    KdInitOutput(argc, argv);
    sixelStartRenderModeControl();
    signal(SIGHUP, SIG_DFL);
    TRACE("InitOutput()\n");
}

void InitInput(int argc, char **argv)
{
    KdPointerInfo *pi;
    KdKeyboardInfo *ki;

    KdAddKeyboardDriver(&sixelKeyboardDriver);
    KdAddPointerDriver(&sixelMouseDriver);

    ki = KdNewKeyboard();
    if (ki) {
        ki->name = strdup("KDrive Keyboard");
        ki->driverPrivate = strdup("keyboard");
        KdAddKeyboard(ki);
    }
    pi = KdNewPointer();
    if (pi) {
        pi->name = strdup("KDrive Pointer");
        pi->driverPrivate = strdup("mouse");
        pi->nButtons = 5;
        pi->inputClass = KD_MOUSE;
        pi->emulateMiddleButton = kdEmulateMiddleButton;
        pi->transformCoordinates = !kdRawPointerCoordinates;
        KdAddPointer(pi);
    }

    KdInitInput();
    KdExecuteChildCommand();
}

#ifdef DDXBEFORERESET
void ddxBeforeReset(void)
{
}
#endif

void ddxUseMsg(void)
{
    KdUseMsg();
    ErrorF("-exec command    Execute command after server started\n");
    ErrorF("-mouse-protocol {sgr|dec}  Select terminal mouse reporting (default: sgr pixels)\n");
    ErrorF("-iterm2                  Force iTerm2 compatibility (also detected from startup environment)\n");
    ErrorF("-backend {chafa|libsixel}  Choose renderer backend (default: libsixel if built)\n");
}

int ddxProcessArgument(int argc, char **argv, int i)
{
    if (!strcmp(argv[i], "-iterm2")) {
        sixelIterm2Compat = sixelIterm2Terminal = TRUE;
        return 1;
    }
    if (!strcmp(argv[i], "-mouse-protocol")) {
        if (i + 1 >= argc ||
            (strcmp(argv[i + 1], "sgr") && strcmp(argv[i + 1], "dec"))) {
            UseMsg();
            return 0;
        }
        sixelDecMouse = !strcmp(argv[i + 1], "dec");
        return 2;
    }
    if (!strcmp(argv[i], "-exec")) {
        if (i + 1 >= argc) {
            UseMsg();
            return 0;
        }
        kdExecuteCommand = argv[i + 1];
        return 2;
    }
    if (!strcmp(argv[i], "-backend")) {
        if (i + 1 >= argc) {
            UseMsg();
            return 0;
        }
#ifdef HAVE_CHAFA
        else if (!strcmp(argv[i + 1], "chafa")) {
            g_renderer = RENDERER_CHAFA;
        }
#endif
#ifdef HAVE_LIBSIXEL
        else if (!strcmp(argv[i + 1], "libsixel")) {
            g_renderer = RENDERER_LIBSIXEL;
        }
#endif
        else {
            UseMsg();
            return 0;
        }
        return 2;
    }
    return KdProcessArgument(argc, argv, i);
}

static void
sixelApplyTerminalSize(SIXEL_Driver *driver)
{
    sixelNormalizeScreenPixels(driver);
    if ((!driver->pixel_size_reply || (sixelIterm2Terminal && !sixelTmux)) &&
        driver->cell_pixel_w > 0 && driver->cell_pixel_h > 0 &&
        driver->cell_w > 0 && driver->cell_h > 0 &&
        driver->cell_pixel_w <= SHRT_MAX / driver->cell_w &&
        driver->cell_pixel_h <= SHRT_MAX / driver->cell_h) {
        driver->pixel_w = driver->cell_pixel_w * driver->cell_w;
        driver->pixel_h = driver->cell_pixel_h * driver->cell_h;
    }
#ifdef RANDR
    if (driver->auto_size && driver->screen->pScreen &&
        sixelValidSize(driver->pixel_w, driver->pixel_h) &&
        (driver->w != driver->pixel_w || driver->h != driver->pixel_h)) {
        RRScreenSize size = {0};
        size.width = driver->pixel_w;
        size.height = driver->pixel_h;
        if (!sixelRandRSetConfig(driver->screen->pScreen, RR_Rotate_0, 0, &size))
            ErrorF("Xsixel: could not resize to terminal dimensions %dx%d\n",
                   size.width, size.height);
        return;
    }
#endif
    SIXEL_Flip(driver);
    fflush(stdout);
}

/* A self-pipe wakes the server without doing X server work in SIGWINCH. */
static int sixelResizePipe[2] = {-1, -1};
static volatile sig_atomic_t sixelResizeWriteFd = -1;
static struct sigaction sixelPreviousWinch;
static Bool sixelWinchInstalled;
static OsTimerPtr sixelResizeTimer;

static void
sixelHandleWinch(int signo)
{
    int saved_errno = errno;
    int fd = sixelResizeWriteFd;
    char byte = 0;

    if (fd >= 0) {
        /* EAGAIN means the pipe already contains a wakeup. */
        while (write(fd, &byte, 1) < 0 && errno == EINTR)
            ;
    }
    errno = saved_errno;
}

static CARD32
sixelResizeTimeout(OsTimerPtr timer, CARD32 now, void *arg)
{
    SIXEL_Driver *driver = g_driver;

    if (!driver)
        return 0;
    /* Pixel and cell dimensions must belong to this terminal configuration.
     * In particular, do not reuse cell geometry after a font-size change. */
    driver->pixel_w = driver->pixel_h = 0;
    driver->reported_pixel_w = driver->reported_pixel_h = 0;
    driver->cell_w = driver->cell_h = 0;
    driver->cell_pixel_w = driver->cell_pixel_h = 0;
    driver->pixel_size_reported = FALSE;
    driver->pixel_size_reply = FALSE;
    sixelReadTerminalSize(driver);
    if (sixelValidSize(driver->pixel_w, driver->pixel_h))
        sixelApplyTerminalSize(driver);
    return 0;
}

static void
sixelResizeNotify(int fd, int ready, void *data)
{
    char bytes[128];
    ssize_t count;

    do {
        count = read(fd, bytes, sizeof(bytes));
    } while (count > 0 || (count < 0 && errno == EINTR));
    /* Restart the quiet period each time more resize signals arrive. */
    sixelResizeTimer = TimerSet(sixelResizeTimer, 0, 100,
                                sixelResizeTimeout, NULL);
}

static Bool
sixelStartResizeMonitor(void)
{
    struct sigaction action = {0};
    int i, flags;

    if (pipe(sixelResizePipe) < 0)
        return FALSE;
    for (i = 0; i < 2; i++) {
        flags = fcntl(sixelResizePipe[i], F_GETFL);
        if (flags < 0 ||
            fcntl(sixelResizePipe[i], F_SETFL, flags | O_NONBLOCK) < 0 ||
            fcntl(sixelResizePipe[i], F_SETFD, FD_CLOEXEC) < 0)
            goto fail;
    }
    if (!SetNotifyFd(sixelResizePipe[0], sixelResizeNotify, X_NOTIFY_READ, NULL))
        goto fail;
    action.sa_handler = sixelHandleWinch;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sixelResizeWriteFd = sixelResizePipe[1];
    if (sigaction(SIGWINCH, &action, &sixelPreviousWinch) < 0)
        goto fail;
    sixelWinchInstalled = TRUE;
    return TRUE;

fail:
    sixelStopResizeMonitor();
    return FALSE;
}

static void
sixelStopResizeMonitor(void)
{
    sigset_t blocked, previous;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blocked, &previous);
    sixelResizeWriteFd = -1;
    if (sixelWinchInstalled) {
        sigaction(SIGWINCH, &sixelPreviousWinch, NULL);
        sixelWinchInstalled = FALSE;
    }
    if (sixelResizeTimer) {
        TimerFree(sixelResizeTimer);
        sixelResizeTimer = NULL;
    }
    if (sixelResizePipe[0] >= 0) {
        RemoveNotifyFd(sixelResizePipe[0]);
        close(sixelResizePipe[0]);
        sixelResizePipe[0] = -1;
    }
    if (sixelResizePipe[1] >= 0) {
        close(sixelResizePipe[1]);
        sixelResizePipe[1] = -1;
    }
    sigprocmask(SIG_SETMASK, &previous, NULL);
}

static void
sixelEnableMouse(void)
{
    if (sixelDecMouse)
        printf("\033[1;1'z\033[1;3'{\033['w");
    else if (sixelTmux || sixelScreen)
        /* Both multiplexers forward SGR cells rather than 1016 pixels. */
        printf("\033[?1006h\033[?1003h");
    else
        printf("\033[?1006h\033[?1016h\033[?1003h");
    fflush(stdout);
}

static void
sixelMouseReport(sixel_key_t *key)
{
    int x, y, button, flags;
    Bool release = key->value == SIXEL_MOUSE_SGR_RELEASE;

    if (!sixelPointer)
        return;
    if (key->value == SIXEL_MOUSE_DEC) {
        if (!sixelDecMouse || key->nparams < 4 ||
            key->params[0] < 1 || key->params[0] > 10 ||
            key->params[1] > 15 ||
            !sixelValidSize(key->params[3], key->params[2]))
            return;
        x = key->params[3] - 1;
        y = key->params[2] - 1;
        /* DEC button bits are right, middle, left, fourth. */
        flags = key->params[1];
        mouseState = ((flags & 4) ? KD_BUTTON_1 : 0) |
                     ((flags & 2) ? KD_BUTTON_2 : 0) |
                     ((flags & 1) ? KD_BUTTON_3 : 0) |
                     ((flags & 8) ? KD_BUTTON_4 : 0);
        KdEnqueuePointerEvent(sixelPointer, mouseState, x, y, 0);
        /* The DEC motion filter is one-shot and must be armed again. */
        sixelEnableMouse();
        return;
    }
    if (sixelDecMouse || key->nparams != 3 || key->params[0] > 127 ||
        !sixelValidSize(key->params[1], key->params[2]))
        return;
    flags = key->params[0];
    x = key->params[1] - 1;
    y = key->params[2] - 1;
    if ((sixelTmux || sixelScreen) && g_driver &&
        g_driver->cell_w > 0 && g_driver->cell_h > 0) {
        int width = g_driver->pixel_w > 0 ? g_driver->pixel_w : g_driver->w;
        int height = g_driver->pixel_h > 0 ? g_driver->pixel_h : g_driver->h;

        /* Multiplexer SGR 1006 coordinates are relative to the pane, in cells. Place the pointer at
         * the cell centre; use current terminal geometry after each resize.
         * Outside tmux, 1016 continues to provide exact pixel coordinates. */
        x = ((int64_t)x * width + width / 2) / g_driver->cell_w;
        y = ((int64_t)y * height + height / 2) / g_driver->cell_h;
    }
    button = flags & 3;
    if (flags & 64) {
        if (!release) {
            int wheel = KD_BUTTON_4 << button;
            KdEnqueuePointerEvent(sixelPointer, mouseState | wheel, x, y, 0);
            KdEnqueuePointerEvent(sixelPointer, mouseState, x, y, 0);
        }
        return;
    }
    if (button < 3) {
        if (release)
            mouseState &= ~(KD_BUTTON_1 << button);
        else
            mouseState |= KD_BUTTON_1 << button;
    } else if (!(flags & 32)) {
        mouseState = 0;
    }
    /* Queue motion at the new position before any button transition. */
    KdEnqueuePointerEvent(sixelPointer, mouseState, x, y, 0);
}

static void sixelPollInput(void)
{
    int posted = 0;
    Bool size_changed = FALSE;
#if SIXEL_DEBUG
    static int events = 0;
#endif
    char buf[4096];
    static sixel_key_t keys[4097];
    int scancode;
    sixel_key_t *key;
    int nread, nkeys;
    int i;
    int state;

    nread = get_input(buf, sizeof(buf));
    if (nread > 0) {
        nkeys = getkeys(buf, nread, keys);
        if (nkeys >= sizeof(keys) / sizeof(*keys)) {
            nkeys = sizeof(keys) / sizeof(*keys) - 1;
        }
        for (i = 0; i < nkeys; ++i) {
            key = keys + i;
            switch (key->value) {
            case SIXEL_DTTERM_SEQS:
                if (g_driver && key->nparams == 3 &&
                    sixelValidSize(key->params[2], key->params[1])) {
                    switch (key->params[0]) {
                    case 4:
                        g_driver->pixel_size_reported = TRUE;
                        g_driver->pixel_size_reply = TRUE;
                        g_driver->pixel_h = key->params[1];
                        g_driver->pixel_w = key->params[2];
                        g_driver->reported_pixel_w = key->params[2];
                        g_driver->reported_pixel_h = key->params[1];
                        size_changed = TRUE;
                        break;
                    case 6:
                        g_driver->cell_pixel_h = key->params[1];
                        g_driver->cell_pixel_w = key->params[2];
                        size_changed = TRUE;
                        break;
                    case 8:
                        g_driver->cell_h = key->params[1];
                        g_driver->cell_w = key->params[2];
                        size_changed = TRUE;
                        break;
                    default:
                        break;
                    }
                }
                break;

            case SIXEL_MOUSE_DEC:
            case SIXEL_MOUSE_SGR:
            case SIXEL_MOUSE_SGR_RELEASE:
                sixelMouseReport(key);
                break;

            case SIXEL_FKEYS:
                /* TODO: modifyFunctionKeys */
                switch (key->params[0]) {
                case 2:  scancode = 110+8; break;
                case 3:  scancode = 111+8; break;
                case 5:  scancode = 104+8; break;
                case 6:  scancode = 109+8; break;
                case 7:  scancode = 102+8; break;
                case 8:  scancode = 107+8; break;
                case 11: scancode =  59+8; break;
                case 12: scancode =  60+8; break;
                case 13: scancode =  61+8; break;
                case 14: scancode =  62+8; break;
                case 15: scancode =  63+8; break;
                case 17: scancode =  64+8; break;
                case 18: scancode =  65+8; break;
                case 19: scancode =  66+8; break;
                case 20: scancode =  67+8; break;
                case 21: scancode =  68+8; break;
                case 23: scancode =  87+8; break;
                case 24: scancode =  88+8; break;
                default:
                    scancode = 0;
                    break;
                }
                if (key->nparams == 2) {
                    key->params[1]--;
                    SendModifierKey(key->params[1], 0);
                }
                KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                if (key->nparams == 2) {
                    SendModifierKey(key->params[1], 1);
                }
                KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                break;
            case SIXEL_FOCUSIN:
                SIXEL_Flip(g_driver);
                break;
            case SIXEL_FOCUSOUT:
                /* TODO: */
                /* SIXEL_Flip(g_driver); */
                break;
            default:
                if ((key->value >= SIXEL_UP && key->value <= SIXEL_LEFT) ||
                    (key->value >= SIXEL_END && key->value <= SIXEL_HOME) ||
                    (key->value >= SIXEL_F1 && key->value <= SIXEL_F4)) {
                    /* TODO: modifyCursorKeys, modifyOtherKeys */
                    switch(key->value) {
                    case SIXEL_UP:    scancode = 103+8; break;
                    case SIXEL_DOWN:  scancode = 108+8; break;
                    case SIXEL_RIGHT: scancode = 106+8; break;
                    case SIXEL_LEFT:  scancode = 105+8; break;
                    case SIXEL_HOME:  scancode = 102+8; break;
                    case SIXEL_END:   scancode = 107+8; break;
                    case SIXEL_F1:    scancode =  59+8; break;
                    case SIXEL_F2:    scancode =  60+8; break;
                    case SIXEL_F3:    scancode =  61+8; break;
                    case SIXEL_F4:    scancode =  62+8; break;
                    default:
                        scancode = 0;
                        break;
                    }
                    if (key->nparams >= 1) {
                        key->params[key->nparams-1]--;
                        posted += SendModifierKey(key->params[key->nparams-1], 0);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                    if (key->nparams >= 1) {
                        posted += SendModifierKey(key->params[key->nparams-1], 1);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                }
                else {
                    state = GetState(key->value);
                    scancode = TranslateKey(key->value);
                    if (state) {
                        SendModifierKey(state, 0);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                    if (state) {
                        SendModifierKey(state, 1);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                    if (key->value == 0x0c) {
                        SIXEL_Flip(g_driver);
                    }
                }
                break;
            }
        }
    }
    if (g_driver && size_changed)
        sixelApplyTerminalSize(g_driver);

}

static int sixelInit(void)
{
    tty_raw();
    printf("\033[H");
    printf("\033[?1004h");
    printf("\033[?25l");
    /* Save the terminal's SIXEL mode and prevent bottom-edge scrolling. */
    sixelTerminalControl("\033[?80s\033[?80h");
    /* Request the strongest terminal fallback; XFixes handles motion too. */
    printf("\033[>3p");
    mouseState = 0;
    sixelEnableMouse();

    return 0;
}


static void sixelFini(void)
{
    fd_set fdset;
    struct timeval timeout;
    char buf[4096];

    sixelStopRenderModeControl();
    sixelStopScreenMonitor();
    sixelStopResizeMonitor();
    if (sixelHostCursorFd >= 0) {
        RemoveNotifyFd(sixelHostCursorFd);
        sixelHostCursorFd = -1;
    }
    sixelHostCursorFini();
    if (sixelFrameTimer) {
        TimerFree(sixelFrameTimer);
        sixelFrameTimer = NULL;
    }
    sixelFramePending = FALSE;
    if (sixelDamageInitialized) {
        RegionUninit(&sixelPendingDamage);
        sixelDamageInitialized = FALSE;
    }
    printf("\033\\");
    fflush(stdout);
    if (sixelDecMouse)
        printf("\033[0'z\033[2;4'{");
    else
        printf("\033[?1003l\033[?1016l\033[?1006l");
    sixelTerminalControl("\033[?80r");
    printf("\033[>0p");
    printf("\033[?25h");
    printf("\033[?1004l");

    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == 1) {
        while (read(STDIN_FILENO, buf, sizeof(buf))) {
            ;
        }
    }
    tty_restore();

    if (g_driver) {
#ifdef HAVE_CHAFA
        if (g_driver->chafa_canvas)
            chafa_canvas_unref(g_driver->chafa_canvas);
        if (g_driver->chafa_cfg)
            chafa_canvas_config_unref(g_driver->chafa_cfg);
        if (g_driver->chafa_tinfo)
            chafa_term_info_unref(g_driver->chafa_tinfo);
#endif
#ifdef HAVE_LIBSIXEL
        if (g_driver->dither)
            sixel_dither_unref(g_driver->dither);
        if (g_driver->output)
            sixel_output_unref(g_driver->output);
        free(g_driver->encoded);
#endif
        free(g_driver->buffer);
        free(g_driver->bitmap);
        free(g_driver);
        g_driver = NULL;
    }
}

static void
sixelBell(int volume, int pitch, int duration)
{
    if (volume && pitch)
        printf("\007");
}

void CloseInput(void)
{
    KdCloseInput();
    sixelFini();
}

static void sixelNotifyFd(int fd, int ready, void *data)
{
    (void)fd; (void)ready; (void)data;
    sixelPollInput();
}

static void
sixelHostCursorNotify(int fd, int ready, void *data)
{
    sixelHostCursorDispatch();
}

static int sixelOsInit(void)
{
    return 1;
}

static const KdOsFuncs sixelOsFuncs = {
    .Init = sixelOsInit,
    .Bell = sixelBell,
};

void ddxInit(void)
{
    sixelDetectIterm2();
    sixelCheckTmux();
    /* TMUX takes precedence; nested multiplexers are not supported. */
    sixelScreen = !sixelTmux && getenv("STY") && *getenv("STY");
    /* Initialize terminal + sixel, then hook stdin for input */
    KdOsInit(&sixelOsFuncs);
    sixelInit();
    sixelHostCursorFd = sixelHostCursorInit();
    if (sixelHostCursorFd >= 0) {
        if (!SetNotifyFd(sixelHostCursorFd, sixelHostCursorNotify, X_NOTIFY_READ, NULL)) {
            sixelHostCursorFini();
            sixelHostCursorFd = -1;
        } else {
            sixelHostCursorDispatch();
        }
    }
    sixelStartScreenMonitor();
    SetNotifyFd(STDIN_FILENO, sixelNotifyFd, X_NOTIFY_READ, NULL);
    if (!sixelStartResizeMonitor())
        FatalError("Xsixel: could not monitor terminal size changes\n");
}

/* Input thread hook (may be called by os/inputthread.c) */
void ddxInputThreadInit(void)
{
    /* Nothing special needed for sixel */
}
