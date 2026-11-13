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

struct sixel_client sixelClient = {.detected_scale = 1};

static const char *const client_names[] = {
	"tmux", "screen", "xterm", "mlterm", "iterm2", "windows-terminal",
	"konsole"
};

static void
fast_plan(SIXEL_Driver *driver, const BoxRec *damage, Bool part,
    struct sixel_image *image)
{
	if (driver->output_scale > 1) {
		sixelIterm2Render.plan(driver, damage, part, image);
		return;
	}
	image->box = *damage;
	image->box.y1 = image->box.y1 / 6 * 6;
	image->box.y2 = min(driver->h, (image->box.y2 + 5) / 6 * 6);
	image->home_mode = sixelClient.iterm2 && !sixelClient.tmux;
}

static BoxRec
fast_bounds(SIXEL_Driver *driver, const BoxRec *damage)
{
	struct sixel_image image = {0};

	fast_plan(driver, damage, TRUE, &image);
	return (image.box);
}

static Bool
fast_full_damage(SIXEL_Driver *driver)
{
	return (driver->output_scale > 1 &&
	    sixelIterm2Render.full_damage(driver));
}

static const struct sixel_render_hooks fast_render = {
	.name = "fast",
	.owner = SIXEL_CLIENT_COUNT,
	.plan = fast_plan,
	.bounds = fast_bounds,
	.full_damage = fast_full_damage
};

void
sixelClientDirectControl(const char *text)
{
	fputs(text, stdout);
}

static Bool
direct_image(SIXEL_Driver *driver, struct sixel_image *image)
{
	return (sixelClientDirectImage(driver, image, stdout));
}

static const struct sixel_transport_hooks direct_transport = {
	.control = sixelClientDirectControl,
	.write_image = direct_image
};

Bool
sixelClientEnabled(enum sixel_client_id id)
{
	return (sixelClient.policy[id] != SIXEL_COMPAT_OFF);
}

Bool
sixelClientForced(enum sixel_client_id id)
{
	return (sixelClient.policy[id] == SIXEL_COMPAT_ON);
}

void
sixelClientSelect(const struct sixel_render_hooks *render, Bool explicit)
{
	SIXEL_Driver *driver;
	Bool changed;

	changed = sixelClient.render != render;
	sixelClient.render = render;
	sixelClient.explicit_mode |= explicit;
	sixelTmuxCheckLimit();
	driver = sixelClient.driver;
	if (changed && driver != NULL) {
		if (driver->dither != NULL)
			sixel_dither_set_diffusion_type(driver->dither,
			    sixelClientNoDiffusion() ? SIXEL_DIFFUSE_NONE :
			    SIXEL_DIFFUSE_FS);
		driver->screen_frame_valid = FALSE;
		sixelClient.callbacks.redraw();
	}
}

void
sixelClientInit(const struct sixel_client_callbacks *callbacks)
{
	sixelClient.callbacks = *callbacks;
	sixelClient.transport = &direct_transport;
	if (sixelClient.render == NULL)
		sixelClient.render = &fast_render;
	/* Multiplexer selection precedes queries; tmux takes precedence. */
	sixelTmuxInit();
	sixelScreenInit();
	sixelIterm2Detect();
	sixelKonsoleDetect();
	sixelMltermInit();
	sixelTmuxCheckLimit();
}

void
sixelClientAttach(SIXEL_Driver *driver)
{
	sixelClient.driver = driver;
}

void
sixelClientDamage(SIXEL_Driver *driver)
{
	driver->screen_frame_valid = FALSE;
}

void
sixelClientRelease(SIXEL_Driver *driver)
{
	free(driver->screen_frame);
	free(driver->retina_palette_cache);
	driver->screen_frame = NULL;
	driver->retina_palette_cache = NULL;
	driver->screen_frame_valid = FALSE;
}

void
sixelClientStart(void)
{
	sixelScreenStart();
}

void
sixelClientStop(void)
{
	sixelScreenStop();
}

void
sixelClientFinish(void)
{
	sixelTmuxFinish();
	sixelClient.driver = NULL;
}

void
sixelClientUseMsg(void)
{
	ErrorF("-compat name=auto|on|off  Control one client compatibility module\n");
	ErrorF("  names: tmux, screen, xterm, mlterm, iterm2, windows-terminal, konsole\n");
	ErrorF("-iterm2                  Force iTerm2 rendering and geometry\n");
	ErrorF("-iterm2-scale {auto|1|2}  Select iTerm2 image enlargement\n");
	ErrorF("-iterm2-full-redraw      Use full images for shared viewers\n");
	ErrorF("-konsole                 Force Konsole rendering\n");
}

