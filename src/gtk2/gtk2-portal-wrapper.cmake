#!/bin/bash

if [ "`locale | grep 'LANG=' | grep -i 'utf-8' | wc -l`" = "0" ] ; then
	export G_BROKEN_FILENAMES=1
fi

app=`basename $0`

if [ "$app" = "gtk2-portal-wrapper" ] ; then
	LD_PRELOAD="@CMAKE_INSTALL_PREFIX@/lib@LIB_SUFFIX@/gtk-portal/libgtk2-portal.so${LIBSUFF}:$LD_PRELOAD" "$@"
else
	dir=`dirname $0`
	oldPath=$PATH
	PATH=`echo $PATH | sed s:$dir::g`
	real=`which $app`
	PATH=$oldPath
	
	if [ "$real" != "" ] && [ "`dirname $real`" != "$dir" ] ; then
		LD_PRELOAD="@CMAKE_INSTALL_PREFIX@/lib@LIB_SUFFIX@/gtk-portal/libgtk2-portal.so${LIBSUFF}:$LD_PRELOAD" $real "$@"
	fi
fi
