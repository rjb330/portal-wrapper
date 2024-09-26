#!/bin/bash

if [ "$(locale | grep 'LANG=' | grep -i 'utf-8' | wc -l)" = "0" ] ; then
	export G_BROKEN_FILENAMES=1
fi

app_arg="$1"
shift

app_arg_path="$(dirname "$app_arg")"
app_name="$(basename "$app_arg")"

if [ -n "$app_arg_path" ] && [ -x "$app_arg" ] ; then
	app_abspath="$app_arg"
else
	app_abspath="$(which "$app_name")"
	if [ -z "$app_abspath" ] ; then
		app_abspath="$(which "./$app_name")"
	fi
fi

toolkit=""

if [ "$toolkit" = "" ] && [ ! -z "$app_abspath" ] ; then
	libs="$(ldd "$app_abspath" 2>/dev/null)"
	
	if [ ! -z "$libs" ] ; then
		if [ "0" != "$(echo "$libs" | grep libgtk-x11-2 | wc -l)" ] ; then
			toolkit="gtk2"
		elif [ "0" != "$(echo "$libs" | grep libgtk-3 | wc -l)" ] ; then
			toolkit="gtk3"
		fi
	fi
fi

if [ "$toolkit" = "x" ] ; then
	toolkit=""
fi

libgtk_portal_path="@CMAKE_INSTALL_PREFIX@/lib@LIB_SUFFIX@/gtk-portal/lib${toolkit}-portal.so${LIBSUFF}"

if [ ! -z "$GTK_WRAPPER_DEBUG" ]; then
	echo "Overriding for GTK version $toolkit for app $app_name"
	echo "Override library: $libgtk_portal_path"
fi

if [ ! -z "$toolkit" ] && [ -f "$libgtk_portal_path" ] ; then
	export LD_PRELOAD="$libgtk_portal_path:$LD_PRELOAD"
else
	echo "WARNING: Failed to find override library $libgtk_portal_path"
	echo "Disabled!"
fi

exec "$app_abspath" "$@"
