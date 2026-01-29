wayland_scanner = find_program('wayland-scanner')

wayland_scanner_code = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
  wayland_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

protos_src = wayland_scanner_code.process('linux-dmabuf-v1.xml')
protos_headers = wayland_scanner_client.process('linux-dmabuf-v1.xml')

shared_library(
  'eglplatform_membrane',
  ['eglplatform_membrane.cpp', protos_src, protos_headers],
  name_prefix: '',
  include_directories: incdir,
  dependencies: [
    libgralloc_dep,
    hybris_egl_platform_dep,
    wayland_egl_dep,
    wayland_client_dep,
    libdrm_dep,
    libsync_dep,
  ],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'libhybris'),
)
