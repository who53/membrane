egl_dep = dependency('egl')
wayland_client_dep = dependency('wayland-client')
wayland_egl_dep = dependency('wayland-egl')
wayland_protos = dependency('wayland-protocols')

wayland_scanner = find_program('wayland-scanner')
wl_proto_dir = wayland_protos.get_variable(pkgconfig: 'pkgdatadir')

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

linux_dmabuf_xml = wl_proto_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml'
protos_src = wayland_scanner_code.process(linux_dmabuf_xml)
protos_headers = wayland_scanner_client.process(linux_dmabuf_xml)

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
    egl_dep
  ],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'libhybris'),
)
