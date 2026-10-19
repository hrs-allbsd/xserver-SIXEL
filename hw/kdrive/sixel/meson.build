sixel_dep = dependency('libsixel')
chafa_dep = dependency('chafa', required: get_option('chafa'))
sixel_config = configuration_data()
sixel_config.set('HAVE_LIBSIXEL', 1)
sixel_config.set('HAVE_CHAFA', get_option('chafa') ? 1 : false)
configure_file(output: 'sixel-config.h', configuration: sixel_config)

host_x11_dep = dependency('x11', required: false)
host_xfixes_dep = dependency('xfixes', required: false)
host_cursor_args = []
if host_x11_dep.found() and host_xfixes_dep.found()
    host_cursor_args += '-DHAVE_HOST_XFIXES'
endif

xsixel_server = executable(
    'Xsixel',
    ['sixel.c', 'sixelinit.c', 'hostcursor.c', '../src/kexeccmd.c', '../../stubs/ddxBeforeReset.c'],
    include_directories: [inc, include_directories('../src')],
    dependencies: [common_dep, sixel_dep, chafa_dep, host_x11_dep, host_xfixes_dep, dependency('threads')],
    c_args: host_cursor_args,
    link_with: [
        libxserver_main,
        kdrive,
        libxserver_fb,
        libxserver,
        libxserver_config,
        libxserver_xkeyboard_stubs,
        libxserver_xi_stubs,
        libxserver_glx,
        libglxvnd,
    ],
    install: true,
)
