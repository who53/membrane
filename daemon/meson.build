libhwc2_dep = dependency('libhwc2', method: 'pkg-config')
hybris_egl_platform_dep = dependency('hybris-egl-platform', method: 'pkg-config')
libgralloc_dep = dependency('libgralloc', method: 'pkg-config')
drm_dep = dependency('libdrm', method: 'pkg-config')
libdroid_dep = dependency('libdroid-0', method: 'pkg-config')

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
    drm_dep,
    libdroid_dep,
  ],
  install: true,
)
