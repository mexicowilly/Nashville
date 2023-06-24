project('nashville', 'cpp', default_options : [ 'cpp_std=c++17' ])

comp = meson.get_compiler('cpp')
chucho_lib = comp.find_library('chucho', dirs: [ '/usr/local/lib' ])

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: [ 'Core', 'Gui' ])
ui_files = qt5.preprocess(ui_files: [ 'nashville.ui' ])

executable('nashville',
           [ 'main.cpp', ui_files ],
           dependencies: [ chucho_lib ],
           include_directories: [ '/usr/local/include' ])
