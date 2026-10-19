sixel_dep = dependency('libsixel')
chafa_dep = dependency('chafa', required: get_option('chafa'))
sixel_config = configuration_data()
sixel_config.set('HAVE_LIBSIXEL', 1)
sixel_config.set('HAVE_CHAFA', get_option('chafa') ? 1 : false)
configure_file(output: 'sixel-config.h', configuration: sixel_config)

executable(
    'Xsixel',
    ['sixel.c', 'sixelinit.c', '../src/kexeccmd.c', '../../stubs/ddxBeforeReset.c'],
    include_directories: [inc, include_directories('../src')],
    dependencies: [common_dep, sixel_dep, chafa_dep, dependency('threads')],
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
