
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <gdk/gdkx.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <ctype.h>

#if GTK_CHECK_VERSION(3, 0, 0)
#include <libportal-gtk3/portal-gtk3.h>
#else
#include <libportal/portal.h>
#endif

#include "config.h"

#ifndef GTK_PORTAL_DLSYM_VERSION
#define GTK_PORTAL_DLSYM_VERSION "GLIBC_2.2.5"
#endif

#define FUNC_ENTER printf("%s\n", __PRETTY_FUNCTION__);
#define FUNC_EXIT printf("%s\n", __PRETTY_FUNCTION__);

/*
 * For SWT apps (e.g. eclipse) we need to override dlsym, but we can only do this if
 * dlvsym is present in libdl. dlvsym is needed so that we can access the real dlsym
 * as well as our fake dlsym
 */
#ifdef HAVE_DLVSYM
static void *real_dlsym(void *handle, const char *name);
#else
#define real_dlsym(A, B) dlsym(A, B)
#endif

typedef enum {
	APP_ANY,
	// these apps need special treatment
	APP_FIREFOX,
} Application;

static XdpPortal *portal = NULL;
static const char *P_gtkAppName = NULL;
static Application P_gtkApp = APP_ANY;

#define MAX_DATA_LEN 4096
#define MAX_FILTER_LEN 256
#define MAX_LINE_LEN 1024
#define MAX_APP_NAME_LEN 32

static char *P_gtk_get_app_name(int pid)
{
	static char app_name[MAX_APP_NAME_LEN + 1] = "\0";
	
	int  procFile = -1;
	char cmdline[MAX_LINE_LEN + 1];
	
	sprintf(cmdline, "/proc/%d/cmdline", pid);
	
	if (-1 != (procFile = open(cmdline, O_RDONLY))) {
		if (read(procFile, cmdline, MAX_LINE_LEN) > 2) {
			int len = strlen(cmdline),
			pos = 0;
		
			for (pos = len - 1; pos > 0 && cmdline[pos] && cmdline[pos] != '/'; --pos);
		
			if (pos >= 0 && pos < len) {
			strncpy(app_name, &cmdline[pos ? pos + 1 : 0], MAX_APP_NAME_LEN);
			app_name[MAX_APP_NAME_LEN] = '\0';
			}
		}
		
		close(procFile);
	}
	
	return app_name;
}

/* Try to get name of application executable - either from argv[0], or /proc */
static const gchar *getAppName(const gchar *app)
{
	static const char *appName = NULL;
	
	if (!appName) {
		/* Is there an app name set?  - if not read from /proc */
		const gchar *a = app ? app : P_gtk_get_app_name(getpid());
		gchar *slash;
	
		/* Was the cmdline app java? if so, try to use its parent name - just in case
		its run from a shell script, etc. - e.g. as eclipse does */
		if (a && 0 == strcmp(a, "java")) {
			a = P_gtk_get_app_name(getppid());
		}
	
		if (a && a[0] == '\0') {
			a = NULL;
		}
	
		appName = a && (slash = strrchr(a, '/')) && '\0' != slash[1]
			? &(slash[1])
			: a ? a : "GtkApp";
	}
	
#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::getAppName: %s\n", appName);
#endif
	return appName;
}

