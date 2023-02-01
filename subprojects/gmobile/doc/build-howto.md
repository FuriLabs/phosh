Title: Compiling with gmobile
Slug: building

# Compiling with gmobile

If you need to build gmobile, get the source from
[here](https://gitlab.gnome.org/guidog/gmobile/) and see the `README.md` file.

## Bundling the library

gmobile is currently not meant to be used as a shared library. It can be bundled
in one of two ways:

### As a git submodule

To use it as a submodule add the submodule to git

```
git submodule add https://gitlab.gnome.org/guidog/gmobile.git subprojects/gmobile
```

And then add this to your `meson.build`:

```meson
gmobile = subproject('gmobile',
  default_options: [
    'package_name=' + meson.project_name(),
    'package_version=' + meson.project_version(),
    'pkgdatadir=' + pkgdatadir,
    'pkglibdir=' + pkglibdir,
    'examples=false',
    'gtk_doc=false',
    'tests=false',
  ])
gmobile_dep = gmobile.get_variable('gmobile_dep')
```

### As a meson subproject

To use it as a meson subproject add this to `subprojects/gmobile.wrap`:

```ini
[wrap-git]
directory=gmobile
url=https://gitlab.gnome.org/guidog/gmobile.git
revision=main
depth=1
```

You can then use `gmobile_dep` in your mesn build files like:

```meson
gmobile = dependency('gmobile',
                     fallback: ['gmobile', 'gmobile_dep'],
			         native: true)
```
