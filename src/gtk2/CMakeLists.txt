include(FindPkgConfig)

find_package(GTK2)

if (GTK2_FOUND)
	message("** INFORMATION: Gtk2 LD_PRELOAD library will be built.")
	
	set(gtk2_portal_SRCS ../common/gtk-portal.c)
	# set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
	set(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}  CACHE PATH "The subdirectory relative to the install prefix where libraries will be installed (default is /lib${LIB_SUFFIX})" FORCE)
	
	include_directories(SYSTEM ${GTK2_INCLUDE_DIRS})
	include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/gtk2 ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
	
	add_library(gtk2-portal SHARED ${gtk2_portal_SRCS})
	target_link_libraries(gtk2-portal ${GTK2_LDFLAGS} -lgthread-2.0 -lglib-2.0 -lc -ldl -lportal)
	target_compile_options(gtk2-portal PRIVATE -Werror)

	install(TARGETS gtk2-portal LIBRARY DESTINATION ${LIB_INSTALL_DIR}/gtk-portal NAMELINK_SKIP)
	
	configure_file(gtk2-portal-wrapper.cmake ${CMAKE_CURRENT_BINARY_DIR}/gtk2-portal-wrapper @ONLY)
	install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/gtk2-portal-wrapper DESTINATION bin)
else (GTK2_FOUND)
	message("** ERROR      : Could not locate Gtk2 headers, Gtk2 LD_PRELOAD library will not be built.")
endif (GTK2_FOUND)
