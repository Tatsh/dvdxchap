cmake_minimum_required (VERSION 3.10.3)
project (DVDXCHAP)

find_library(LIBDVDREAD dvdread)
find_path(DVDREAD_DIR dvdread)

include_directories(${DVDREAD_DIR})
add_compile_options(-Wno-unknown-attributes -DVERSION="1.5.1")

add_executable(dvdxchap dvdxchap.c)
target_link_libraries(dvdxchap PUBLIC ${LIBDVDREAD})