static void
set_policy(enum sixel_client_id id, enum sixel_compat_policy policy)
{
	sixelClient.policy[id] = policy;
	if (id != SIXEL_CLIENT_ITERM2 && id != SIXEL_CLIENT_KONSOLE)
		return;
	if (policy == SIXEL_COMPAT_ON) {
		if (id == SIXEL_CLIENT_ITERM2)
			sixelClient.iterm2 = TRUE;
		sixelClientSelect(id == SIXEL_CLIENT_ITERM2 ?
		    &sixelIterm2Render : &sixelKonsoleRender, TRUE);
	} else {
		if (id == SIXEL_CLIENT_ITERM2)
			sixelClient.iterm2 = FALSE;
		if (sixelClient.render != NULL && sixelClient.render->owner == id) {
			sixelClient.render = &fast_render;
			sixelClient.explicit_mode = FALSE;
		}
	}
}

int
sixelClientArgument(int argc, char **argv, int i)
{
	enum sixel_compat_policy policy;
	const char *value, *equal;
	size_t length;
	int id;

	if (!strcmp(argv[i], "-compat")) {
		if (i + 1 >= argc)
			FatalError("Xsixel: -compat requires name=auto|on|off\n");
		value = argv[i + 1];
		equal = strchr(value, '=');
		if (equal == NULL)
			FatalError("Xsixel: invalid -compat value: %s\n", value);
		length = equal - value;
		for (id = 0; id < SIXEL_CLIENT_COUNT; id++) {
			if (strlen(client_names[id]) == length &&
			    !memcmp(value, client_names[id], length))
				break;
		}
		if (id == SIXEL_CLIENT_COUNT)
			FatalError("Xsixel: unknown compatibility module: %s\n", value);
		if (!strcmp(equal + 1, "auto"))
			policy = SIXEL_COMPAT_AUTO;
		else if (!strcmp(equal + 1, "on"))
			policy = SIXEL_COMPAT_ON;
		else if (!strcmp(equal + 1, "off"))
			policy = SIXEL_COMPAT_OFF;
		else
			FatalError("Xsixel: invalid compatibility policy: %s\n", value);
		set_policy(id, policy);
		return (2);
	}
	if (!strcmp(argv[i], "-konsole")) {
		set_policy(SIXEL_CLIENT_KONSOLE, SIXEL_COMPAT_ON);
		return (1);
	}
	if (!strcmp(argv[i], "-iterm2") ||
	    !strcmp(argv[i], "-iterm2-full-redraw")) {
		if (!strcmp(argv[i], "-iterm2-full-redraw"))
			sixelClient.full_redraw = TRUE;
		set_policy(SIXEL_CLIENT_ITERM2, SIXEL_COMPAT_ON);
		return (1);
	}
	if (!strcmp(argv[i], "-iterm2-scale")) {
		if (i + 1 >= argc || (strcmp(argv[i + 1], "auto") &&
		    strcmp(argv[i + 1], "1") && strcmp(argv[i + 1], "2")))
			FatalError("Xsixel: -iterm2-scale requires auto, 1 or 2\n");
		sixelClient.scale = !strcmp(argv[i + 1], "auto") ? 0 :
		    argv[i + 1][0] - '0';
		set_policy(SIXEL_CLIENT_ITERM2, SIXEL_COMPAT_ON);
		return (2);
	}
	return (0);
}

const char *
sixelClientMode(void)
{
	return (sixelClient.render->name);
}

Bool
sixelClientModeAllowed(const char *name, size_t length)
{
	const struct sixel_render_hooks *modes[] = {
		&fast_render, &sixelIterm2Render, &sixelKonsoleRender
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strlen(modes[i]->name) == length &&
		    !memcmp(modes[i]->name, name, length))
			return (modes[i]->owner == SIXEL_CLIENT_COUNT ||
			    sixelClientEnabled(modes[i]->owner));
	}
	return (FALSE);
}