static gboolean isApp(const char *str, const char *app)
{
	/* Standard case... */
	if (0 == strcmp(str, app)) {
		return TRUE;
	}

	/* Autopackage'd app */
#define AUTOPACKAGE_PROXY     ".proxy."
#define AUTOPACKAGE_PROXY_LEN 7

	if (str == strstr(str, ".proxy.") && strlen(str) > AUTOPACKAGE_PROXY_LEN &&
		0 == strcmp(&str[AUTOPACKAGE_PROXY_LEN], app)) {
		return TRUE;
	}

	/* gimp and mozilla */
	{
		unsigned int app_len = strlen(app);
		
		if (strlen(str) > app_len && str == strstr(str, app) &&
		   (0 == memcmp(&str[app_len], "-2", 2) ||
		    0 == memcmp(&str[app_len], "-bin", 4))) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean isMozApp(const char *app, const char *check)
{
	if (0 == strcmp(app, check)) {
		return TRUE;
	} else if (app == strstr(app, check)) {
		int app_len = strlen(app), check_len = strlen(check);
		
		if (check_len + 4 == app_len && 0 == strcmp(&app[check_len], "-bin")) {
			return TRUE;
		}
		
		/* OK check for xulrunner-1.9 */
		{
			float dummy;
		
			if (app_len > (check_len + 1) && 1 == sscanf(&app[check_len + 1], "%f", &dummy)) {
				return TRUE;
			}
		}
	}
	
	return FALSE;
}

static void P_gtkExit()
{
	if (portal!=NULL) {
		free(portal);
	portal = NULL;
}
}

static gboolean P_gtkInit(const char *appName)
{
	static gboolean initialised = FALSE;
	
	if (!initialised) {
		initialised = TRUE;
		P_gtkAppName = getAppName(appName);
		
		// open portal
		portal = xdp_portal_new();
		atexit(&P_gtkExit);
	
		const gchar *prg = getAppName(NULL);
	
		if (prg) {
#ifdef GTK_PORTAL_DEBUG
			printf("GTK Portal::APP %s\n", prg);
#endif
			if (isMozApp(prg, "palemoon") ||
			    isMozApp(prg, "librewolf") ||
			    isMozApp(prg, "firefox") ||
			    isMozApp(prg, "swiftfox") ||
			    isMozApp(prg, "iceweasel") ||
			    isMozApp(prg, "xulrunner")) {
				P_gtkApp = APP_FIREFOX;
#ifdef GTK_PORTAL_DEBUG
				printf("GTK Portal::Firefox\n");
#endif
			}
		}
	}
	
	return initialised;
}

static GtkWidget *
P_gtk_file_chooser_dialog_new_valist(const gchar          *title,
                                     GtkWindow            *parent,
                                     GtkFileChooserAction  action,
                                     const gchar          *backend,
                                     const gchar          *first_button_text,
                                     va_list               varargs)
{
	GtkWidget *result;
	const char *button_text = first_button_text;
	gint response_id;
	
	result = g_object_new(GTK_TYPE_FILE_CHOOSER_DIALOG,
	                      "title", title,
	                      "action", action,
	                      "file-system-backend", backend,
	                      NULL);
	
	if (parent) {
#ifdef GTK_PORTAL_DEBUG
		printf("Setting transient for window %p\n", parent);
#endif
		gtk_window_set_transient_for(GTK_WINDOW(result), parent);
	}
	
	while (button_text) {
		response_id = va_arg(varargs, gint);
		gtk_dialog_add_button(GTK_DIALOG(result), button_text, response_id);
		button_text = va_arg(varargs, const gchar *);
	}
	
	return result;
}
/* ......................... */

gboolean gtk_init_check(int *argc, char ***argv)
{
	static void *(*realFunction)() = NULL;
	
	gboolean rv = FALSE;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_init_check");
	}
	
	rv = realFunction(argc, argv) ? TRUE : FALSE;
#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::gtk_init_check\n");
#endif
	
	if (rv) {
		P_gtkInit(argv && argc ? (*argv)[0] : NULL);
	}
	
	return rv;
}

void gtk_init(int *argc, char ***argv)
{
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_init");
	}
	
	realFunction(argc, argv);
#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::gtk_init\n");
#endif
	P_gtkInit(argv && argc ? (*argv)[0] : NULL);
}

/* Store a hash from widget pointer to folder/file list retried from KDialogD */
static GHashTable *fileDialogHash = NULL;

typedef struct {
	gchar    *folder;
	gchar    *name;
	GSList   *files;
	int      ok,
	         cancel;
	gboolean setOverWrite,
	doOverwrite;
} P_GtkFileData;

static P_GtkFileData *lookupHash(void *hash, gboolean create)
{
	P_GtkFileData *rv = NULL;

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::lookupHash %p\n", hash);
#endif

	if (!fileDialogHash) {
		fileDialogHash = g_hash_table_new(g_direct_hash, g_direct_equal);
	}

	rv = (P_GtkFileData *)g_hash_table_lookup(fileDialogHash, hash);

	if (!rv && create) {
		rv = (P_GtkFileData *)malloc(sizeof(P_GtkFileData));
		rv->folder = NULL;
		rv->files = NULL;
		rv->name = NULL;
		rv->ok = GTK_RESPONSE_OK;
		rv->cancel = GTK_RESPONSE_CANCEL;
		rv->setOverWrite = FALSE;
		rv->doOverwrite = FALSE;
		g_hash_table_insert(fileDialogHash, hash, rv);
		rv = g_hash_table_lookup(fileDialogHash, hash);
	}

	return rv;
}

static void freeHash(void *hash)
{
	P_GtkFileData *data = NULL;

	if (!fileDialogHash) {
		fileDialogHash = g_hash_table_new(g_direct_hash, g_direct_equal);
	}

	data = (P_GtkFileData *)g_hash_table_lookup(fileDialogHash, hash);

	if (data) {
		if (data->folder) {
			g_free(data->folder);
		}

		if (data->name) {
			g_free(data->name);
		}

		if (data->files) {
			g_slist_foreach(data->files, (GFunc)g_free, NULL);
			g_slist_free(data->files);
		}

		data->files = NULL;
		data->folder = NULL;
		data->name = NULL;
		g_hash_table_remove(fileDialogHash, hash);
	}
}

