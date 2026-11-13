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

static Bool
sixelPatchCells(SIXEL_Driver *driver, int *cw, int *ch)
{
	double w, h;

	if ((sixelClient.render != &sixelIterm2Render && driver->output_scale == 1) ||
	    sixelClient.render == &sixelKonsoleRender ||
	    !sixelClient.iterm2 || sixelClient.tmux || sixelClient.full_redraw ||
	    !driver->iterm_cell_scale || driver->cell_w <= 0 || driver->cell_h <= 0)
		return FALSE;
	w = driver->iterm_cell_w * driver->iterm_cell_scale / driver->output_scale;
	h = driver->iterm_cell_h * driver->iterm_cell_scale / driver->output_scale;
	*cw = (int)(w + 0.5);
	*ch = (int)(h + 0.5);
	return *cw > 0 && *ch > 0 && fabs(w - *cw) < 0.001 &&
	    fabs(h - *ch) < 0.001 &&
	    (int64_t)*cw * driver->cell_w == driver->w &&
	    (int64_t)*ch * driver->cell_h == driver->h;
}

static void
sixelRetinaPixels(SIXEL_Driver *driver, int x1, int y1, int width, int height)
{
	const unsigned char *palette = sixel_dither_get_palette(driver->dither);
	int colors = sixel_dither_get_num_of_palette_colors(driver->dither);

	for (int y = 0; y < height; y++) {
		const unsigned char *src = driver->buffer +
		(size_t)(y1 + y) * driver->pitch + x1 * 4;
		unsigned char *row = driver->bitmap + (size_t)y * width * 4;

		for (int x = 0; x < width; x++, src += 4) {
			/*
			 * Match the RGB555 bucket resolution of libsixel's
			 * built-in palette lookup. First use chooses the
			 * nearest palette entry; subsequent patches keep
			 * that choice, including at cell edges.
			 */
			unsigned int key = (src[2] >> 3) << 10 |
			(src[1] >> 3) << 5 | (src[0] >> 3);
			unsigned short *cached = &driver->retina_palette_cache[key];

			if (!*cached) {
				int distance = INT_MAX;

				for (int i = 0; i < colors; i++) {
					int r = src[2] - palette[i * 3];
					int g = src[1] - palette[i * 3 + 1];
					int b = src[0] - palette[i * 3 + 2];
					int d = r * r + g * g + b * b;

					if (d < distance) {
						distance = d;
						*cached = i + 1;
					}
				}
			}
			row[x * 2] = row[x * 2 + 1] = *cached - 1;
		}
		memcpy(row + width * 2, row, width * 2);
	}
}

void
sixelIterm2Detect(void)
{
	const char *program, *terminal;

	if (!sixelClientEnabled(SIXEL_CLIENT_ITERM2))
		return;
	program = getenv("TERM_PROGRAM");
	terminal = getenv("LC_TERMINAL");
	if ((program != NULL && !strcmp(program, "iTerm.app")) ||
	    (terminal != NULL && !strcmp(terminal, "iTerm2"))) {
		sixelClient.iterm2 = TRUE;
		if (!sixelClient.explicit_mode)
			sixelClientSelect(&sixelIterm2Render, FALSE);
	}
}

Bool
sixelIterm2Identity(const char *text)
{
	if (!sixelClientEnabled(SIXEL_CLIENT_ITERM2) || sixelClient.tmux ||
	    strncmp(text, ">|iTerm2 ", 9))
		return (FALSE);
	sixelClient.iterm2 = TRUE;
	if (!sixelClient.explicit_mode)
		sixelClientSelect(&sixelIterm2Render, FALSE);
	/* iTerm2 implements 1016 but reports it permanently disabled. */
	if (!sixelClient.screen && !sixelClient.dec_mouse) {
		sixelClient.mouse_pending = FALSE;
		sixelClient.mouse_cells = FALSE;
		sixelClient.callbacks.enable_mouse();
	}
	sixelIterm2Query();
	fflush(stdout);
	return (TRUE);
}

void
sixelIterm2Query(void)
{
	/* Inside tmux this query supplies backing-cell geometry only. */
	if (sixelClientEnabled(SIXEL_CLIENT_ITERM2) &&
	    (sixelClient.iterm2 || sixelClient.tmux) && !sixelClient.cell_pending) {
		sixelClient.cell_pending = TRUE;
		sixelClientControl("\033]1337;ReportCellSize\007");
	}
}

const char *
sixelIterm2ReplyPrefix(void)
{
	return ("1337;ReportCellSize=");
}

Bool
sixelIterm2Cell(SIXEL_Driver *driver, const char *text, Bool palette_renderer)
{
	double h, w, scale;
	int used = 0;

	if (!sixelClientEnabled(SIXEL_CLIENT_ITERM2) ||
	    !sixelClient.cell_pending || driver == NULL ||
	    sscanf(text, "1337;ReportCellSize=%lf;%lf;%lf%n", &h, &w, &scale,
	    &used) != 3 || text[used] != '\0' || !isfinite(w) ||
	    !isfinite(h) || !isfinite(scale) || w < 1 || h < 1 ||
	    w > SHRT_MAX || h > SHRT_MAX || (scale != 1 && scale != 2))
		return (FALSE);
	sixelClient.cell_pending = FALSE;
	if (!sixelClient.tmux)
		sixelClient.detected_scale = palette_renderer ? (int)scale : 1;
	driver->iterm_cell_w = w;
	driver->iterm_cell_h = h;
	driver->iterm_cell_scale = (int)scale;
	driver->cell_pixel_w = (int)w;
	driver->cell_pixel_h = (int)h;
	return (TRUE);
}