Bool
sixelClientSetMode(const char *name, size_t length)
{
	const struct sixel_render_hooks *modes[] = {
		&fast_render, &sixelIterm2Render, &sixelKonsoleRender
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strlen(modes[i]->name) != length ||
		    memcmp(modes[i]->name, name, length))
			continue;
		if (modes[i]->owner != SIXEL_CLIENT_COUNT &&
		    !sixelClientEnabled(modes[i]->owner))
			return (FALSE);
		sixelClientSelect(modes[i], TRUE);
		return (TRUE);
	}
	return (FALSE);
}

void
sixelClientControl(const char *text)
{
	sixelClient.transport->control(text);
}

Bool
sixelClientWrapped(void)
{
	return (sixelClient.transport != &direct_transport);
}

Bool
sixelClientCoalesceSize(void)
{
	return (sixelClient.transport->coalesce_size);
}

int
sixelClientOutputScale(void)
{
	return (sixelClient.iterm2 && !sixelClient.tmux ?
	    (sixelClient.scale ? sixelClient.scale : sixelClient.detected_scale) : 1);
}

void
sixelClientReadSize(SIXEL_Driver *driver)
{
	sixelScreenNormalize(driver);
	if (driver->cell_w == 0 || driver->cell_h == 0)
		fputs("\033[18t", stdout);
	if (sixelClient.tmux || sixelClient.iterm2)
		sixelClientControl("\033[16t");
	else
		sixelWindowsQuery();
	sixelIterm2Query();
}

void
sixelClientResetSize(SIXEL_Driver *driver)
{
	driver->iterm_cell_scale = 0;
	sixelClient.cell_pending = FALSE;
}

void
sixelClientNormalizeSize(SIXEL_Driver *driver)
{
	sixelScreenNormalize(driver);
	sixelIterm2Normalize(driver);
	sixelWindowsNormalize(driver);
	sixelTmuxNormalize(driver);
}

Bool
sixelClientSizeReply(SIXEL_Driver *driver, int kind, int width, int height)
{
	switch (kind) {
	case 4:
		if (sixelClient.tmux ||
		    (sixelClient.iterm2 && driver->pixel_size_reported))
			return (FALSE);
		driver->pixel_size_reported = TRUE;
		driver->pixel_size_reply = TRUE;
		driver->pixel_w = driver->reported_pixel_w = width;
		driver->pixel_h = driver->reported_pixel_h = height;
		break;
	case 6:
		driver->cell_pixel_w = width;
		driver->cell_pixel_h = height;
		break;
	case 8:
		driver->cell_w = width;
		driver->cell_h = height;
		break;
	default:
		return (FALSE);
	}
	return (TRUE);
}

Bool
sixelClientStringReply(SIXEL_Driver *driver, Bool identity, const char *text,
    Bool palette_renderer)
{
	Bool changed;

	if (!identity)
		return (sixelIterm2Cell(driver, text, palette_renderer));
	if (!sixelClient.identity_pending)
		return (FALSE);
	sixelClient.identity_pending = FALSE;
	changed = sixelIterm2Identity(text);
	changed |= sixelKonsoleIdentity(text);
	if (changed) {
		if (driver != NULL)
			driver->screen_frame_valid = FALSE;
		sixelClient.callbacks.publish_mode();
	}
	return (FALSE);
}

const char *
sixelClientReplyPrefix(Bool identity)
{
	return (identity ? ">|" : sixelIterm2ReplyPrefix());
}

void
sixelClientMouseStart(Bool dec)
{
	sixelClient.dec_mouse = dec;
	if (dec)
		fputs("\033[1;1'z\033[1;3'{\033['w", stdout);
	else if (sixelClient.transport->cells || sixelClient.mouse_cells)
		fputs("\033[?1006h\033[?1003h", stdout);
	else
		fputs("\033[?1006h\033[?1016h\033[?1003h", stdout);
}

void
sixelClientMouseReply(int mode, int state)
{
	if (!sixelClient.mouse_pending || mode != 1016 || state < 0 || state > 4)
		return;
	sixelClient.mouse_pending = FALSE;
	if (state == 0 || state == 4) {
		sixelClient.mouse_cells = TRUE;
		fputs("\033[?1016l", stdout);
		sixelClient.callbacks.enable_mouse();
	}
}

