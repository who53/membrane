executable(
  'membrane',
  [
    'main.c',
    'rwb.cpp',
  ],
  include_directories: incdir,
  dependencies: [
    libhwc2_dep,
    hybris_egl_platform_dep,
    libgralloc_dep,
    libdrm_dep,
    libdroid_dep,
  ],
  install: true,
)
