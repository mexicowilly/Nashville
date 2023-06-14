project('nashville', 'cpp', default_options : [ 'cpp_std=c++17' ])

comp = meson.get_compiler('cpp')
chucho_lib = comp.find_library('chucho', dirs: [ '/usr/local/lib' ])

executable('nashville',
           [ 'main.cpp' ],
           dependencies: [ chucho_lib ],
           include_directories: [ '/usr/local/include' ])
