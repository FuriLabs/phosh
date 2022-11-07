gm_deps = [
  glib_dep,
  cc.find_library('m', required: false),
  cc.find_library('rt', required: false),
]

gm_sources = files(
  'gmobile.h',
  'gm-timeout.c',
  'gm-timeout.h',
)

gm_c_args = [
  '-DG_LOG_DOMAIN="gmobile"',
]

gm_lib = static_library(
  'gmobile',
  gm_sources,
  include_directories: root_inc,
  c_args: gm_c_args,
  dependencies: gm_deps)

libgmobile_dep = declare_dependency(
  include_directories: [root_inc, include_directories('.')],
  dependencies: gm_deps,
  link_with: gm_lib)

if get_option('gtk_doc')
  libgmobile_gir_extra_args = [
    '--c-include=gmobile.h',
    '-L@0@'.format(meson.current_build_dir()),
    '--quiet',
    '-DGMOBILE_USE_UNSTABLE_API',
   ]

  libgmobile_gir = gnome.generate_gir(gm_lib,
                 sources: gm_sources,
               nsversion: '0',
               namespace: 'Gm',
         export_packages: 'gmobile-0',
           symbol_prefix: 'gm',
       identifier_prefix: 'Gm',
               link_with: gm_lib,
                includes: ['Gio-2.0'],
                 install: false,
              extra_args: libgmobile_gir_extra_args,
   )
endif
