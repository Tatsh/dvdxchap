# dvdxchap

This is only the `dvdxchap` tool from [OGMTools](http://www.bunkus.org/videotools/ogmtools/index.html) by Moritz Bunkus.

# Build and install

You need to have [CMake]() and [libdvdread](https://github.com/mirror/libdvdread) installed. Run the following commands after cloning the repository:

```
cd where-project-is-cloned
mkdir build
cd build
cmake ..
make
```

You will then have `dvdxchap` in the `build` directory. You can use `make install` to install like normal on a Linux or GNU-convention system (like macOS).
