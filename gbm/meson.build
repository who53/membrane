gbm_dep = dependency('gbm')

shared_library(
  'membrane_gbm',
  'membrane.c',
  name_prefix: '',
  include_directories: incdir,
  dependencies: [
    gbm_dep,
    libgralloc_dep,
    libdrm_dep,
  ],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'gbm',),
)