static GtkWidget *getCombo(GtkWidget *widget, GSList *ignore)
{
	if (widget && GTK_IS_CONTAINER(widget)) {
		GList     *children = gtk_container_get_children(GTK_CONTAINER(widget)),
		          *child    = children;
		GtkWidget *w        = 0;
		
		for (; child && !w; child = child->next) {
			GtkWidget *boxChild = (GtkWidget *)child->data;
		
			if (ignore && g_slist_find(ignore, boxChild)) {
				continue;
			}

#if GTK_CHECK_VERSION(3, 0, 0)

			if (GTK_IS_COMBO_BOX_TEXT(boxChild))
#else
			if (GTK_IS_COMBO_BOX(boxChild))
#endif
			w = boxChild;
			else if (GTK_IS_CONTAINER(boxChild)) {
                		GtkWidget *box = getCombo(boxChild, ignore);

				if (box) {
					w = box;
				}
			}
		}

		if (children) {
			g_list_free(children);
		}

		if (w) {
			return w;
		}
	}

	return NULL;
}

static void getToggleButtons(GtkWidget *widget, GSList **widgets)
{
	if (widget && GTK_IS_CONTAINER(widget)) {
		GList *children = gtk_container_get_children(GTK_CONTAINER(widget)),
		      *child    = children;
		
		for (; child; child = child->next) {
			GtkWidget *boxChild = (GtkWidget *)child->data;
		
			if (GTK_IS_CHECK_BUTTON(boxChild) && gtk_widget_get_visible(boxChild)) {
				*widgets = g_slist_append(*widgets, boxChild);
			} else if (GTK_IS_CONTAINER(boxChild)) {
				getToggleButtons(boxChild, widgets);
			}
		}
		
		if (children) {
			g_list_free(children);
		}
	}
}

static GSList *addProtocols(GSList *files)
	{
		GSList *item = files;
		
		for (; item; item = g_slist_next(item)) {
			gchar *cur = item->data;
			item->data = g_filename_to_uri(item->data, NULL, NULL);
			g_free(cur);
	}
	
	return files;
}

void gtk_window_present(GtkWindow *window)
{
	static void *(*realFunction)() = NULL;

	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_window_present");
	}

#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::gtk_window_present %s %d\n", g_type_name(G_OBJECT_TYPE(window)),
	       GTK_IS_FILE_CHOOSER(window));
#endif

	if (GTK_IS_FILE_CHOOSER(window))
	{
		gtk_dialog_run(GTK_DIALOG(window));
	} else {
		realFunction(window);
	}
}

void gtk_widget_show(GtkWidget *widget)
{
    static void *(*realFunction)() = NULL;

    if (!realFunction) {
        realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_widget_show");
    }

    if (widget && !GTK_IS_FILE_CHOOSER_BUTTON(widget) && GTK_IS_FILE_CHOOSER(widget)) {
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_widget_show %s %d\n", g_type_name(G_OBJECT_TYPE(widget)),
                                         GTK_IS_FILE_CHOOSER(widget));
#endif
        gtk_dialog_run(GTK_DIALOG(widget));
        /* GTK_OBJECT_FLAGS(widget)|=GTK_REALIZED; */
        gtk_widget_set_realized(widget, TRUE);
    } else {
        realFunction(widget);
    }
}

void gtk_widget_hide(GtkWidget *widget)
{
    static void *(*realFunction)() = NULL;

    FUNC_ENTER

    if (!realFunction) {
        realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_widget_hide");
    }

    if (widget && !GTK_IS_FILE_CHOOSER_BUTTON(widget) && GTK_IS_FILE_CHOOSER(widget)) {
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_widget_hide %s %d\n", g_type_name(G_OBJECT_TYPE(widget)),
                                         GTK_IS_FILE_CHOOSER(widget));
#endif

        if (gtk_widget_get_realized(widget)) {
            gtk_widget_set_realized(widget, FALSE);
        }

        /*if(GTK_OBJECT_FLAGS(widget)&GTK_REALIZED)
            GTK_OBJECT_FLAGS(widget)-=GTK_REALIZED;*/
    } else {
        realFunction(widget);
    }

    FUNC_EXIT
}

gboolean gtk_file_chooser_get_do_overwrite_confirmation(GtkFileChooser *widget)
{
    static gboolean(*realFunction)(GtkFileChooser * chooser) = NULL;

    gboolean rv = FALSE;

    if (!realFunction) {
        realFunction = real_dlsym(RTLD_NEXT, "gtk_file_chooser_get_do_overwrite_confirmation");
    }

    if (realFunction) {
        P_GtkFileData *data = lookupHash(widget, FALSE);

        if (data) {
            if (!data->setOverWrite) {
                data->setOverWrite = TRUE;
                data->doOverwrite = (gboolean) realFunction(widget);
            }

            rv = data->doOverwrite;
        } else {
            rv = (gboolean) realFunction(widget);
        }
    }

    return rv;
}

