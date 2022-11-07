Title: Compiling with libgmobile
Slug: building

# Compiling with libgmobile

If you need to build libgmobile, get the source from
[here](https://gitlab.gnome.org/guidog/libgmobile/) and see the `README.md` file.

## Bundling the library

libgmobile is not meant to be used as a shared library. It should be embedded in your source
tree as a git submodule instead:

```
git submodule add https://gitlab.gnome.org/guidog/libgmobile.git subprojects/libgmobile
```

Add this to your `meson.build`:

```meson
libgmobile = subproject('libgmobile',
  default_options: [
    'package_name=' + meson.project_name(),
    'package_version=' + meson.project_version(),
    'pkgdatadir=' + pkgdatadir,
    'pkglibdir=' + pkglibdir,
    'examples=false',
    'gtk_doc=false',
    'tests=false',
  ])
libgmobile_dep = libgmobile.get_variable('libgmobile_dep')
```
