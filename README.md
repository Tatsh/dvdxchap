# dvdxchap

This is only the `dvdxchap` tool from [OGMTools](http://www.bunkus.org/videotools/ogmtools/index.html) by Moritz Bunkus.

# Build and install

You need to have [CMake]() and [libdvdread](https://github.com/mirror/libdvdread) installed. Run the following commands after cloning the repository:

```
cd where-project-is-cloned
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
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
