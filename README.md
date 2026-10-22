# XLibre Xserver

[![GetMerged Scorecard](https://getmerged.abhishekco.de/api/badge/X11Libre/xserver)](https://getmerged.abhishekco.de/X11Libre/xserver)


The XLibre Xserver is the community-managed display server for the [X Window System Protocol Version 11 (Wikipedia)](https://en.wikipedia.org/wiki/X_Window_System_core_protocol), in short, X11.

<p>
    <figure><a href="https://github.com/orgs/X11Libre/discussions/504#discussioncomment-17195498"><img src="https://github.com/X11Libre/website/blob/eb05723185647561390dade472edea5938333113/build/img/xlibre-one-year.jpg" alt="1 year of XLibre screenshot"></a><figcaption>One Year of XLibre. See more <a href="https://github.com/orgs/X11Libre/discussions/211">liberated screens here</a>.</figcaption>
    </figure>
</p>

## Selected Features

* All the good things from [X.Org Server](https://en.wikipedia.org/wiki/X.Org_Server), including its unreleased features
* [TearFree modesetting](https://github.com/X11Libre/xserver/commit/0dacee6c5149b63a563e9bed63502da2e9f1ac1f) by default and optionally [atomic modesetting](https://github.com/X11Libre/xserver/commit/461411c798c263f70daa96f7136614dfefda6adc)
* Support for the Nvidia drivers 340, 390, 470, 570, and newer
* [Xnamespace extension](https://github.com/X11Libre/xserver/blob/master/doc/Xnamespace.md) for separating X clients
* Support for seat management via [seatd](https://sr.ht/~kennylevinsen/seatd) besides [systemd-logind](https://www.freedesktop.org/software/systemd/man/latest/systemd-logind.service.html)
* [Xfbdev](https://github.com/X11Libre/xserver/tree/master/hw/kdrive), the generic framebuffer Xserver for Linux
* CI builds for several BSDs, Linux, MacOS, and Microsoft Windows
* Active community, cleanups, fixes, and development based on merit

To learn more about the features and mission of XLibre, please [visit our homepage](https://xlibre.net).


## Switching to XLibre

The easiest way to install and run XLibre is to use your distribution's provided packages. Please see the [Are We XLibre Yet? - (X11Libre/xserver Wiki)](https://github.com/X11Libre/xserver/wiki/Are-We-XLibre-Yet%3F) page for a list of the available options. If there is no option, then go on with building and installing XLibre from source.


### Building XLibre

After cloning the [Xserver repository](https://github.com/X11Libre/xserver.git) or unpacking the sources and installing the dependencies, change into the source directory and run the [Meson](https://mesonbuild.com) build tool:

```shell
cd "<source dir of xserver>"
meson setup <prefix> build <meson_options>
ninja -C build install
```

You may specify the install `<prefix>` with, for example, `--prefix="$(pwd)/image"` and add build time [`<meson_options>`](https://github.com/X11Libre/xserver/blob/master/meson_options.txt) like so: `-Dxnest=false`. You may also want to build and install some graphics and input drivers. Please refer to the [Building XLibre (X11Libre/xserver Wiki)](https://github.com/X11Libre/xserver/wiki/Building-XLibre) page for more details.


### Configuring XLibre

Until XLibre releases its own, you can find a detailed description of the configuration on the [Configuration - Xorg (ArchWiki)](https://wiki.archlinux.org/title/Xorg#Configuration) page. If you have built and installed XLibre yourself, then change into the `<prefix>` directory with `cd <prefix>` and create a directory `etc/X11` with a file `xorg.conf` and adjust it accordingly.

Starting with version 25.0.0.16, the proprietary Nvidia driver is autodetected and handled internally without any special configuration. Please see the [Compatibility of XLibre (X11Libre/xserver Wiki)](https://github.com/X11Libre/xserver/wiki/Compatibility-of-XLibre) page for [more details on the Nvidia driver](https://github.com/X11Libre/xserver/wiki/Compatibility-of-XLibre#nvidia-proprietary-driver) and compatibility in general.


### Running XLibre

If you installed XLibre using your distribution's provided packages, then the Xserver is usually started by [init (Wikipedia)](https://en.wikipedia.org/wiki/Init) on system start. On other systems it should be possible to manually start XLibre with user permissions by invoking `startx`. Please refer to [`man startx`](https://linux.die.net/man/1/startx) for how to use it.

If you have built and installed XLibre yourself, then you may want to shutdown other Xservers, change into the `<prefix>` directory, and create a simple `testx.sh` file with the following contents:

```shell
#!/bin/sh
./bin/X :1 vt8 -logfile /dev/stdout &
_pid=$!
sleep 10 && kill $_pid
```

You can adjust the `:1 vt8` and other options in the `testx.sh` file as detailed in [`man Xorg`](https://linux.die.net/man/1/xorg). Make the `testx.sh` executable and run it:

```shell
chmod 0770 testx.sh
./testx.sh
```

This should give you 10 glorious seconds of a black and beautiful and empty screen. Afterwards the Xserver complains about being killed, but there should be no other critical errors for a "test passed." For more details, please see [Building XLibre (X11Libre/xserver Wiki)](https://github.com/X11Libre/xserver/wiki/Building-XLibre).

If you are using seatd for rootless operation instead of (e)logind, create the following file named `.xserverrc` in your home directory:
```shell
#!/bin/sh
exec X -keeptty "$@"
```
Then, run `chmod +x ~/.xserverrc`.


## I want to help!

That's great; there's enough to do for everyone. You may consider [one](https://github.com/orgs/X11Libre/discussions/categories/1-new-ideas) of the [many](https://github.com/orgs/X11Libre/discussions/categories/2-rfcs-of-the-core-team) [ideas](https://github.com/orgs/X11Libre/discussions/categories/3-ideas-soon-to-be-addressed) and [feature requests](https://github.com/X11Libre/xserver/issues?q=is%3Aissue%20state%3Aopen%20label%3Aenhancement) out there. To help in testing, you may consider becoming an [XLibre Test Driver](https://github.com/X11Libre/xserver/wiki/XLibre-Test-Drivers). Please also have a look at the [good first](https://github.com/X11Libre/xserver/issues?q=is%3Aissue%20state%3Aopen%20label%3A%22good%20first%20issue%22) and [help wanted](https://github.com/X11Libre/xserver/issues?q=is%3Aissue%20state%3Aopen%20label%3A%22help%20wanted%22) issues and the [Liberated Screens](https://github.com/orgs/X11Libre/discussions/211).

If you want to work on anything, just let us know. If you have any questions, [just ask](https://github.com/orgs/X11Libre/discussions/categories/q-a). Thank you!


## Contact

[XLibre Discussions at GitHub](https://github.com/orgs/X11Libre/discussions) | [XLibre mailing list at FreeLists](https://www.freelists.org/list/xlibre) | [@x11dev channel at Telegram](https://t.me/x11dev) | [#xlibredev space at Matrix](https://matrix.to/#/#xlibredev:matrix.org) | [XLibre security contact at GitHub](https://github.com/X11Libre/xserver/security/policy)

## Xsixel terminal server

The `sixel` branch adds the Xsixel terminal backend to XLibre. Build it with
Meson and Ninja, with libsixel and the usual XLibre development dependencies
available through pkg-config:

```sh
meson setup build-xsixel -Dxsixel=true -Dxorg=false -Dxnest=false -Dxvfb=false \
    -Dxquartz=false -Dxwin=false -Dglamor=false -Dglx=false \
    -Ddri1=false -Ddri2=false -Ddri3=false -Dinput_thread=false \
    -Ddocs=false -Ddevel-docs=false
meson compile -C build-xsixel
./build-xsixel/hw/kdrive/sixel/Xsixel :1 -exec twm
```

Run Xsixel in a SIXEL-capable terminal. The optional Chafa renderer requires
Chafa development files and `-Dchafa=true`; select it at runtime with
`-backend chafa`. The default renderer is libsixel.

Inside tmux, Xsixel detects `TMUX` and automatically sends SIXEL images through
tmux passthrough, regardless of whether tmux was built with SIXEL support.
Enable passthrough and focus notifications before starting Xsixel:

```sh
tmux set-option -p allow-passthrough on
tmux set-option -g focus-events on
```

Alternatively, add these settings to `~/.tmux.conf`:

```text
set -g allow-passthrough on
set -g focus-events on
```

Xsixel checks both settings at startup and exits with setup instructions if
either is off. Passthrough accepts `on` and `all`. Focus notifications trigger a
full redraw when returning to the Xsixel window. The outer terminal must
support SIXEL. No tmux configuration is changed automatically.

Passthrough draws directly on the outer terminal, so use a single pane or a
zoomed pane (`Ctrl-b z`), preferably with the status line disabled
(`tmux set-option status off`). Split-pane clipping and nested tmux are not
handled. Use `xrefresh -display :9` if an additional redraw is needed. tmux
forwards SGR mouse positions in character cells rather than pixels. Xsixel
converts them to pixel positions using the current pane geometry, including after
a resize. Pointer movement is consequently quantized to cell centres in tmux;
outside tmux, SGR pixel precision is retained. Outside tmux, the existing direct output
and damage-based encoding are retained without extra buffering or subprocesses.

Inside GNU screen, Xsixel detects `STY` and uses screen's DCS passthrough
automatically. No SIXEL build option or passthrough setting is required in
screen. SIXEL frames are split into packets below screen's string-size limit,
including safe handling of the embedded image terminator. For example:

```sh
screen Xsixel :9 -exec twm
```

Use one full-screen region without a hardstatus or caption line. Split-region
clipping and nested multiplexers are not supported. Window
switching and reattachment restore the image automatically within about two
seconds. In screen only, Xsixel periodically resends a full frame; screen
forwards it to displays showing that window. Frames are also encoded while the
window is hidden, since screen provides no focus notifications. This adds a
periodic encoding cost in screen only. No terminal status queries are used:
their responses can interfere with screen's command-prefix input. Use
`xrefresh -display :9` if an immediate redraw is needed.

For screen, Xsixel fits reported pixel dimensions to whole character cells,
excluding the usual xterm internal-border remainder. This correction applies
at startup and on resize without adding terminal queries; an explicit `-screen`
size remains authoritative. tmux and direct-terminal sizing are unchanged.

Screen forwards SGR cell coordinates, which Xsixel converts using the current
terminal dimensions; pointer precision is limited to character cells. Use the
default SGR mouse protocol in screen. The outer terminal still needs SIXEL and
mouse-reporting support. The normal and tmux output paths retain their existing
encoding and do not allocate screen's frame buffer or split output packets.

Xsixel automatically enables libsixel compatibility mode at startup when
`TERM_PROGRAM=iTerm.app` or `LC_TERMINAL=iTerm2` is present. The latter may be
forwarded over SSH, depending on the client and server configuration. Detection
runs only at startup, without terminal queries or subprocesses.

If that information is unavailable, force the mode explicitly:

```sh
screen Xsixel :9 -iterm2 -exec ratpoison
```

iTerm2 3.7.6 decodes transparent SIXEL gaps as opaque pixels, and rounds image
coverage to character cells. Partial images can therefore leave black padding
at their right and bottom edges, even when their interior pixels are correct.
In compatibility mode, Xsixel encodes the full framebuffer for every update,
keeping image boundaries fixed at the screen edges. This also works when
`screen -x` viewers have different cell sizes. It increases encoding and output
cost, especially during pointer movement; normal terminal and tmux rendering
retain their cropped fast path when compatibility mode is off. Chafa already
uses full-frame output; this option affects libsixel only.

On Retina displays, enlarge the X desktop's pixels before SIXEL encoding to
keep bitmap fonts, window decorations, and cursors at a comfortable size:

```sh
Xsixel :9 -iterm2-scale 2 -exec ratpoison
# Or inside GNU screen:
screen Xsixel :9 -iterm2-scale 2 -exec ratpoison
```

This option also enables iTerm2 compatibility. For a terminal reporting
1120x850 image pixels, the X desktop is 560x425 and the output image is 1120x850.
Each source pixel becomes an exact 2x2 block (nearest-neighbor enlargement).
The RGB buffer is reused across frames; only resizing allocates new buffers.
Mouse coordinates and terminal resizes use the logical desktop dimensions.
Explicit `-screen` dimensions specify the logical X desktop, and its output
image is enlarged by the selected factor.

The default is `-iterm2-scale 1`, preserving existing behavior. Enlargement is
currently an explicit startup choice, separate from the `fast`/`iterm2` drawing
mode; automatic Retina-scale detection is not implemented. At scale 2, both
drawing modes send a full enlarged image to avoid iTerm2's partial-image edges.
The option requires libsixel. tmux always uses scale 1, preserving its existing
geometry and output path. Normal terminals without this option retain their
existing framebuffer, RGB conversion, and cropped rendering path. In a shared
screen session the selected scale applies to every viewer, including non-Retina
viewers.

When using `screen -x` from multiple terminals, start Xsixel with `-iterm2` if
an iTerm2 viewer will attach later and the startup environment does not identify
it. Later attachments cannot be detected from the startup environment. The mode
applies to all viewers of that Xsixel server. `TERM=xterm-256color` or
`TERM_PROGRAM=tmux` alone does not enable compatibility mode.

Change the libsixel drawing mode at runtime with the root-window property
`_XSIXEL_RENDER_MODE` (type `STRING`, format 8):

```sh
# Enable iTerm2 compatibility after attaching another terminal.
xprop -display :9 -root -f _XSIXEL_RENDER_MODE 8s -set _XSIXEL_RENDER_MODE iterm2

# Restore normal cropped rendering.
xprop -display :9 -root -f _XSIXEL_RENDER_MODE 8s -set _XSIXEL_RENDER_MODE fast

# Read the current mode.
xprop -display :9 -root _XSIXEL_RENDER_MODE
```

The initial value reflects `-iterm2` and startup environment detection. A change
immediately schedules a full redraw; subsequent damage uses the selected mode.
Writing the same value does not force a redraw. Only `fast` and `iterm2` are
accepted; invalid values, other types, deletion, and property rotation are
rejected so the readable property always matches the active mode. Settings on
ordinary client windows do not affect the server. Runtime changes apply to all
viewers and last until Xsixel restarts. Change notifications drive this control;
there is no additional polling or property lookup in the frame rendering path.

Mouse input defaults to SGR pixel reporting (terminal modes 1003, 1006, and
1016), so xterm does not need DEC locator support. Motion, dragging, the first
three buttons, and vertical/horizontal wheel events are supported. For terminals
using DEC locator reports, start Xsixel with `-mouse-protocol dec`. Mouse reporting
modes are disabled when Xsixel exits.

If xterm suppresses mouse reports, clear its `disallowedMouseOps` list when
starting the outer terminal:

```sh
xterm -xrm 'XTerm*decTerminalID: vt340' \
    -xrm 'XTerm*numColorRegisters: 256' \
    -xrm 'XTerm*maxGraphicSize: 4096x4096' \
    -xrm 'XTerm*disallowedMouseOps:'
```

Inside that terminal, run `Xsixel :9 -exec twm`, then connect X clients to `:9`.



The Debian and macOS helper scripts use the same Meson build. Set `BUILD_DIR`
to change their build directory, or `CHAFA=true` to enable Chafa. They expect
build dependencies to be installed beforehand and do not install packages.

Without `-screen`, Xsixel initially uses the terminal's ioctl pixel dimensions,
or 640x480 when those dimensions are unavailable. It also asks the outer terminal
for its text-area pixel size and character-cell size at startup and on SIGWINCH:
a nonzero ioctl value may be stale (Windows `ssh.exe` can report 640x480 while
Windows Terminal reports a 1200x600 SIXEL area). A valid text-area reply overrides
the ioctl fallback; a cell-size reply can supply the dimensions when no text-area
reply is available. screen uses passthrough for these queries. tmux keeps valid
pane pixel dimensions and does not add queries in that case. For iTerm2 (identified
by the startup environment or `-iterm2`), Xsixel keeps valid ioctl dimensions
for SIXEL images and queries the character-cell size separately for mouse input.
On Retina displays, images use backing pixels while mouse reports use points;
shrinking the framebuffer to the point dimensions leaves part of the terminal
unpainted. Window-size replies may include window chrome and do not override
valid iTerm2 ioctl dimensions. screen mouse reports already use cell coordinates
and are converted using the framebuffer dimensions. Rendering-mode changes do
not change this policy. Terminal replies
resize the screen through RandR without blocking startup. Explicit dimensions
such as `-screen 1024x768` remain fixed.

Windows Terminal reports virtual SIXEL pixels, not necessarily physical display
pixels; its replies determine the correct drawable area. These queries are not
periodic, and the frame rendering path is unchanged.
Terminal window changes are detected through SIGWINCH. Updates are applied after
100 ms without another resize notification, so dragging the terminal window does
not repeatedly reallocate the framebuffer. If no usable dimensions are available,
Xsixel keeps the current screen size until a valid reply arrives. Explicit
`-screen` dimensions remain fixed during window changes.

The PTY integration tests cover sizing, delayed and fragmented replies, RandR
notifications, SIGWINCH resizing and coalescing, font-size changes, drawing,
pointer bounds, allocation failure recovery, and shutdown with a pending resize:

```sh
meson test -C build-xsixel --suite xsixel --print-errorlogs
```

The suite requires Python 3, libX11, libXrandr, `xdpyinfo`, and `xsetroot`.

Xsixel disables SIXEL scrolling. The libsixel backend converts and transmits
only the damaged rectangle, using transparent SIXEL rows and columns to position
it from the top-left without erasing surrounding pixels. Vertical damage is
aligned to six-pixel bands. Full redraws are used for initialization, resizing,
and explicit refresh. Pending updates are collected at 8 ms intervals; unchanged
frames are not transmitted. The optional Chafa backend still uses full redraws.
The terminal's SIXEL mode is saved on startup and restored on exit.

When built with libX11 and libXfixes development files, Xsixel uses the outer
terminal's `DISPLAY` and `WINDOWID` to hide its host pointer while the pointer is
inside that window. The pointer is restored on leaving the terminal and on
Xsixel exit. Without that support, Xsixel requests xterm's pointer-hiding mode,
whose effect depends on the terminal. No additional xterm startup option is
needed for pointer hiding.

Xterm's SIXEL graphics limit is independent of its window size and defaults to
1000x1000. The transparent-offset renderer uses absolute graphic coordinates, so
set `maxGraphicSize` large enough for the intended terminal dimensions (as in the
command above). Otherwise RandR can report a correctly enlarged screen while
pixels beyond the terminal's graphics limit are clipped. Xterm does not permit
changing this resource through XTSMGRAPHICS after startup; restart the outer
terminal to apply it.
