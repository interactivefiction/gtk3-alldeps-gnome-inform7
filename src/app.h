/* Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2018 P. F. Chimento
 * This file is part of GNOME Inform 7.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _APP_H_
#define _APP_H_

#include "config.h"

#include <stdarg.h>

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <webkit2/webkit2.h>

#include "document.h"
#include "prefs.h"

#define I7_TYPE_APP            (i7_app_get_type())
#define I7_APP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), I7_TYPE_APP, I7App))
#define I7_APP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), I7_TYPE_APP, I7AppClass))
#define I7_IS_APP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), I7_TYPE_APP))
#define I7_IS_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), I7_TYPE_APP))
#define I7_APP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), I7_TYPE_APP, I7AppClass))

enum _I7AppRegices {
	I7_APP_REGEX_HEADINGS, /* Matches story headings in the source text */
	I7_APP_REGEX_UNICODE_ESCAPE, /* Matches Unicode escapes in Javascript paste code */
	I7_APP_REGEX_EXTENSION, /* Matches the title of an extension in the proper format */
	I7_APP_NUM_REGICES
};

enum _I7AppExtensionsColumns {
	I7_APP_EXTENSION_TEXT,  /* title rows are children of author rows */
	I7_APP_EXTENSION_VERSION,
	I7_APP_EXTENSION_READ_ONLY,
	I7_APP_EXTENSION_ICON,
	I7_APP_EXTENSION_FILE,
	I7_APP_NUM_EXTENSION_COLUMNS
};

typedef void (*I7DocumentForeachFunc)(I7Document *, gpointer);

typedef struct {
	GtkApplicationClass parent_class;
} I7AppClass;

typedef struct {
	GtkApplication parent_instance;

	/* Public preferences dialog */
	I7PrefsWidgets *prefs;
	/* Already-compiled regices */
	GRegex *regices[I7_APP_NUM_REGICES];
} I7App;

/**
 * I7AppAuthorFunc:
 * @info: the #GFileInfo for the author directory.
 * @data: user data to pass to the callback.
 *
 * Callback for enumerating installed extensions, called for each author
 * directory. May return a result, which is passed to #I7AppExtensionFunc for
 * each extension file found in that author directory.
 */
typedef gpointer (*I7AppAuthorFunc)(GFileInfo *info, gpointer data);
/**
 * I7AppExtensionFunc:
 * @parent: the #GFile for the extension file's parent.
 * @info: the #GFileInfo for the extension file.
 * @author_result: the return value of the #I7AppAuthorFunc for the parent.
 * @data: user data to pass to the callback.
 *
 * Callback for enumerating installed extensions, called for each author
 * directory.
 */
typedef void (*I7AppExtensionFunc)(GFile *parent, GFileInfo *info, gpointer author_result, gpointer data);

GType i7_app_get_type(void) G_GNUC_CONST;
I7App *i7_app_new(void);

I7Document *i7_app_get_already_open(I7App *self, const GFile *file);
void i7_app_close_all_documents(I7App *self);

void i7_app_monitor_extensions_directory(I7App *self);
void i7_app_stop_monitoring_extensions_directory(I7App *self);
void i7_app_install_extension(I7App *self, GFile *file);
void i7_app_delete_extension(I7App *self, char *author, char *extname);
gboolean i7_app_download_extension(I7App *self, GFile *file, GCancellable *cancellable, GFileProgressCallback progress_callback, void *progress_callback_data, GError **error);
char *i7_app_get_extension_version(I7App *self, const char *author, const char *title, gboolean *builtin);
void i7_app_foreach_installed_extension(I7App *self, gboolean builtin, I7AppAuthorFunc author_func, void *author_func_data, I7AppExtensionFunc extension_func, void *extension_func_data, GDestroyNotify free_author_result);
void i7_app_run_census(I7App *self, gboolean wait);

GFile *i7_app_get_extension_file(I7App *self, const gchar *author, const char *extname);
GFile *i7_app_get_extension_docpage(I7App *self, const char *author, const char *extname);
GFile *i7_app_get_extension_home_page(I7App *self);
GFile *i7_app_get_extension_index_page(I7App *self);
GFile *i7_app_get_internal_dir(I7App *self);
GFile *i7_app_get_data_file(I7App *self, const char *filename);
GFile *i7_app_get_data_file_va(I7App *self, const char *path1, ...) G_GNUC_NULL_TERMINATED;
GFile *i7_app_get_binary_file(I7App *self, const char *filename);
GFile *i7_app_get_config_dir(I7App *self);

GtkTreeStore *i7_app_get_installed_extensions_tree(I7App *self);
void i7_app_update_extensions_menu(I7App *self);
const char *i7_app_lookup_action_tooltip(I7App *self, const char *action_name, GVariant *target_value);

GtkPrintSettings *i7_app_get_print_settings(I7App *self);
void i7_app_set_print_settings(I7App *self, GtkPrintSettings *settings);
GtkPageSetup *i7_app_get_page_setup(I7App *self);
void i7_app_set_page_setup(I7App *self, GtkPageSetup *setup);

GtkSourceStyleSchemeManager *i7_app_get_color_scheme_manager(I7App *self);

void i7_app_present_prefs_window(I7App *self);
void i7_app_update_css(I7App *self);

GFile *i7_app_get_last_opened_project(I7App *self);

/* Color scheme functions, in app-colorscheme.c */

void i7_app_foreach_color_scheme(I7App *self, GFunc callback, gpointer data);
gboolean i7_app_color_scheme_is_user_scheme(I7App *self, const char *id);
const char *i7_app_install_color_scheme(I7App *self, GFile *file);
gboolean i7_app_uninstall_color_scheme(I7App *self, const char *id);
GtkSourceStyleScheme *i7_app_get_current_color_scheme(I7App *self);

/* GSettings accessors */
GSettings *i7_app_get_state(I7App *self);
GSettings *i7_app_get_prefs(I7App *self);

#endif /* _APP_H_ */
