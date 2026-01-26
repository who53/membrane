project(
  'membrane',
  ['c', 'cpp'],
  version: '1.0',
  default_options: [
    'b_lto=true',
    'b_ndebug=if-release',
  ],
)

incdir = include_directories('include')

install_data(
  'data/zz-membrane.sh',
  install_dir: get_option('sysconfdir') / 'profile.d'
)

subdir('daemon')
subdir('gbm')