void
sixelClientMousePosition(SIXEL_Driver *driver, int *x, int *y)
{
	int width, height;

	if (driver == NULL)
		return;
	if ((sixelClient.transport->cells || sixelClient.mouse_cells) &&
	    driver->cell_w > 0 && driver->cell_h > 0) {
		width = driver->pixel_w > 0 ?
		    driver->pixel_w / driver->output_scale : driver->w;
		height = driver->pixel_h > 0 ?
		    driver->pixel_h / driver->output_scale : driver->h;
		*x = ((int64_t)*x * width + width / 2) / driver->cell_w;
		*y = ((int64_t)*y * height + height / 2) / driver->cell_h;
	} else
		sixelIterm2Mouse(driver, x, y);
}

void
sixelClientPointerStart(void)
{
	sixelXtermPointerStart();
}

void
sixelClientPointerStop(void)
{
	sixelXtermPointerStop();
}

int
sixelClientHostCursor(void)
{
	return (sixelXtermHostCursor());
}

void
sixelClientInitControls(void)
{
	sixelClient.identity_pending = TRUE;
	sixelClientControl("\033[>q");
	sixelIterm2Query();
	sixelWindowsMouseQuery();
}

void
sixelClientFiniControls(void)
{
	if (sixelClientWrapped())
		sixelClientControl("\033[H\033[2J");
	sixelClientControl("\033[?80r");
}

void
sixelClientPlan(SIXEL_Driver *driver, const BoxRec *damage, Bool part,
    struct sixel_image *image)
{
	sixelClient.render->plan(driver, damage, part, image);
	image->box.x1 = max(0, image->box.x1);
	image->box.y1 = max(0, image->box.y1);
	image->box.x2 = min(driver->w, image->box.x2);
	image->box.y2 = min(driver->h, image->box.y2);
}

BoxRec
sixelClientBounds(SIXEL_Driver *driver, const BoxRec *damage)
{
	return (sixelClient.render->bounds(driver, damage));
}

Bool
sixelClientFullDamage(SIXEL_Driver *driver)
{
	return (sixelClient.render->full_damage(driver));
}

Bool
sixelClientConvertPixels(SIXEL_Driver *driver, struct sixel_image *image)
{
	return (sixelIterm2Pixels(driver, image));
}

Bool
sixelClientNoDiffusion(void)
{
	return ((sixelClient.iterm2 && !sixelClient.tmux) ||
	    sixelClient.render == &sixelKonsoleRender);
}

Bool
sixelClientReplay(SIXEL_Driver *driver)
{
	return (sixelScreenReplay(driver));
}

enum sixel_image_limit
sixelClientImageLimit(SIXEL_Driver *driver, struct sixel_image *image,
    BoxRec *first, BoxRec *second)
{
	if (sixelClient.transport->limit == NULL)
		return (SIXEL_IMAGE_OK);
	return (sixelClient.transport->limit(driver, image, first, second));
}

Bool
sixelClientWriteImage(SIXEL_Driver *driver, struct sixel_image *image)
{
	return (sixelClient.transport->write_image(driver, image));
}

static void
horizontal_offset(FILE *out, int x)
{
	if (x != 0)
		fprintf(out, "!%d?", x);
}

void
sixelClientImageBody(SIXEL_Driver *driver, struct sixel_image *image, FILE *out)
{
	size_t chunk, i;
	int y;

	for (y = 0; !image->positioned && y < image->box.y1; y += 6)
		putc('-', out);
	if (!image->positioned)
		horizontal_offset(out, image->box.x1);
	chunk = image->body;
	for (i = chunk; i < image->end - 2; i++) {
		if (driver->encoded[i] != '$' && driver->encoded[i] != '-')
			continue;
		fwrite(driver->encoded + chunk, 1, i + 1 - chunk, out);
		if (!image->positioned)
			horizontal_offset(out, image->box.x1);
		chunk = i + 1;
	}
	fwrite(driver->encoded + chunk, 1, image->end - 2 - chunk, out);
}

Bool
sixelClientDirectImage(SIXEL_Driver *driver, struct sixel_image *image, FILE *out)
{
	if (image->positioned)
		fprintf(out, "\033[?80l\033[%d;%dH", image->row, image->col);
	else if (image->home_mode)
		fputs("\033[?80h\033[H", out);
	fprintf(out, "\033P0;%dq\"1;1;%d;%d", image->opaque ? 0 : 1,
	    image->positioned ? image->width : image->box.x2,
	    image->positioned ? image->height : image->box.y2);
	sixelClientImageBody(driver, image, out);
	fputs("\033\\", out);
	if (image->positioned)
		fputs("\033[H", out);
	return (TRUE);
}
