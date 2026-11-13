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

static size_t sixelTmuxImageLimit = 1048576;
static size_t sixelTmuxImageRequired;
static Bool sixelTmuxLimitQueried, sixelTmuxLimitOption;

static Bool
sixelTmuxOption(const char *pane, const char *option, char *value,
    size_t capacity, Bool required)
{
	int fd, fds[2], status;
	pid_t pid, waited;
	ssize_t n;
	size_t used;

	if (!pane || !*pane || pipe(fds) < 0) {
		if (required)
			FatalError("Xsixel: cannot check tmux option "
			    "(TMUX_PANE missing or pipe failed)\n");
		return (FALSE);
	}
	pid = fork();
	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(fds[1]);
		if (!required) {
			fd = open("/dev/null", O_WRONLY);
			if (fd < 0 || dup2(fd, STDERR_FILENO) < 0)
				_exit(127);
			close(fd);
		}
		execlp("tmux", "tmux", "show-options", "-A",
		    strcmp(option, "allow-passthrough") == 0 ? "-p" : "-s",
		    "-v", "-t", pane, option, (char *)NULL);
		_exit(127);
	}
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		if (required)
			FatalError("Xsixel: cannot query tmux\n");
		return (FALSE);
	}
	used = 0;
	while (used < capacity - 1) {
		n = read(fds[0], value + used, capacity - 1 - used);
		if (n > 0)
			used += n;
		else if (n == 0 || errno != EINTR)
			break;
	}
	close(fds[0]);
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited < 0 && errno == EINTR);
	value[used] = '\0';
	value[strcspn(value, "\r\n")] = '\0';
	if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		if (required)
			FatalError("Xsixel: cannot query tmux setting; "
			    "check tmux and TMUX_PANE\n");
		return (FALSE);
	}
	return (TRUE);
}

void
sixelTmuxCheckLimit(void)
{
	char value[32], *end;
	unsigned long long limit;

	if (!sixelClient.tmux || sixelClient.render != &sixelKonsoleRender ||
	    sixelTmuxLimitQueried)
		return;
	sixelTmuxLimitQueried = TRUE;
	if (!sixelTmuxOption(getenv("TMUX_PANE"), "input-buffer-size", value,
	    sizeof(value), FALSE))
		return;
	errno = 0;
	limit = strtoull(value, &end, 10);
	if (errno || end == value || *end || limit < 32 || limit > UINT_MAX)
		return;
	sixelTmuxLimitOption = TRUE;
	sixelTmuxImageLimit = 32;
	while (sixelTmuxImageLimit <= limit / 2)
		sixelTmuxImageLimit *= 2;
}

void
sixelTmuxInit(void)
{
	const char *tmux = getenv("TMUX");
	const char *pane = getenv("TMUX_PANE");
	char value[32];

	if (!sixelClientEnabled(SIXEL_CLIENT_TMUX) ||
	    (!sixelClientForced(SIXEL_CLIENT_TMUX) && (!tmux || !*tmux)))
		return;
	sixelTmuxOption(pane, "allow-passthrough", value, sizeof(value), TRUE);
	if (strcmp(value, "on") != 0 && strcmp(value, "all") != 0)
		FatalError("Xsixel: tmux allow-passthrough is off. Run in this pane:\n"
		    "  tmux set-option -p allow-passthrough on\n"
		    "Then restart Xsixel. To enable it persistently, add to ~/.tmux.conf:\n"
		    "  set -g allow-passthrough on\n");
	sixelTmuxOption(pane, "focus-events", value, sizeof(value), TRUE);
	if (strcmp(value, "on") != 0)
		FatalError("Xsixel: tmux focus-events is off. Run:\n"
		    "  tmux set-option -g focus-events on\n"
		    "Then restart Xsixel. To enable it persistently, add to ~/.tmux.conf:\n"
		    "  set -g focus-events on\n");
	sixelClient.tmux = TRUE;
	sixelClient.transport = &sixelTmuxTransport;
}

