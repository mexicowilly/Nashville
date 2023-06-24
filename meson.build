project('nashville', 'cpp', default_options : [ 'cpp_std=c++17' ])

comp = meson.get_compiler('cpp')
chucho_lib = comp.find_library('chucho', dirs: [ '/usr/local/lib' ])
if comp.has_argument('-fPIC')
    pic_flag = '-fPIC'
endif

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: [ 'Widgets' ])
ui_files = qt5.preprocess(ui_files: [ 'nashville.ui' ])
qmake = find_program('qmake')
qt_includes = run_command(qmake, '-query', 'QT_INSTALL_HEADERS').stdout().strip()

executable('nashville',
           [ 'main.cpp', ui_files ],
           dependencies: [ chucho_lib, qt5_dep ],
           include_directories: [ '/usr/local/include', qt_includes ],
           cpp_args: [ pic_flag ])
