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
#ifndef SIXEL_PRIVATE_H
#define SIXEL_PRIVATE_H
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
#ifdef CHAFA_PIXEL_BGRA8_UNPREMULTIPLIED
#define CHAFA_PIXEL_BGRA8_UNASSOCIATED CHAFA_PIXEL_BGRA8_UNPREMULTIPLIED
#else
#define CHAFA_PIXEL_BGRA8_UNASSOCIATED CHAFA_PIXEL_BGRA8_PREMULTIPLIED
#endif
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
#include <pthread.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>

#include "client.h"

struct sixel_driver {
	int	w;
	int	h;
	int	pitch;
	int	output_scale;
	int	pixel_w, pixel_h;
	int	reported_pixel_w, reported_pixel_h;
	int	cell_w, cell_h;
	int	cell_pixel_w, cell_pixel_h;
	double	iterm_cell_w, iterm_cell_h;
	int	iterm_cell_scale;
	Bool	auto_size;
	Bool	pixel_size_reported;
	Bool	pixel_size_reply;	/* Explicit 14t reply, rather than
					 * ioctl fallback. */
	KdScreenInfo *screen;
	Rotation randr;
	Bool	shadow;
	unsigned char *buffer;
	unsigned char *bitmap;
	/* libsixel backend */
#ifdef HAVE_LIBSIXEL
	sixel_dither_t *dither;
	sixel_output_t *output;
	unsigned char *encoded;
	size_t	encoded_size, encoded_capacity;
	Bool	encode_failed;
	unsigned char *screen_frame;
	size_t	screen_frame_size;
	Bool	screen_frame_valid;
	unsigned short *retina_palette_cache;
#endif
	/* chafa backend */
#ifdef HAVE_CHAFA
	ChafaCanvasConfig *chafa_cfg;
	ChafaCanvas *chafa_canvas;
	ChafaTermInfo *chafa_tinfo;
#endif
};

Bool	sixelRenderRect(SIXEL_Driver *, BoxPtr);
Bool	sixelRenderDamage(SIXEL_Driver *, RegionPtr);
#endif
