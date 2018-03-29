# dvdxchap

This is only the `dvdxchap` tool from [OGMTools](http://www.bunkus.org/videotools/ogmtools/index.html) by Moritz Bunkus.

This program exracts chaper information from a DVD source and displays it in a form usable by tools like [mkvmerge](https://mkvtoolnix.download/downloads.html).

## Usage

```bash
dvdxchap [-t TITLE] [-c START[-END]] [-v] [-V] [-h] DVD-SOURCE
```

where:

* `-t`/`--title` - Specify title number, defaulting to 1.
* `-c`/`--chapter` - Specify the chapter range (2-4). This adjusts all timecodes so that they start at 0.
* `-v` - Increase verbosity
* `-V` - Show version infomration
* `-h` - Show help
* `DVD-SOURCE` - Path to a DVD file system root.

# Build and install

You need to have [CMake](https://cmake.org/) and [libdvdread](https://github.com/mirror/libdvdread) installed. Run the following commands after cloning the repository:

```bash
cd where-project-is-cloned
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
make install
```

You can add C flags to the `cmake` invocation with an argument similar to `-DCMAKE_C_FLAGS_RELEASE='-O2 -pipe -march=native -fomit-frame-pointer'`.

You will then have `dvdxchap` in the `build` directory. You can use `make install` to install like normal on a Linux or GNU-convention system (like macOS).

# Contributing guidelines

* Follow standards as seen in the `.clang-format` file
* Use `clang-format` for all files before committing (`clang-format -i *.c *.h`)
* Do not change the compiler flags or `.clang-format`
* Use a debug build while working on the project (`cmake .. -DCMAKE_BUILD_TYPE=Debug`)
* Test your changes with a release build (`cmake .. -DCMAKE_BUILD_TYPE=Release`)
* C++ will not be used in this project
