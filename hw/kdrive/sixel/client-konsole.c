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

static BoxRec
konsole_bounds(SIXEL_Driver *driver, const BoxRec *damage)
{
	BoxRec box;
	int cw, ch;

	box = (BoxRec){0, 0, driver->w, driver->h};
	if (driver->cell_w <= 0 || driver->cell_h <= 0 ||
	    driver->w % driver->cell_w || driver->h % driver->cell_h ||
	    driver->pixel_w != driver->w * driver->output_scale ||
	    driver->pixel_h != driver->h * driver->output_scale)
		return (box);
	cw = driver->w / driver->cell_w;
	ch = driver->h / driver->cell_h;
	if (cw <= 0 || ch <= 0)
		return (box);
	/*
	 * SixelCharacterAdd() extends Konsole's actual image height to the
	 * entire last six-pixel band, even with shorter raster attributes.
	 * Include those pixels rather than leaving a black strip below it.
	 */
	if (ch % 2 != 0)
		ch *= 2;
	if (ch % 3 != 0)
		ch *= 3;
	box.x2 = min(driver->w, (damage->x2 + cw - 1) / cw * cw);
	box.y2 = min(driver->h, (damage->y2 + ch - 1) / ch * ch);
	return (box);
}

static void
konsole_plan(SIXEL_Driver *driver, const BoxRec *damage, Bool part,
    struct sixel_image *image)
{
	image->box = konsole_bounds(driver, damage);
	image->opaque = TRUE;
	image->home_mode = !sixelClient.tmux;
}

static Bool
konsole_full_damage(SIXEL_Driver *driver)
{
	return (FALSE);
}

void
sixelKonsoleDetect(void)
{
	const char *version;

	version = getenv("KONSOLE_VERSION");
	if (sixelClientEnabled(SIXEL_CLIENT_KONSOLE) &&
	    !sixelClient.explicit_mode && version != NULL && *version != '\0') {
		sixelClient.iterm2 = FALSE;
		sixelClientSelect(&sixelKonsoleRender, FALSE);
	}
}

Bool
sixelKonsoleIdentity(const char *text)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_KONSOLE) ||
	    sixelClient.explicit_mode || strncmp(text, ">|Konsole ", 10))
		return (FALSE);
	sixelClientSelect(&sixelKonsoleRender, FALSE);
	return (TRUE);
}

const struct sixel_render_hooks sixelKonsoleRender = {
	.name = "konsole",
	.owner = SIXEL_CLIENT_KONSOLE,
	.plan = konsole_plan,
	.bounds = konsole_bounds,
	.full_damage = konsole_full_damage
};
