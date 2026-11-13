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
#include "sixel-private.h"

static Bool
sixelRenderRectPart(SIXEL_Driver *driver, BoxPtr damage, Bool part)
{
	struct sixel_image image = {0};
	BoxRec first, second;
	int x1, y1, x2, y2, x, y, width, height;
	Bool indexed;
	size_t body, end;
	SIXELSTATUS status;
	enum sixel_image_limit limit;

	sixelClientPlan(driver, damage, part, &image);
	x1 = image.box.x1;
	y1 = image.box.y1;
	x2 = image.box.x2;
	y2 = image.box.y2;
	if (x1 >= x2 || y1 >= y2)
		return TRUE;
	width = x2 - x1;
	height = y2 - y1;
	image.full = x1 == 0 && y1 == 0 && x2 == driver->w && y2 == driver->h;
	indexed = sixelClientConvertPixels(driver, &image);
	for (y = y1; !image.converted && y < y2; y++) {
		unsigned char *src = driver->buffer + (size_t)y * driver->pitch + x1 * 4;
		unsigned char *dst = driver->bitmap + (size_t)(y - y1) * width * 3;

		for (x = 0; x < width; x++, src += 4, dst += 3) {
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
		}
	}
	width *= driver->output_scale;
	height *= driver->output_scale;
	x1 *= driver->output_scale;
	y1 *= driver->output_scale;
	x2 *= driver->output_scale;
	y2 *= driver->output_scale;
	driver->encoded_size = 0;
	driver->encode_failed = FALSE;
	if (sixelClientNoDiffusion())
		sixel_dither_set_diffusion_type(driver->dither, SIXEL_DIFFUSE_NONE);
	if (indexed)
		sixel_dither_set_pixelformat(driver->dither, SIXEL_PIXELFORMAT_PAL8);
	status = sixel_encode(driver->bitmap, width, height, 3,
	    driver->dither, driver->output);
	if (indexed)
		sixel_dither_set_pixelformat(driver->dither, SIXEL_PIXELFORMAT_RGB888);
	if (SIXEL_FAILED(status) || driver->encode_failed)
		return FALSE;
	/*
	 * Keep libsixel's palette and pixel data, replacing only the
	 * envelope and raster attributes. No RGB pixels are allocated for
	 * the transparent gap.
	 */
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
	image.box = (BoxRec){x1, y1, x2, y2};
	image.width = width;
	image.height = height;
	image.body = body;
	image.end = end;
	limit = sixelClientImageLimit(driver, &image, &first, &second);
	switch (limit) {
	case SIXEL_IMAGE_SPLIT:
		return sixelRenderRectPart(driver, &first, TRUE) &&
		    sixelRenderRectPart(driver, &second, TRUE);
	case SIXEL_IMAGE_STOP:
		return TRUE;
	case SIXEL_IMAGE_ERROR:
		return FALSE;
	case SIXEL_IMAGE_OK:
		return sixelClientWriteImage(driver, &image);
	}
	return FALSE;
}

Bool
sixelRenderRect(SIXEL_Driver *driver, BoxPtr damage)
{
	return sixelRenderRectPart(driver, damage, FALSE);
}

static int64_t
sixelRectCost(SIXEL_Driver *driver, const BoxRec *box)
{
	return (int64_t)(box->x2 - box->x1) * (box->y2 - box->y1) *
	    driver->output_scale * driver->output_scale + 4096;
}

static BoxRec
sixelRectUnion(const BoxRec *a, const BoxRec *b)
{
	return ((BoxRec){min(a->x1, b->x1), min(a->y1, b->y1),
	    max(a->x2, b->x2), max(a->y2, b->y2)});
}

Bool
sixelRenderDamage(SIXEL_Driver *driver, RegionPtr damage)
{
	enum {
	MAX_IMAGES = 64};
BoxRec boxes[MAX_IMAGES], extent = *RegionExtents(damage);
	BoxPtr rects = RegionRects(damage);
	int count = 0, n = RegionNumRects(damage);
	int64_t cost = 0;

	if (sixelClientFullDamage(driver))
		return sixelRenderRect(driver, &extent);
	extent = sixelClientBounds(driver, &extent);
	for (int i = 0; i < n; i++) {
		BoxRec box = rects[i];
		int best = -1;
		int64_t saving = -1;

		box = sixelClientBounds(driver, &box);
		/*
		 * Merge only when the saved image overhead pays for the
		 * extra area.
		 */
		for (int j = 0; j < count; j++) {
			BoxRec merged = sixelRectUnion(&box, &boxes[j]);
			int64_t s = sixelRectCost(driver, &box) +
			sixelRectCost(driver, &boxes[j]) -
			sixelRectCost(driver, &merged);

			if (s > saving) {
				saving = s;
				best = j;
			}
		}
		if (best >= 0)
			boxes[best] = sixelRectUnion(&boxes[best], &box);
		else if (count < MAX_IMAGES)
			boxes[count++] = box;
		else
			return sixelRenderRect(driver, &extent);
	}
	for (int i = 0; i < count; i++)
		cost += sixelRectCost(driver, &boxes[i]);
	if (cost >= sixelRectCost(driver, &extent))
		return sixelRenderRect(driver, &extent);
	for (int i = 0; i < count; i++) {
		if (!sixelRenderRect(driver, &boxes[i]))
			return FALSE;
	}
	return TRUE;
}
