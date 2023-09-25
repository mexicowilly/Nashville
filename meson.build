project('nashville', 'cpp', default_options : [ 'cpp_std=c++17' ])

comp = meson.get_compiler('cpp')
if comp.has_argument('-fPIC')
    pic_flag = '-fPIC'
else
    pic_flag = ''
endif
if comp.has_link_argument('-Wl,--copy-dt-needed-entries')
    dt_flag = '-Wl,--copy-dt-needed-entries'
else
    dt_flag = ''
endif

chucho_lib = comp.find_library('chucho', dirs: [ '/usr/local/lib' ])

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: [ 'Widgets' ])
ui_files = qt5.preprocess(ui_files: [ 'nashville.ui' ])
moc_files = qt5.preprocess(moc_headers: [ 'title_text.hpp' ])
qmake = find_program('qmake')
qt5_includes = run_command(qmake, '-query', 'QT_INSTALL_HEADERS').stdout().strip()

sources = [
    ui_files,
    moc_files,
    'app.cpp',
    'app.hpp',
    'bar.hpp',
    'chord.hpp',
    'main.cpp',
    'title_text.cpp',
    'title_text.hpp'
]

executable('nashville',
           sources,
           dependencies: [ chucho_lib, qt5_dep ],
           include_directories: [ '/usr/local/include', qt5_includes ],
           cpp_args: [ pic_flag ],
           link_args: [ dt_flag ])