void
sixelTmuxNormalize(SIXEL_Driver *driver)
{
	double cell_w = driver->cell_pixel_w, cell_h = driver->cell_pixel_h;
	double w, h;

	if (!sixelClient.tmux || driver->cell_w <= 0 || driver->cell_h <= 0)
		return;
	/*
	 * A known Retina terminal must not shrink to a points-only 16t
	 * reply if its backing-cell query is unavailable. Retain ioctl in
	 * that case.
	 */
	if (sixelClient.iterm2 && !driver->iterm_cell_scale)
		return;
	if (driver->iterm_cell_scale) {
		cell_w = driver->iterm_cell_w * driver->iterm_cell_scale;
		cell_h = driver->iterm_cell_h * driver->iterm_cell_scale;
	}
	w = cell_w * driver->cell_w;
	h = cell_h * driver->cell_h;
	if (cell_w < 1 || cell_h < 1 || w > SHRT_MAX || h > SHRT_MAX)
		return;
	if (driver->pixel_size_reported &&
	    driver->reported_pixel_w / driver->cell_w == cell_w &&
	    driver->reported_pixel_h / driver->cell_h == cell_h) {
		driver->pixel_w = driver->reported_pixel_w;
		driver->pixel_h = driver->reported_pixel_h;
	} else {
		driver->pixel_w = (int)(w + 0.5);
		driver->pixel_h = (int)(h + 0.5);
	}
}

void
sixelTmuxFinish(void)
{
	if (sixelTmuxImageRequired) {
		size_t limit = 32;

		fprintf(stderr, "Xsixel: Konsole image needs %zu bytes; "
		    "tmux input capacity is %zu bytes.\n",
		    sixelTmuxImageRequired, sixelTmuxImageLimit);
		while (limit < sixelTmuxImageRequired && limit <= UINT_MAX / 2)
			limit *= 2;
		if (sixelTmuxLimitOption && limit >= sixelTmuxImageRequired)
			fprintf(stderr, "Run:\n"
			    "  tmux set-option -s input-buffer-size %zu\n"
			    "Then restart Xsixel. To persist, add to ~/.tmux.conf:\n"
			    "  set -s input-buffer-size %zu\n", limit, limit);
		else
			fprintf(stderr, "Use tmux with a configurable input-buffer-size, "
			    "or run Xsixel directly or through GNU screen.\n");
		fflush(stderr);
	}
}

static void
tmux_control(const char *text)
{
	fputs("\033Ptmux;", stdout);
	for (; *text != '\0'; text++) {
		if (*text == '\033')
			putchar('\033');
		putchar(*text);
	}
	fputs("\033\\", stdout);
}

static enum sixel_image_limit
tmux_limit(SIXEL_Driver *driver, struct sixel_image *image,
    BoxRec *first, BoxRec *second)
{
	size_t length, i;
	int offset;

	if (sixelTmuxImageRequired != 0)
		return (SIXEL_IMAGE_STOP);
	if (image->opaque) {
		length = strlen("\033Ptmux;\033\033[?80h\033\033[H\033\033P") +
		    snprintf(NULL, 0, "0;0q\"1;1;%d;%d", image->box.x2,
		    image->box.y2) + image->end - image->body - 2 +
		    strlen("\033\033\\\033\\");
		if (length > sixelTmuxImageLimit) {
			sixelTmuxImageRequired = length;
			GiveUp(0);
			return (SIXEL_IMAGE_STOP);
		}
		return (SIXEL_IMAGE_OK);
	}
	if (image->end <= 65536)
		return (SIXEL_IMAGE_OK);
	length = image->end - image->body + 128 + image->box.y1 / 6;
	offset = image->box.x1 != 0 ?
	    snprintf(NULL, 0, "!%d?", image->box.x1) : 0;
	length += offset;
	if (offset != 0) {
		for (i = image->body; i < image->end - 2; i++) {
			if (driver->encoded[i] == '$' || driver->encoded[i] == '-')
				length += offset;
		}
	}
	if (length <= 1048576)
		return (SIXEL_IMAGE_OK);
	*first = *second = image->box;
	if (image->height > 6)
		first->y2 = second->y1 = image->box.y1 +
		    max(6, image->height / 12 * 6);
	else if (image->width > 1)
		first->x2 = second->x1 = image->box.x1 + image->width / 2;
	else
		return (SIXEL_IMAGE_ERROR);
	return (SIXEL_IMAGE_SPLIT);
}

static Bool
tmux_image(SIXEL_Driver *driver, struct sixel_image *image)
{
	fputs(image->opaque ?
	    "\033Ptmux;\033\033[?80h\033\033[H\033\033P" :
	    "\033Ptmux;\033\033[H\033\033P", stdout);
	fprintf(stdout, "0;%dq\"1;1;%d;%d", image->opaque ? 0 : 1,
	    image->box.x2, image->box.y2);
	sixelClientImageBody(driver, image, stdout);
	fputs("\033\033\\\033\\", stdout);
	return (TRUE);
}

const struct sixel_transport_hooks sixelTmuxTransport = {
	.control = tmux_control,
	.write_image = tmux_image,
	.limit = tmux_limit,
	.cells = TRUE,
	.coalesce_size = TRUE
};
