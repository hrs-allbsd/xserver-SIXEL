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
#ifndef SIXEL_CLIENT_H
#define SIXEL_CLIENT_H

typedef struct sixel_driver SIXEL_Driver;

struct sixel_image {
	BoxRec	box;
	int	width, height, row, col;
	Bool	positioned, opaque, home_mode, full, converted;
	size_t	body, end;
};

struct sixel_client_callbacks {
	void	(*redraw)(void);
	void	(*publish_mode)(void);
	void	(*enable_mouse)(void);
};

enum sixel_image_limit {
	SIXEL_IMAGE_OK,
	SIXEL_IMAGE_SPLIT,
	SIXEL_IMAGE_STOP,
	SIXEL_IMAGE_ERROR
};

void	sixelClientInit(const struct sixel_client_callbacks *);
void	sixelClientAttach(SIXEL_Driver *);
void	sixelClientDamage(SIXEL_Driver *);
void	sixelClientRelease(SIXEL_Driver *);
void	sixelClientStart(void);
void	sixelClientStop(void);
void	sixelClientFinish(void);
void	sixelClientUseMsg(void);
int	sixelClientArgument(int, char **, int);
const char *sixelClientMode(void);
Bool	sixelClientModeAllowed(const char *, size_t);
Bool	sixelClientSetMode(const char *, size_t);
void	sixelClientControl(const char *);
Bool	sixelClientWrapped(void);
Bool	sixelClientCoalesceSize(void);
void	sixelClientReadSize(SIXEL_Driver *);
void	sixelClientResetSize(SIXEL_Driver *);
void	sixelClientNormalizeSize(SIXEL_Driver *);
Bool	sixelClientSizeReply(SIXEL_Driver *, int, int, int);
Bool	sixelClientStringReply(SIXEL_Driver *, Bool, const char *, Bool);
const char *sixelClientReplyPrefix(Bool);
int	sixelClientOutputScale(void);
void	sixelClientMouseStart(Bool);
void	sixelClientMouseReply(int, int);
void	sixelClientMousePosition(SIXEL_Driver *, int *, int *);
void	sixelClientPointerStart(void);
void	sixelClientPointerStop(void);
int	sixelClientHostCursor(void);
void	sixelClientInitControls(void);
void	sixelClientFiniControls(void);

void
sixelClientPlan(SIXEL_Driver *, const BoxRec *, Bool,
    struct sixel_image *);
BoxRec	sixelClientBounds(SIXEL_Driver *, const BoxRec *);
Bool	sixelClientFullDamage(SIXEL_Driver *);
Bool	sixelClientConvertPixels(SIXEL_Driver *, struct sixel_image *);
Bool	sixelClientNoDiffusion(void);
Bool	sixelClientReplay(SIXEL_Driver *);
enum sixel_image_limit
sixelClientImageLimit(SIXEL_Driver *,
    struct sixel_image *, BoxRec *, BoxRec *);
Bool	sixelClientWriteImage(SIXEL_Driver *, struct sixel_image *);

#endif