/* ext => called from app, not kgtk */
void P_gtkFileChooserSetDoOverwriteConfirmation(GtkFileChooser *widget, gboolean v, gboolean ext)
{
    static void *(*realFunction)() = NULL;

    if (!realFunction) {
        realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_set_do_overwrite_confirmation");
    }

    if (realFunction) {
        realFunction(widget, v);

        if (ext) {
            P_GtkFileData *data = lookupHash(widget, FALSE);

            if (data) {
                data->setOverWrite = TRUE;
                data->doOverwrite = v;
            }
        }
    }
}

/* ---------- File chooser dialog ---------- */

static gboolean dialogRunning = FALSE;
static void _portalOpenFileDialogCallback(GObject *object, GAsyncResult *result, gpointer ret)
{
	*(GVariant **)ret = xdp_portal_open_file_finish(portal, result, NULL);
	dialogRunning = FALSE;
}

static void _portalSaveFileDialogCallback(GObject *object, GAsyncResult *result, gpointer ret)
{
	*(GVariant **)ret = xdp_portal_save_file_finish(portal, result, NULL);
	dialogRunning = FALSE;
}

gint gtk_dialog_run(GtkDialog *dialog)
{
	static gint(*realFunction)(GtkDialog * dialog) = NULL;

	if (!realFunction) {
		realFunction = real_dlsym(RTLD_NEXT, "gtk_dialog_run");
	}

#ifdef GTK_PORTAL_DEBUG
        printf("GTK Portal::gtk_dialog_run %s \n", dialog ? g_type_name(G_OBJECT_TYPE(dialog)) : "<null>");
#endif

	if (P_gtkInit(NULL) && GTK_IS_FILE_CHOOSER(dialog) && !dialogRunning) {
		GtkFileChooserAction act = gtk_file_chooser_get_action(GTK_FILE_CHOOSER(dialog));
		const gchar *title = gtk_window_get_title(GTK_WINDOW(dialog));
		XdpParent *p_window = NULL;
		P_GtkFileData *data = lookupHash(dialog, TRUE);
		gint resp = data->cancel;
		GVariant *ret = NULL;

		gtk_file_chooser_get_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog));
		P_gtkFileChooserSetDoOverwriteConfirmation(GTK_FILE_CHOOSER(dialog), FALSE, FALSE);
		
#if GTK_CHECK_VERSION(3, 0, 0)
		p_window = xdp_parent_new_gtk(gtk_window_get_transient_for(GTK_WINDOW(dialog)));
#endif

		dialogRunning = TRUE;

		switch (act) {
			case GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:
			case GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER:
				// there are no libportal calls for opening folders
				return (gint)realFunction(dialog);
				break;
			case GTK_FILE_CHOOSER_ACTION_OPEN:
				if (gtk_file_chooser_get_select_multiple(GTK_FILE_CHOOSER(dialog))) {
					xdp_portal_open_file(portal, p_window, title, NULL, NULL, NULL, XDP_OPEN_FILE_FLAG_MULTIPLE, NULL, _portalOpenFileDialogCallback, &ret);
				} else {
					xdp_portal_open_file(portal, p_window, title, NULL, NULL, NULL, XDP_OPEN_FILE_FLAG_NONE, NULL, _portalOpenFileDialogCallback, &ret);
				}
				break;
			case GTK_FILE_CHOOSER_ACTION_SAVE:
				xdp_portal_save_file(portal, p_window, title, data->name, data->folder, NULL, NULL, NULL, NULL, XDP_SAVE_FILE_FLAG_NONE, NULL, _portalSaveFileDialogCallback, &ret);
				break;
		}

		// unblock async, application freezes without this
		// dialogRunning gets set to false in the portal callback
		do {
			gtk_main_iteration_do(TRUE);
		} while (dialogRunning);

		if (ret!=NULL) {
			gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(dialog));
			const char **uris;
			g_variant_lookup(ret, "uris", "^a&s", &uris);

			for (int i = 0; uris[i]; i++) {
				gtk_file_chooser_select_uri(GTK_FILE_CHOOSER(dialog), uris[i]);
			}

			resp = data->ok;

			g_free(uris);
		}

		// fixes a case in some apps not accepting a response the first time after setting the folder (maybe)
		for (int i=0; i<20; i++) do {
			gtk_main_iteration_do(FALSE);
		} while (gtk_events_pending());

		g_signal_emit_by_name(dialog, "response", resp);
		return resp;
	}

	return (gint)realFunction(dialog);
}

void gtk_widget_destroy(GtkWidget *widget)
{
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_widget_destroy");
	}
	
	if (fileDialogHash && GTK_IS_FILE_CHOOSER(widget)) {
		freeHash(widget);
	}
	
	realFunction(widget);
}

