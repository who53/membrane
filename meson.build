project(
  'membrane',
  ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'b_lto=true',
    'b_ndebug=if-release',
  ],
)

add_project_arguments(
  '-fmacro-prefix-map=../=',
  language: ['c', 'cpp']
)

incdir = include_directories('include')

libhwc2_dep = dependency('libhwc2', method: 'pkg-config')
libgralloc_dep = dependency('libgralloc', method: 'pkg-config')
libdrm_dep = dependency('libdrm', method: 'pkg-config')
libdroid_dep = dependency('libdroid-0', method: 'pkg-config')
gbm_dep = dependency('gbm', method: 'pkg-config')

install_data(
  'data/zz-membrane.sh',
  install_dir: get_option('sysconfdir') / 'profile.d'
)

subdir('daemon')
subdir('gbm')
