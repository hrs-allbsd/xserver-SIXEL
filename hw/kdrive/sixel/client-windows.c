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

/* Windows ssh.exe may supply a plausible but stale ioctl pixel size.  These
 * xterm-compatible probes also serve terminals that SSH cannot identify. */
void
sixelWindowsQuery(void)
{
	if (sixelClientEnabled(SIXEL_CLIENT_WINDOWS))
		sixelClientControl("\033[14t\033[16t");
}

void
sixelWindowsNormalize(SIXEL_Driver *driver)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_WINDOWS) || sixelClient.tmux ||
	    driver->pixel_size_reply ||
	    (sixelClient.iterm2 && driver->pixel_size_reported) ||
	    driver->cell_pixel_w <= 0 || driver->cell_pixel_h <= 0 ||
	    driver->cell_w <= 0 || driver->cell_h <= 0 ||
	    driver->cell_pixel_w > SHRT_MAX / driver->cell_w / driver->output_scale ||
	    driver->cell_pixel_h > SHRT_MAX / driver->cell_h / driver->output_scale)
		return;
	driver->pixel_w = driver->cell_pixel_w * driver->cell_w * driver->output_scale;
	driver->pixel_h = driver->cell_pixel_h * driver->cell_h * driver->output_scale;
}

void
sixelWindowsMouseQuery(void)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_WINDOWS) ||
	    sixelClientWrapped() || sixelClient.dec_mouse || sixelClient.iterm2)
		return;
	sixelClient.mouse_pending = TRUE;
	fputs("\033[?1016$p", stdout);
	fflush(stdout);
}
