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

hybris_egl_platform_dep = dependency('hybris-egl-platform')
libdrm_dep = dependency('libdrm')
libgralloc_dep = dependency('libgralloc')

install_data(
  'data/zz-membrane.sh',
  install_dir: get_option('sysconfdir') / 'profile.d'
)

subdir('daemon')
subdir('gbm')
subdir('eglplatform')
