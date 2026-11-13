/*
 * Copyright © 2004 PillowElephantBadgerBankPond
 * Copyright © 2014 Sergii Pylypenko
 * Copyright © 2014 Hayaki Saito
 * Copyright © 2026 Hiroki Sato <hrs@allbsd.org>
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
#include "client-private.h"

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

static OsTimerPtr refresh_timer;

enum {
SCREEN_REFRESH_INTERVAL = 2000};

void
sixelScreenInit(void)
{
	const char *sty;

	sty = getenv("STY");
	if (sixelClient.tmux || !sixelClientEnabled(SIXEL_CLIENT_SCREEN) ||
	    (!sixelClientForced(SIXEL_CLIENT_SCREEN) && (sty == NULL || !*sty)))
		return;
	sixelClient.screen = TRUE;
	sixelClient.transport = &sixelScreenTransport;
}

static void
screen_control(const char *text)
{
	sixelScreenWrite(text, strlen(text));
}

void
sixelScreenNormalize(SIXEL_Driver *driver)
{
	if (sixelClient.screen)
		sixelXtermNormalize(driver);
}

void
sixelScreenStop(void)
{
	if (refresh_timer) {
		TimerFree(refresh_timer);
		refresh_timer = NULL;
	}
}

static CARD32
sixelScreenPoll(OsTimerPtr timer, CARD32 now, void *arg)
{
	if (sixelClient.driver) {
		/*
		 * Reattachment may use a different terminal with default
		 * modes.
		 */
		sixelClientControl("\033[?80h");
		sixelClient.callbacks.redraw();
	}
	return SCREEN_REFRESH_INTERVAL;
}

void
sixelScreenStart(void)
{
	if (!sixelClient.screen)
		return;
	refresh_timer = TimerSet(NULL, 0, SCREEN_REFRESH_INTERVAL,
	    sixelScreenPoll, NULL);
	if (!refresh_timer)
		ErrorF("Xsixel: could not start screen automatic redraw monitor\n");
}

Bool
sixelScreenReplay(SIXEL_Driver *driver)
{
	if (!sixelClient.screen || !driver->screen_frame_valid)
		return (FALSE);
	sixelScreenWrite((char *)driver->screen_frame, driver->screen_frame_size);
	return (TRUE);
}

static Bool
screen_image(SIXEL_Driver *driver, struct sixel_image *image)
{
	FILE *out;
	char *data = NULL;
	size_t length = 0;

	out = open_memstream(&data, &length);
	if (out == NULL)
		return (FALSE);
	if (!image->positioned)
		fputs("\033[H", out);
	sixelClientDirectImage(driver, image, out);
	if (fclose(out) != 0) {
		free(data);
		return (FALSE);
	}
	sixelScreenWrite(data, length);
	/*
	 * Cache complete envelopes, bounded independently of the
	 * framebuffer.
	 */
	if (image->full && length <= 16 * 1024 * 1024) {
		free(driver->screen_frame);
		driver->screen_frame = (unsigned char *)data;
		driver->screen_frame_size = length;
		driver->screen_frame_valid = TRUE;
	} else
		free(data);
	return (TRUE);
}

const struct sixel_transport_hooks sixelScreenTransport = {
	.control = screen_control,
	.write_image = screen_image,
	.cells = TRUE
};
