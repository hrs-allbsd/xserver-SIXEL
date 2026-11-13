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
#ifndef SIXEL_CLIENT_PRIVATE_H
#define SIXEL_CLIENT_PRIVATE_H

#include "sixel-private.h"

enum sixel_client_id {
	SIXEL_CLIENT_TMUX,
	SIXEL_CLIENT_SCREEN,
	SIXEL_CLIENT_XTERM,
	SIXEL_CLIENT_MLTERM,
	SIXEL_CLIENT_ITERM2,
	SIXEL_CLIENT_WINDOWS,
	SIXEL_CLIENT_KONSOLE,
	SIXEL_CLIENT_COUNT
};

enum sixel_compat_policy {
	SIXEL_COMPAT_AUTO,
	SIXEL_COMPAT_ON,
	SIXEL_COMPAT_OFF
};

struct sixel_render_hooks {
	const char *name;
	enum sixel_client_id owner;
	void	(*plan)(SIXEL_Driver *, const BoxRec *, Bool, struct sixel_image *);
	BoxRec	(*bounds)(SIXEL_Driver *, const BoxRec *);
	Bool	(*full_damage)(SIXEL_Driver *);
};

struct sixel_transport_hooks {
	void	(*control)(const char *);
	Bool	(*write_image)(SIXEL_Driver *, struct sixel_image *);
	enum sixel_image_limit (*limit)(SIXEL_Driver *, struct sixel_image *,
	    BoxRec *, BoxRec *);
	Bool	cells, coalesce_size;
};

struct sixel_client {
	enum sixel_compat_policy policy[SIXEL_CLIENT_COUNT];
	const struct sixel_transport_hooks *transport;
	const struct sixel_render_hooks *render;
	struct sixel_client_callbacks callbacks;
	SIXEL_Driver *driver;
	Bool	explicit_mode, tmux, screen, iterm2;
	Bool	identity_pending, cell_pending, mouse_pending, mouse_cells;
	Bool	full_redraw, dec_mouse;
	int	scale, detected_scale;
};

extern struct sixel_client sixelClient;
extern const struct sixel_render_hooks sixelIterm2Render;
extern const struct sixel_render_hooks sixelKonsoleRender;
extern const struct sixel_transport_hooks sixelTmuxTransport;
extern const struct sixel_transport_hooks sixelScreenTransport;

Bool	sixelClientEnabled(enum sixel_client_id);
Bool	sixelClientForced(enum sixel_client_id);
void	sixelClientSelect(const struct sixel_render_hooks *, Bool);
void	sixelClientDirectControl(const char *);
Bool	sixelClientDirectImage(SIXEL_Driver *, struct sixel_image *, FILE *);
void	sixelClientImageBody(SIXEL_Driver *, struct sixel_image *, FILE *);

void	sixelTmuxInit(void);
void	sixelTmuxCheckLimit(void);
void	sixelTmuxFinish(void);
void	sixelTmuxNormalize(SIXEL_Driver *);
void	sixelScreenInit(void);
void	sixelScreenStart(void);
void	sixelScreenStop(void);
void	sixelScreenNormalize(SIXEL_Driver *);
Bool	sixelScreenReplay(SIXEL_Driver *);
void	sixelIterm2Detect(void);
Bool	sixelIterm2Identity(const char *);
Bool	sixelIterm2Cell(SIXEL_Driver *, const char *, Bool);
const char *sixelIterm2ReplyPrefix(void);
void	sixelIterm2Query(void);
void	sixelIterm2Normalize(SIXEL_Driver *);
void	sixelIterm2Mouse(SIXEL_Driver *, int *, int *);
Bool	sixelIterm2Pixels(SIXEL_Driver *, struct sixel_image *);
void	sixelKonsoleDetect(void);
Bool	sixelKonsoleIdentity(const char *);
void	sixelWindowsQuery(void);
void	sixelWindowsNormalize(SIXEL_Driver *);
void	sixelWindowsMouseQuery(void);
void	sixelXtermNormalize(SIXEL_Driver *);
void	sixelXtermPointerStart(void);
void	sixelXtermPointerStop(void);
int	sixelXtermHostCursor(void);
void	sixelMltermInit(void);

#endif