#if GTK_CHECK_VERSION(3, 0, 0)
void gtk_native_dialog_destroy(GtkNativeDialog* dialog) {
	freeHash(GTK_WIDGET(dialog));
}
#else
void gtk_native_dialog_destroy(GtkWidget* widget) {
	freeHash(widget);
}
#endif

gchar *gtk_file_chooser_get_filename(GtkFileChooser *chooser)
{
	P_GtkFileData *data = lookupHash(chooser, FALSE);
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_filename %d %s\n", data ? g_slist_length(data->files) : 12345,
                        data && data->files && data->files->data ? (const char *)data->files->data : "<>");
#endif
	return data && data->files && data->files->data ? g_strdup(data->files->data) : NULL;
}

gboolean gtk_file_chooser_select_filename(GtkFileChooser *chooser, const char *filename)
{
	P_GtkFileData *data = lookupHash(chooser, TRUE);
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_select_filename");
	}
	
	realFunction(chooser, filename);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_select_filename %s, %d\n",
       filename, data ? g_slist_length(data->files) : 12345);
#endif

	if (data && filename) {
		GSList *c = data->files;

		for (; c; c = g_slist_next(c)) {
			if (c->data && 0 == strcmp((char *)(c->data), filename)) {
				break;
			}
		}

		if (!c) {
			gchar *folder = g_path_get_dirname(filename);
		
			data->files = g_slist_prepend(data->files, g_strdup(filename));

			if (folder) {
				gtk_file_chooser_set_current_folder(chooser, folder);
				g_free(folder);
			}
		}
	}
	
	return TRUE;
}

gboolean gtk_file_chooser_select_uri(GtkFileChooser *chooser, const char *uri)
{
	return gtk_file_chooser_select_filename(chooser, g_filename_from_uri(uri, NULL, NULL));
}

void gtk_file_chooser_unselect_all(GtkFileChooser *chooser)
{
	P_GtkFileData *data = lookupHash(chooser, TRUE);
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_unselect_all");
	}
	
	realFunction(chooser);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_unselect_all %d\n", data ? g_slist_length(data->files) : 12345);
#endif

	if (data && data->files) {
		g_slist_foreach(data->files, (GFunc)g_free, NULL);
		g_slist_free(data->files);
		data->files = NULL;
	}
}

gboolean gtk_file_chooser_set_filename(GtkFileChooser *chooser, const char *filename)
{
	P_GtkFileData *data = lookupHash(chooser, TRUE);
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_set_filename");
	}
	
	realFunction(chooser, filename);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_set_filename %s %d\n", filename,
                                     data ? g_slist_length(data->files) : 12345);
#endif

	if (data && filename) {
		gchar *folder = g_path_get_dirname(filename),
		      *name = g_path_get_basename(filename);
		
		if (data->files) {
			g_slist_foreach(data->files, (GFunc)g_free, NULL);
			g_slist_free(data->files);
			data->files = NULL;
		}
		
		data->files = g_slist_prepend(data->files, g_strdup(filename));
		
		if (name && (!data->name || strcmp(name, data->name))) {
			gtk_file_chooser_set_current_name(chooser, name);
		}
		
		if (name) {
			g_free(name);
		}
		
		if (folder && (!data->folder || strcmp(folder, data->folder))) {
			gtk_file_chooser_set_current_folder(chooser, folder);
		}
		
		if (folder) {
			g_free(folder);
		}
	}
	
	return TRUE;
}

void gtk_file_chooser_set_current_name(GtkFileChooser *chooser, const char *filename)
{
	P_GtkFileData       *data = lookupHash(chooser, TRUE);
	GtkFileChooserAction act = gtk_file_chooser_get_action(chooser);
	
	if (GTK_FILE_CHOOSER_ACTION_SAVE == act || GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER == act) {
		static void *(*realFunction)() = NULL;
		
		if (!realFunction) {
			realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_set_current_name");
		}
		
		realFunction(chooser, filename);
	}

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_set_current_name %s %d\n", filename,
                                     data ? g_slist_length(data->files) : 12345);
#endif

	if (data && filename) {
		if (data->name) {
			g_free(data->name);
		}
		
		data->name = g_strdup(filename);
	}
}

GSList *gtk_file_chooser_get_filenames(GtkFileChooser *chooser)
{
	P_GtkFileData *data = lookupHash(chooser, FALSE);
	GSList       *rv = NULL;

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_filenames %d\n", data ? g_slist_length(data->files) : 12345);
#endif

	if (data && data->files) {
		GSList *item = data->files;
		
		for (; item; item = g_slist_next(item)) {
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::FILE:%s\n", (const char *)item->data);
#endif

			if (item->data) {
				rv = g_slist_prepend(rv, g_strdup(item->data));
			}
		}
	}

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_filenames END\n");
#endif
	return rv;
}