void
sixelIterm2Normalize(SIXEL_Driver *driver)
{
	double w, h;

	if (sixelClient.tmux || !driver->iterm_cell_scale ||
	    driver->cell_w <= 0 || driver->cell_h <= 0)
		return;
	w = driver->iterm_cell_w * driver->cell_w * driver->iterm_cell_scale;
	h = driver->iterm_cell_h * driver->cell_h * driver->iterm_cell_scale;
	if (w >= 1 && h >= 1 && w <= SHRT_MAX && h <= SHRT_MAX) {
		driver->pixel_w = driver->reported_pixel_w = (int)(w + 0.5);
		driver->pixel_h = driver->reported_pixel_h = (int)(h + 0.5);
		driver->pixel_size_reported = TRUE;
	}
}

void
sixelIterm2Mouse(SIXEL_Driver *driver, int *x, int *y)
{
	double width, height;

	if (!sixelClient.iterm2 || driver->cell_pixel_w <= 0 ||
	    driver->cell_pixel_h <= 0 || driver->cell_w <= 0 || driver->cell_h <= 0)
		return;
	width = (driver->iterm_cell_scale ? driver->iterm_cell_w :
	    driver->cell_pixel_w) * driver->cell_w;
	height = (driver->iterm_cell_scale ? driver->iterm_cell_h :
	    driver->cell_pixel_h) * driver->cell_h;
	*x = (int64_t)*x * (driver->pixel_w / driver->output_scale) / width;
	*y = (int64_t)*y * (driver->pixel_h / driver->output_scale) / height;
}

static void
iterm2_plan(SIXEL_Driver *driver, const BoxRec *damage, Bool part,
    struct sixel_image *image)
{
	int cw, ch;

	image->box = *damage;
	if (sixelPatchCells(driver, &cw, &ch)) {
		image->box.x1 = 0;
		image->box.x2 = driver->w;
		image->box.y1 = damage->y1 / ch * ch;
		image->box.y2 = min(driver->h, (damage->y2 + ch - 1) / ch * ch);
		image->positioned = TRUE;
		image->col = 1;
		image->row = image->box.y1 / ch + 1;
	} else {
		if (!part)
			image->box = (BoxRec){0, 0, driver->w, driver->h};
		image->box.y1 = image->box.y1 / 6 * 6;
		image->box.y2 = min(driver->h, (image->box.y2 + 5) / 6 * 6);
	}
	image->home_mode = sixelClient.iterm2 && !sixelClient.tmux;
}

static BoxRec
iterm2_bounds(SIXEL_Driver *driver, const BoxRec *damage)
{
	struct sixel_image image = {0};

	iterm2_plan(driver, damage, TRUE, &image);
	return (image.box);
}

static Bool
iterm2_full_damage(SIXEL_Driver *driver)
{
	int cw, ch;

	return (!sixelPatchCells(driver, &cw, &ch));
}

Bool
sixelIterm2Pixels(SIXEL_Driver *driver, struct sixel_image *image)
{
	int width, height, x1, y1, x, y;
	Bool indexed = FALSE;
	unsigned char *src, *dst;

	if (driver->output_scale != 2)
		return (FALSE);
	x1 = image->box.x1;
	y1 = image->box.y1;
	width = image->box.x2 - x1;
	height = image->box.y2 - y1;
	if (sixelClient.iterm2 && !sixelClient.tmux) {
		if (driver->retina_palette_cache == NULL)
			driver->retina_palette_cache = calloc(32768, sizeof(unsigned short));
		indexed = driver->retina_palette_cache != NULL;
	}
	image->converted = TRUE;	/* The compatibility hook converted
					 * the pixels. */
	if (indexed) {
		sixelRetinaPixels(driver, x1, y1, width, height);
		return (TRUE);
	}
	for (y = 0; y < height; y++) {
		src = driver->buffer + (size_t)(y + y1) * driver->pitch + x1 * 4;
		dst = driver->bitmap + (size_t)y * width * 3;
		for (x = 0; x < width; x++, src += 4, dst += 3) {
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
		}
	}
	/* Expand backwards so unread source pixels remain available. */
	for (y = height - 1; y >= 0; y--) {
		src = driver->bitmap + (size_t)y * width * 3;
		dst = driver->bitmap + (size_t)y * width * 12;
		for (x = width - 1; x >= 0; x--) {
			unsigned char rgb[3];

			memcpy(rgb, src + x * 3, 3);
			memcpy(dst + x * 6, rgb, 3);
			memcpy(dst + x * 6 + 3, rgb, 3);
		}
		memcpy(dst + width * 6, dst, width * 6);
	}
	return (FALSE);
}

const struct sixel_render_hooks sixelIterm2Render = {
	.name = "iterm2",
	.owner = SIXEL_CLIENT_ITERM2,
	.plan = iterm2_plan,
	.bounds = iterm2_bounds,
	.full_damage = iterm2_full_damage
};
