include(FindPkgConfig)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

if (GTK3_FOUND)
	message("** INFORMATION: Gtk3 LD_PRELOAD library will be built.")
	
	set(gtk3_portal_SRCS gtk-portal.c)
	# set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
	set(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}  CACHE PATH "The subdirectory relative to the install prefix where libraries will be installed (default is /lib${LIB_SUFFIX})" FORCE)
	
	include_directories(SYSTEM ${GTK3_INCLUDE_DIRS})
	include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/gtk3 ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
	
	add_library(gtk3-portal SHARED ${gtk3_portal_SRCS})
	target_link_libraries(gtk3-portal ${GTK3_LDFLAGS} -lgthread-2.0 -lglib-2.0 -lc -ldl -lportal -lportal-gtk3)
	target_compile_options(gtk3-portal PRIVATE -Werror -Wno-deprecated-declarations)

	install(TARGETS gtk3-portal LIBRARY DESTINATION ${LIB_INSTALL_DIR}/gtk-portal NAMELINK_SKIP)
	
	configure_file(gtk3-portal-wrapper.cmake ${CMAKE_CURRENT_BINARY_DIR}/gtk3-portal-wrapper @ONLY)
	install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/gtk3-portal-wrapper DESTINATION bin)
else (GTK3_FOUND)
	message("** ERROR      : Could not locate Gtk3 headers, Gtk3 LD_PRELOAD library will not be built.")
endif (GTK3_FOUND)
