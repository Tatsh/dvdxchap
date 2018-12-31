cmake_minimum_required(VERSION 3.0)
project(dvdxchap)
include(GNUInstallDirs)

find_library(LIBDVDREAD dvdread)
find_path(DVDREAD_DIR dvdread)

include_directories(${DVDREAD_DIR})
add_compile_options(
    -DVERSION="1.5.3"
    -Wno-unknown-attributes
    -fstrict-aliasing
    -fstack-protector
    -Werror
    -Wstack-protector
    -Wno-multichar
    -Wimplicit-fallthrough
    -Wfloat-equal
    -Wunused-macros
    -Wbad-function-cast
    -Wcast-qual
    -Wall
    -Wextra
    -Wuninitialized
    -Wconversion
    -Wlogical-not-parentheses
    -Waggregate-return
    -Wdangling-else
    -Wunused-parameter
    -std=gnu1x
    -pedantic
    -pedantic-errors
    -Wswitch-default
    -Wswitch-enum
    -Wswitch-bool
)

add_executable(dvdxchap dvdxchap.c)
target_link_libraries(dvdxchap PUBLIC ${LIBDVDREAD} m)

install(TARGETS dvdxchap RUNTIME DESTINATION bin)
install(FILES dvdxchap.1 DESTINATION ${CMAKE_INSTALL_FULL_MANDIR}/man1)