gboolean gtk_file_chooser_set_current_folder(GtkFileChooser *chooser, const gchar *folder)
{
	P_GtkFileData *data = lookupHash(chooser, TRUE);
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_set_current_folder");
	}
	
	//realFunction(chooser, folder);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_set_current_folder %s %d\n", folder,
       data ? g_slist_length(data->files) : 12345);
#endif

	if (data && folder) {
		if (data->folder) {
			g_free(data->folder);
		}
		
		data->folder = g_strdup(folder);
	}
	
	g_signal_emit_by_name(chooser, "current-folder-changed", 0);
	
	return TRUE;
}

gchar *gtk_file_chooser_get_current_folder(GtkFileChooser *chooser)
{
	P_GtkFileData *data = lookupHash(chooser, FALSE);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_current_folder %d\n",
                                     data ? g_slist_length(data->files) : 12345);
#endif

	if (!data) {
		gtk_file_chooser_set_current_folder(chooser, get_current_dir_name());
		data = g_hash_table_lookup(fileDialogHash, chooser);
	}
	
	return data && data->folder ? g_strdup(data->folder) : NULL;
}

gchar *gtk_file_chooser_get_uri(GtkFileChooser *chooser)
{
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_uri\n");
#endif
	gchar *filename = gtk_file_chooser_get_filename(chooser);
	
	if (filename) {
		gchar *uri = g_filename_to_uri(filename, NULL, NULL);
		
		g_free(filename);
		return uri;
	}
	
	return NULL;
}

gboolean gtk_file_chooser_set_uri(GtkFileChooser *chooser, const char *uri)
{
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_set_uri\n");
#endif

	gchar    *file = g_filename_from_uri(uri, NULL, NULL);
	gboolean rv = FALSE;
	
	if (file) {
		rv = gtk_file_chooser_set_filename(chooser, file);
		
		g_free(file);
	}
	
	return rv;
}

GSList *gtk_file_chooser_get_uris(GtkFileChooser *chooser)
{
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_uris\n");
#endif
	return addProtocols(gtk_file_chooser_get_filenames(chooser));
}

gboolean gtk_file_chooser_set_current_folder_uri(GtkFileChooser *chooser, const gchar *uri)
{
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_set_current_folder_uri\n");
#endif
	gchar    *folder = g_filename_from_uri(uri, NULL, NULL);
	gboolean rv = FALSE;
	
	if (folder) {
		rv = gtk_file_chooser_set_current_folder(chooser, folder);
		
		g_free(folder);
	}
	
	return rv;
}

gchar *gtk_file_chooser_get_current_folder_uri(GtkFileChooser *chooser)
{
#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_get_current_folder_uri\n");
#endif

	gchar *folder = gtk_file_chooser_get_current_folder(chooser);
	
	if (folder) {
		gchar *uri = g_filename_to_uri(folder, NULL, NULL);
		
		g_free(folder);
		return uri;
	}
	
	return NULL;
}

void g_signal_stop_emission_by_name(gpointer instance, const gchar *detailed_signal)
{
	static void *(*realFunction)() = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "g_signal_stop_emission_by_name");
	}

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::g_signal_stop_emission_by_name %s  %s (check)\n", g_type_name(G_OBJECT_TYPE(instance)), detailed_signal);
#endif

	if (!GTK_IS_FILE_CHOOSER(instance) || strcmp(detailed_signal, "response")) {
		realFunction(instance, detailed_signal);
	}

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::g_signal_stop_emission_by_name %s  %s\n", g_type_name(G_OBJECT_TYPE(instance)), detailed_signal);
#endif
}

GtkWidget *gtk_file_chooser_dialog_new(const gchar *title, GtkWindow *parent,
                                       GtkFileChooserAction action, const gchar *first_button_text,
                                       ...)
{
	GtkWidget    *dlg = NULL;
	P_GtkFileData *data = NULL;
	const char   *text = first_button_text;
	gint         id;
	va_list      varargs;
	
	va_start(varargs, first_button_text);
	dlg = P_gtk_file_chooser_dialog_new_valist(title, parent, action, NULL, first_button_text, varargs);
	va_end(varargs);

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::gtk_file_chooser_dialog_new\n");
#endif
	data = lookupHash(dlg, TRUE);
	va_start(varargs, first_button_text);
	
	while (text) {
		id = va_arg(varargs, gint);
		
		if (text && (0 == strcmp(text, GTK_STOCK_CANCEL) || 0 == strcmp(text, GTK_STOCK_CLOSE) ||
		    0 == strcmp(text, GTK_STOCK_QUIT) || 0 == strcmp(text, GTK_STOCK_NO))) {
			data->cancel = id;
		} else if (text && (0 == strcmp(text, GTK_STOCK_OK) || 0 == strcmp(text, GTK_STOCK_OPEN) ||
		    0 == strcmp(text, GTK_STOCK_SAVE) || 0 == strcmp(text, GTK_STOCK_YES))) {
			data->ok = id;
		}
		
		text = va_arg(varargs, const gchar *);
	}
	
	va_end(varargs);
	return dlg;
}

