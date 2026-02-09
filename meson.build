project(
  'membrane',
  ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'b_ndebug=if-release',
  ],
)

add_project_arguments(
  '-fmacro-prefix-map=../=',
  language: ['c', 'cpp']
)

incdir = include_directories('include')

pkg = import('pkgconfig')

libhwc2_dep = dependency('libhwc2')
hybris_egl_platform_dep = dependency('hybris-egl-platform')
libgralloc_dep = dependency('libgralloc')
libdrm_dep = dependency('libdrm')
libdroid_dep = dependency('libdroid-0')
gbm_dep = dependency('gbm')
wayland_egl_dep = dependency('wayland-egl')
wayland_client_dep = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
libsync_dep = dependency('libsync')

install_data(
  'data/zz-membrane.sh',
  install_dir: get_option('sysconfdir') / 'profile.d'
)

subdir('daemon')
subdir('gbm')
subdir('eglplatform')
