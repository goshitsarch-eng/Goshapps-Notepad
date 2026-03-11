project('notepad', 'c',
  version : '1.0.0',
  default_options : ['c_std=c17', 'warning_level=2']
)

gtk4 = dependency('gtk4', version : '>=4.10')
libadwaita = dependency('libadwaita-1', version : '>=1.0')

src = files(
  'src/main.c',
  'src/mainwindow.c',
  'src/dialogs.c',
)

executable('notepad', src,
  dependencies : [gtk4, libadwaita],
  install : true,
)
