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

/* With screen, xterm's ioctl dimensions can include its internal border.
 * SIXEL is clipped to whole cells.  Keep the unrounded report for replies
 * arriving in either order. */
void
sixelXtermNormalize(SIXEL_Driver *driver)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_XTERM) || !driver->pixel_size_reported)
		return;
	driver->pixel_w = driver->reported_pixel_w;
	driver->pixel_h = driver->reported_pixel_h;
	if (driver->cell_w > 0 && driver->pixel_w >= driver->cell_w)
		driver->pixel_w -= driver->pixel_w % driver->cell_w;
	if (driver->cell_h > 0 && driver->pixel_h >= driver->cell_h)
		driver->pixel_h -= driver->pixel_h % driver->cell_h;
}

void
sixelXtermPointerStart(void)
{
	if (sixelClientEnabled(SIXEL_CLIENT_XTERM))
		fputs("\033[>3p", stdout);
}

void
sixelXtermPointerStop(void)
{
	sixelHostCursorFini();
	if (sixelClientEnabled(SIXEL_CLIENT_XTERM))
		fputs("\033[>0p", stdout);
}

int
sixelXtermHostCursor(void)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_XTERM))
		return (-1);
	return (sixelHostCursorInit());
}