/* ---------- File chooser button ---------- */

typedef struct _GtkFileSystem      GtkFileSystem;
typedef struct _GtkFilePath        GtkFilePath;
typedef struct _GtkFileSystemModel GtkFileSystemModel;

struct _GtkFileChooserButtonPrivate {
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *combo_box;
	
	GtkCellRenderer *icon_cell;
	GtkCellRenderer *name_cell;
	
	GtkTreeModel *model;
	GtkTreeModel *filter_model;

#if !GTK_CHECK_VERSION(3, 0, 0)
	gchar *backend;
#endif
	GtkFileSystem *fs;
	GtkFilePath *old_path;
	
	gulong combo_box_changed_id;
	/* ...and others...
	gulong dialog_file_activated_id;
	gulong dialog_folder_changed_id;
	gulong dialog_selection_changed_id;
	gulong fs_volumes_changed_id;
	gulong fs_bookmarks_changed_id;
	*/
};

/* TreeModel Columns */
enum {
	ICON_COLUMN,
	DISPLAY_NAME_COLUMN,
	TYPE_COLUMN,
	DATA_COLUMN,
#if GTK_CHECK_VERSION(3, 0, 0)
	IS_FOLDER_COLUMN,
	CANCELLABLE_COLUMN,
#endif
	NUM_COLUMNS
};

/* TreeModel Row Types */
typedef enum {
	ROW_TYPE_SPECIAL,
	ROW_TYPE_VOLUME,
	ROW_TYPE_SHORTCUT,
	ROW_TYPE_BOOKMARK_SEPARATOR,
	ROW_TYPE_BOOKMARK,
	ROW_TYPE_CURRENT_FOLDER_SEPARATOR,
	ROW_TYPE_CURRENT_FOLDER,
	ROW_TYPE_OTHER_SEPARATOR,
	ROW_TYPE_OTHER,
	
	ROW_TYPE_INVALID = -1
}
RowType;

static void handleGtkFileChooserButtonClicked(GtkButton *button, gpointer user_data)
{
#ifdef KGTK_DEBUG
        printf("KGTK::handleGtkFileChooserButtonClicked\n");
#endif
	gtk_dialog_run(GTK_DIALOG(GTK_FILE_CHOOSER_BUTTON(user_data)->priv->dialog));
}

static void handleGtkFileChooserComboChanged(GtkComboBox *combo_box, gpointer user_data)
{
	static gboolean handle = TRUE;
	GtkTreeIter iter;

#ifdef KGTK_DEBUG
	printf("KGTK::handleGtkFileChooserComboChanged (handle:%d)\n", handle);
#endif

	if (!handle) {
		return;
	}

	if (gtk_combo_box_get_active_iter(combo_box, &iter)) {
		GtkFileChooserButtonPrivate *priv = GTK_FILE_CHOOSER_BUTTON(user_data)->priv;
		gchar type = ROW_TYPE_INVALID;
		
		gtk_tree_model_get(priv->filter_model, &iter, TYPE_COLUMN, &type, -1);
		
		if (ROW_TYPE_OTHER == type) {
			gtk_dialog_run(GTK_DIALOG(GTK_FILE_CHOOSER_BUTTON(user_data)->priv->dialog));
		} else {
			g_signal_handler_unblock(priv->combo_box, priv->combo_box_changed_id);
			handle = FALSE;
			g_signal_emit_by_name(priv->combo_box, "changed");
			handle = TRUE;
			g_signal_handler_block(priv->combo_box, priv->combo_box_changed_id);
		}
	}
}

GtkWidget *gtk_file_chooser_button_new(const gchar *title, GtkFileChooserAction action)
{
	static void *(*realFunction)() = NULL;
	
	GtkWidget *button = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "gtk_file_chooser_button_new");
	}

#ifdef KGTK_DEBUG
	printf("KGTK::gtk_file_chooser_button_new\n");
#endif

	if (P_gtkInit(NULL)) {
		GtkFileChooserButtonPrivate *priv = NULL;
		
		button = realFunction(title, action);
		priv = GTK_FILE_CHOOSER_BUTTON(button)->priv;
		
		if (priv->button) {
			g_signal_handlers_disconnect_matched(priv->button,
			                                     G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, button);
		
			g_signal_connect(priv->button, "clicked",
			                 G_CALLBACK(handleGtkFileChooserButtonClicked),
			                 GTK_FILE_CHOOSER_BUTTON(button));
		}
		
		if (priv->combo_box) {
			g_signal_handler_block(priv->combo_box, priv->combo_box_changed_id);
		
			g_signal_connect(priv->combo_box, "changed",
			                 G_CALLBACK(handleGtkFileChooserComboChanged),
			                 GTK_FILE_CHOOSER_BUTTON(button));
		}
	}
	
	return button;
}

