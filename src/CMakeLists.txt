cmake_minimum_required(VERSION 3.5)
project(portal-wrapper)

include(CheckFunctionExists)
check_function_exists(getpeereid HAVE_GETPEEREID)

find_library(LIBDLVSYM_LIBRARY
                NAMES dl
                PATHS /lib /usr/lib /usr/X11R6/lib /usr/local/lib)

if(LIBDLVSYM_LIBRARY)
    set(HAVE_DLVSYM 1)
else(LIBDLVSYM_LIBRARY)
    message("** INFORMATION: Your libdl does not contain dlvsym - SWT apps will not be supported")
endif(LIBDLVSYM_LIBRARY)

add_subdirectory(gtk2)
add_subdirectory(gtk3)

message("** INFORMATION: Using installation prefix: ${CMAKE_INSTALL_PREFIX}")
set(GTK_PORTAL_DEBUG 1)
configure_file(config.h.cmake ${CMAKE_BINARY_DIR}/config.h)
configure_file(portal-wrapper.cmake ${CMAKE_CURRENT_BINARY_DIR}/portal-wrapper @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/portal-wrapper DESTINATION bin)
