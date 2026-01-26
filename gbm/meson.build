gbm_dep = dependency('gbm', method: 'pkg-config')
libgralloc_dep = dependency('libgralloc', method: 'pkg-config')

shared_library(
  'membrane_gbm',
  'membrane.c',
  name_prefix: '',
  include_directories: incdir,
  dependencies: [
    gbm_dep,
    libgralloc_dep,
  ],
  install: true,
  install_dir: join_paths(
    get_option('libdir'),
    'gbm',
  ),
)