/* ---------- DLSYM ---------- */

static gboolean isGtk(const char *str)
{
	return 'g' == str[0] && 't' == str[1] && 'k' == str[2] && '_' == str[3];
}

static void *P_gtk_get_fnptr(const char *raw_name)
{
	if (raw_name && isGtk(raw_name) && P_gtkInit(NULL)) {

		if (0 == strcmp(raw_name, "gtk_file_chooser_get_filename")) {
			return &gtk_file_chooser_get_filename;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_select_filename")) {
			return &gtk_file_chooser_select_filename;
		}

		else if (0 == strcmp(raw_name, "gtk_file_chooser_select_uri")) {
			return &gtk_file_chooser_select_uri;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_unselect_all")) {
			return &gtk_file_chooser_unselect_all;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_set_filename")) {
			return &gtk_file_chooser_set_filename;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_set_current_name")) {
			return &gtk_file_chooser_set_current_name;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_get_filenames")) {
			return &gtk_file_chooser_get_filenames;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_set_current_folder")) {
			return &gtk_file_chooser_set_current_folder;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_get_current_folder")) {
			return &gtk_file_chooser_get_current_folder;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_get_uri")) {
			return &gtk_file_chooser_get_uri;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_set_uri")) {
			return &gtk_file_chooser_set_uri;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_get_uris")) {
			return &gtk_file_chooser_get_uris;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_set_current_folder_uri")) {
			return &gtk_file_chooser_set_current_folder_uri;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_get_current_folder_uri")) {
			return &gtk_file_chooser_get_current_folder_uri;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_dialog_new")) {
			return &gtk_file_chooser_dialog_new;
		}
		
		else if (0 == strcmp(raw_name, "gtk_file_chooser_button_new")) {
			return &gtk_file_chooser_button_new;
		}
	}
	
	return NULL;
}

const gchar *P_gtk_g_module_check_init(GModule *module)
{
    return gtk_check_version(GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION - GTK_INTERFACE_AGE);
}

/* Mozilla specific */
void *PR_FindFunctionSymbol(void *lib, const char *raw_name)
{
	static void *(*realFunction)() = NULL;
	
	void *rv = NULL;
	
	if (!realFunction) {
		realFunction = (void *(*)()) real_dlsym(RTLD_NEXT, "PR_FindFunctionSymbol");
	}

#ifdef GTK_PORTAL_DEBUG
printf("GTK Portal::PR_FindFunctionSymbol : %s\n", raw_name);
#endif

	rv = P_gtk_get_fnptr(raw_name);
	
	if (!rv) {
		if (0 == strcmp(raw_name, "g_module_check_init")) {
			rv = &P_gtk_g_module_check_init;
		} else if (isGtk(raw_name)) {
			rv = real_dlsym(RTLD_NEXT, raw_name);
		}
	}
	
	return rv ? rv : realFunction(lib, raw_name);
}

#ifdef HAVE_DLVSYM
/* Overriding dlsym is required for SWT - which dlsym's the gtk_file_chooser functions! */
static void *real_dlsym(void *handle, const char *name)
{
	static void *(*realFunction)() = NULL;

#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::real_dlsym : %s\n", name);
#endif
	
	if (!realFunction) {
		static void *ldHandle;
		
		for (int i=0; !ldHandle && i<2; i++) {
			ldHandle = dlopen((char*[2]){"libdl.so", "libdl.so.2"}[i], RTLD_NOW);
		}
		
		if (ldHandle) {
			static const char *versions[] = {GTK_PORTAL_DLSYM_VERSION, "GLIBC_2.3", "GLIBC_2.2.5",
							"GLIBC_2.2", "GLIBC_2.1", "GLIBC_2.0", NULL
							};
			
			int i;
			
			for (i = 0; versions[i] && !realFunction; ++i) {
				realFunction = dlvsym(ldHandle, "dlsym", versions[i]);
			}
		}
	}
	
	return realFunction(handle, name);
}

void *dlsym(void *handle, const char *name)
{
	void *rv = NULL;

#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::dlsym : (%p) %s\n", handle, name);
#endif
	
	rv = P_gtk_get_fnptr(name);
	
	if (!rv) {
		rv = real_dlsym(handle, name);
	}
	
	if (!rv && 0 == strcmp(name, "g_module_check_init")) {
		rv = &P_gtk_g_module_check_init;
	}

#ifdef GTK_PORTAL_DEBUG
	printf("GTK Portal::dlsym found? %d\n", rv ? 1 : 0);
#endif
	return rv;
}
#endif
