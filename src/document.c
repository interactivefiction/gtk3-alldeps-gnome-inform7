/* Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2018 P. F. Chimento
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

#include "config.h"

#include <string.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#include "actions.h"
#include "app.h"
#include "builder.h"
#include "configfile.h"
#include "document.h"
#include "elastic.h"
#include "error.h"
#include "file.h"

typedef struct {
	/* The file this document refers to */
	GFile *file;
	/* Whether it was modified since the last save*/
	gboolean modified;
	/* File monitor */
	GFileMonitor *monitor;
	/* The program code */
	GtkSourceBuffer *buffer;
	GtkTextTag *invisible_tag;
	/* The tree of section headings */
	I7Heading heading_depth;
	GtkTreeStore *headings;
	GtkTreeModel *filter;
	GtkTreePath *current_heading;
	/* The view with a search match currently being highlighted */
	GtkWidget *highlighted_view;

	/* Download counts */
	unsigned downloads_completed;
	unsigned downloads_total;
	unsigned downloads_failed;
	GCancellable *cancel_download;
} I7DocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(I7Document, i7_document, GTK_TYPE_APPLICATION_WINDOW);

/* CALLBACKS */

void
on_buffer_mark_set(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark, I7Document *self)
{
	if (gtk_text_mark_get_name(mark) && strcmp(gtk_text_mark_get_name(mark), "selection-bound")) {
		bool enabled = gtk_text_buffer_get_has_selection(buffer);
		GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "scroll-selection");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
		action = g_action_map_lookup_action(G_ACTION_MAP(self), "comment-out-selection");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
		action = g_action_map_lookup_action(G_ACTION_MAP(self), "uncomment-selection");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
	}
}

void
on_buffer_changed(GtkTextBuffer *buffer, I7Document *document)
{
	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(document), "undo");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), gtk_source_buffer_can_undo(GTK_SOURCE_BUFFER(buffer)));
	action = g_action_map_lookup_action(G_ACTION_MAP(document), "redo");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), gtk_source_buffer_can_redo(GTK_SOURCE_BUFFER(buffer)));
}

void
on_buffer_modified_changed(GtkTextBuffer *buffer, I7Document *document)
{
	if(gtk_text_buffer_get_modified(buffer))
		i7_document_set_modified(document, TRUE);
}

static gboolean
filter_depth(GtkTreeModel *model, GtkTreeIter *iter, I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	gint depth;
	gtk_tree_model_get(model, iter, I7_HEADINGS_DEPTH, &depth, -1);
	return depth <= priv->heading_depth;
}

void
on_findbar_close_clicked(GtkToolButton *button, I7Document *document)
{
	gtk_widget_hide(document->findbar);
	i7_document_unhighlight_quicksearch(document);
}

void
on_multi_download_dialog_response(GtkDialog *dialog, int response, I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	if(response == GTK_RESPONSE_CANCEL)
		g_cancellable_cancel(priv->cancel_download);
}

typedef void (*ActionCallback)(GSimpleAction *, GVariant *, void *);

static void
create_document_actions(I7Document *self)
{
	const GActionEntry actions[] = {
		{ "save", (ActionCallback)action_save },
		{ "save-as", (ActionCallback)action_save_as },
		{ "save-copy", (ActionCallback)action_save_copy },
		{ "revert", (ActionCallback)action_revert },
		{ "page-setup", (ActionCallback)action_page_setup },
		{ "print-preview", (ActionCallback)action_print_preview },
		{ "print", (ActionCallback)action_print },
		{ "close", (ActionCallback)action_close },
		{ "undo", (ActionCallback)action_undo },
		{ "redo", (ActionCallback)action_redo },
		{ "cut", (ActionCallback)action_cut },
		{ "copy", (ActionCallback)action_copy },
		{ "paste", (ActionCallback)action_paste },
		{ "select-all", (ActionCallback)action_select_all },
		{ "find", (ActionCallback)action_find },
		{ "find-next", (ActionCallback)action_find_next },
		{ "find-previous", (ActionCallback)action_find_previous },
		{ "replace", (ActionCallback)action_replace },
		{ "scroll-selection", (ActionCallback)action_scroll_selection },
		{ "search", (ActionCallback)action_search },
		{ "check-spelling", (ActionCallback)action_check_spelling },
		{ "autocheck-spelling", NULL, NULL, "true", (ActionCallback)action_autocheck_spelling_toggle },
		{ "view-toolbar", NULL, NULL, "true", (ActionCallback)action_view_toolbar_toggled },
		{ "view-statusbar", NULL, NULL, "true", (ActionCallback)action_view_statusbar_toggled },
		{ "show-headings", (ActionCallback)action_show_headings },
		{ "current-section-only", (ActionCallback)action_current_section_only },
		{ "increase-restriction", (ActionCallback)action_increase_restriction },
		{ "decrease-restriction", (ActionCallback)action_decrease_restriction },
		{ "entire-source", (ActionCallback)action_entire_source },
		{ "previous-section", (ActionCallback)action_previous_section },
		{ "next-section", (ActionCallback)action_next_section },
		{ "indent", (ActionCallback)action_indent },
		{ "unindent", (ActionCallback)action_unindent },
		{ "comment-out-selection", (ActionCallback)action_comment_out_selection },
		{ "uncomment-selection", (ActionCallback)action_uncomment_selection },
		{ "renumber-all-sections", (ActionCallback)action_renumber_all_sections },
		{ "enable-elastic-tabstops", NULL, NULL, "true", (ActionCallback)action_enable_elastic_tabstops_toggled },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(self), actions, G_N_ELEMENTS(actions), self);
}

/* TYPE SYSTEM */

static void
i7_document_init(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	I7App *theapp = I7_APP(g_application_get_default());
	GSettings *prefs = i7_app_get_prefs(theapp);
	GSettings *state = i7_app_get_state(theapp);

	/* Set the icon */
	gtk_window_set_icon_name(GTK_WINDOW(self), "com.inform7.IDE");

	/* Set the minimum size so that the window can be sized smaller than the
	 widgets inside it */
	gtk_widget_set_size_request(GTK_WIDGET(self), 200, 100);

	/* Build the interface */
	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource("/com/inform7/IDE/ui/document.ui");
	gtk_builder_connect_signals(builder, self);

	/* Create the private properties */
	priv->file = NULL;
	priv->monitor = NULL;
	priv->buffer = GTK_SOURCE_BUFFER(load_object(builder, "buffer"));
	g_object_ref(priv->buffer);
	/* Add invisible tag to buffer */
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(priv->buffer));
	priv->invisible_tag = GTK_TEXT_TAG(load_object(builder, "invisible_tag"));
	gtk_text_tag_table_add(table, priv->invisible_tag);
	/* do not unref table */
	priv->heading_depth = I7_HEADING_PART;
	priv->headings = GTK_TREE_STORE(load_object(builder, "headings_store"));
	g_object_ref(priv->headings);
	priv->filter = GTK_TREE_MODEL(load_object(builder, "headings_filtermodel"));
	g_object_ref(priv->filter);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(priv->filter), (GtkTreeModelFilterVisibleFunc)filter_depth, self, NULL);
	priv->current_heading = gtk_tree_path_new_first();
	priv->highlighted_view = NULL;
	priv->modified = FALSE;

	create_document_actions(self);
	GAction *decrease_restriction = g_action_map_lookup_action(G_ACTION_MAP(self), "decrease-restriction");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(decrease_restriction), FALSE);
	GAction *entire_source = g_action_map_lookup_action(G_ACTION_MAP(self), "entire-source");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(entire_source), FALSE);

	/* Public members */
	LOAD_WIDGET(box);
	LOAD_WIDGET(statusline);
	LOAD_WIDGET(statusbar);
	LOAD_WIDGET(progressbar);
	LOAD_WIDGET(findbar);
	LOAD_WIDGET(findbar_entry);
	LOAD_WIDGET(find_dialog);
	gtk_window_set_transient_for(GTK_WINDOW(self->find_dialog), GTK_WINDOW(self));
	LOAD_WIDGET(search_type);
	LOAD_WIDGET(find_entry);
	LOAD_WIDGET(replace_entry);
	LOAD_WIDGET(ignore_case);
	LOAD_WIDGET(reverse);
	LOAD_WIDGET(restrict_search);
	LOAD_WIDGET(find_button);
	LOAD_WIDGET(replace_button);
	LOAD_WIDGET(replace_all_button);
	LOAD_WIDGET(search_files_dialog);
	gtk_window_set_transient_for(GTK_WINDOW(self->search_files_dialog), GTK_WINDOW(self));
	LOAD_WIDGET(search_files_type);
	LOAD_WIDGET(search_files_entry);
	LOAD_WIDGET(search_files_project);
	LOAD_WIDGET(search_files_extensions);
	LOAD_WIDGET(search_files_documentation);
	LOAD_WIDGET(search_files_ignore_case);
	LOAD_WIDGET(search_files_find);
	LOAD_WIDGET(multi_download_dialog);
	gtk_window_set_transient_for(GTK_WINDOW(self->multi_download_dialog), GTK_WINDOW(self));
	LOAD_WIDGET(download_label);
	LOAD_WIDGET(download_progress);
	gtk_container_add(GTK_CONTAINER(self), self->box);

	/* Bind settings one-way to some properties */
	g_settings_bind(prefs, PREFS_SYNTAX_HIGHLIGHTING,
		priv->buffer, "highlight-syntax",
		G_SETTINGS_BIND_GET | G_SETTINGS_BIND_NO_SENSITIVITY);

	/* Show statusbar if necessary */
	GAction *view_statusbar = g_action_map_lookup_action(G_ACTION_MAP(self), "view-statusbar");
	g_simple_action_set_state(G_SIMPLE_ACTION(view_statusbar), g_settings_get_value(state, PREFS_STATE_SHOW_STATUSBAR));
}

static void
i7_document_finalize(GObject *object)
{
	I7Document *self = I7_DOCUMENT(object);
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	if(priv->file)
		g_object_unref(priv->file);
	if(priv->monitor) {
		g_file_monitor_cancel(priv->monitor);
		g_object_unref(priv->monitor);
	}
	g_object_unref(priv->headings);
	gtk_tree_path_free(priv->current_heading);

	G_OBJECT_CLASS(i7_document_parent_class)->finalize(object);
}

static void
i7_document_class_init(I7DocumentClass *klass)
{
	/* Private pure virtual function */
	klass->extract_title = NULL;
	klass->set_contents_display = NULL;
	/* Public pure virtual functions */
	klass->get_default_view = NULL;
	klass->save = NULL;
	klass->save_as = NULL;
	klass->scroll_to_selection = NULL;
	klass->update_tabs = NULL;
	klass->update_fonts = NULL;
	klass->update_font_sizes = NULL;
	klass->expand_headings_view = NULL;
	klass->highlight_search = NULL;
	klass->set_spellcheck = NULL;
	klass->check_spelling = NULL;
	klass->set_elastic_tabstops = NULL;
	klass->can_revert = NULL;
	klass->revert = NULL;

	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = i7_document_finalize;
}

static void
i7_document_refresh_title(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	char *documentname = i7_document_get_display_name(self);
	if(documentname == NULL)
		return;

	if(priv->modified)
	{
		gchar *title = g_strconcat("*", documentname, NULL);
		gtk_window_set_title(GTK_WINDOW(self), title);
	}
	else
		gtk_window_set_title(GTK_WINDOW(self), documentname);
	g_free(documentname);
}

void
i7_document_set_file(I7Document *self, GFile *file)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	if(priv->file)
		g_object_unref(priv->file);
	priv->file = g_object_ref(file);
	i7_document_refresh_title(self);
}

/**
 * i7_document_get_file:
 * @self: the document
 *
 * Gets the full path to this document.
 *
 * Returns: (transfer full): a #GFile. Unref when done.
 */
GFile *
i7_document_get_file(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return g_object_ref(priv->file);
}

/* Returns a newly-allocated string containing the filename of this document
 without the full path, converted to UTF-8, suitable for display in a window
 titlebar. If this document doesn't have a filename yet, returns NULL. */
gchar *
i7_document_get_display_name(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return priv->file ? file_get_display_name(priv->file) : NULL;
}

GtkSourceBuffer *
i7_document_get_buffer(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return priv->buffer;
}

GtkTextView *
i7_document_get_default_view(I7Document *document)
{
	return I7_DOCUMENT_GET_CLASS(document)->get_default_view(document);
}

/* Write the source to the source buffer & clear the undo history */
void
i7_document_set_source_text(I7Document *self, gchar *text)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkSourceBuffer *buffer = priv->buffer;
	gtk_source_buffer_begin_not_undoable_action(buffer);
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), text, -1);
	gtk_source_buffer_end_not_undoable_action(buffer);
	gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(buffer), FALSE);
}

/* Get text in UTF-8. Allocates a new string */
gchar *
i7_document_get_source_text(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(priv->buffer);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	return gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
}

gboolean
i7_document_get_modified(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return priv->modified;
}

void
i7_document_set_modified(I7Document *self, gboolean modified)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	priv->modified = modified;
	i7_document_refresh_title(self);
}

GtkTreeModel *
i7_document_get_headings(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return priv->filter;
}

/* Convert a path returned from signals on the filter model to a path on the
underlying headings model. Free path when done. */
GtkTreePath *
i7_document_get_child_path(I7Document *self, GtkTreePath *path)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTreePath *real_path = gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(priv->filter), path);
	g_assert(real_path);
	return real_path;
}

typedef struct {
	I7Document *document;
	GFile *file;
} I7DocumentFileMonitorIdleClosure;

static I7DocumentFileMonitorIdleClosure *
new_file_monitor_data(I7Document *document, GFile *file)
{
	I7DocumentFileMonitorIdleClosure *retval = g_new0(I7DocumentFileMonitorIdleClosure, 1);
	retval->document = g_object_ref(document);
	retval->file = g_object_ref(file);
	return retval;
}

static void
free_file_monitor_data(I7DocumentFileMonitorIdleClosure *data)
{
	g_object_unref(data->document);
	g_object_unref(data->file);
	g_free(data);
}

static gboolean
on_document_deleted_or_unmounted_idle(I7Document *document) {
	/* If the file is removed, quietly make sure the user gets a chance
	to save it before exiting */
	i7_document_set_modified(document, TRUE);
	return G_SOURCE_REMOVE;
}

static gboolean
on_document_created_or_changed_idle(I7DocumentFileMonitorIdleClosure *data)
{
	/* g_file_set_contents works by deleting and creating, so both of
	these options mean the source text has been modified. Don't ask for
	confirmation - just read in the new source text. (See mantis #681
	and http://inform7.uservoice.com/forums/57320/suggestions/1220683 )*/
	g_autofree char *text = read_source_file(data->file);
	if (text) {
		i7_document_set_source_text(data->document, text);
		i7_document_flash_status_message(data->document, _("Source code reloaded."), FILE_OPERATIONS);
		i7_document_set_modified(data->document, FALSE);
		return G_SOURCE_REMOVE;
	}
	/* otherwise - that means that our copy of the document
	isn't current with what's on disk anymore, but we weren't able to
	reload it. So treat the situation as if the file had been deleted. */
	return on_document_deleted_or_unmounted_idle(data->document);
}

/* Internal function: callback for when the source text has changed outside of
 * Inform. */
static void
on_document_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, I7Document *document)
{
	switch(event_type) {
		case G_FILE_MONITOR_EVENT_CREATED:
		case G_FILE_MONITOR_EVENT_CHANGED:
		{
			I7DocumentFileMonitorIdleClosure *data = new_file_monitor_data(document, file);
			gdk_threads_add_idle_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)on_document_created_or_changed_idle, data, (GDestroyNotify)free_file_monitor_data);
		}
			break;
		case G_FILE_MONITOR_EVENT_DELETED:
		case G_FILE_MONITOR_EVENT_UNMOUNTED:
			gdk_threads_add_idle((GSourceFunc)on_document_deleted_or_unmounted_idle, document);
			break;
		default:
			;
	}
}

void
i7_document_monitor_file(I7Document *self, GFile *file)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	GError *error = NULL;
	priv->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
	if(!priv->monitor) {
		char *filename = g_file_get_path(file);
		WARN_S(_("Could not start file monitor"), filename, error);
		g_free(filename);
		g_error_free(error);
		return;
	}
	g_signal_connect(priv->monitor, "changed", G_CALLBACK(on_document_changed), self);
}

void
i7_document_stop_file_monitor(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	if(priv->monitor) {
		g_file_monitor_cancel(priv->monitor);
		g_object_unref(priv->monitor);
		priv->monitor = NULL;
	}
}

gboolean
i7_document_save(I7Document *document)
{
	return I7_DOCUMENT_GET_CLASS(document)->save(document);
}

void
i7_document_save_as(I7Document *document, GFile *file)
{
	I7_DOCUMENT_GET_CLASS(document)->save_as(document, file);
}

GFile *
i7_document_run_save_dialog(I7Document *self, GFile *default_file)
{
	return I7_DOCUMENT_GET_CLASS(self)->run_save_dialog(self, default_file);
}

/* Helper function: display a dialog box asking for confirmation of save.
Optionally allow canceling. (If the window is being forced to close, for
example, then the user shouldn't be allowed to cancel.) */
static int
show_save_changes_dialog(I7Document *document, gboolean allow_cancel)
{
	char *filename = i7_document_get_display_name(document);
	GtkWidget *save_changes_dialog;

	if(filename == NULL) {
		save_changes_dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(document), GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
			_("<b><big>Save changes to your story before closing?</big></b>"));
	} else {
		save_changes_dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(document), GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
			_("<b><big>Save changes to '%s' before closing?</big></b>"), filename);
		g_free(filename);
	}

	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(save_changes_dialog),
		_("If you don't save, your changes will be lost."));
	if(allow_cancel)
		gtk_dialog_add_buttons(GTK_DIALOG(save_changes_dialog),
			_("Close _without saving"), GTK_RESPONSE_REJECT,
			_("_Cancel"), GTK_RESPONSE_CANCEL,
			_("_Save"), GTK_RESPONSE_OK,
			NULL);
	else
		gtk_dialog_add_buttons(GTK_DIALOG(save_changes_dialog),
			_("Close _without saving"), GTK_RESPONSE_REJECT,
			_("_Save"), GTK_RESPONSE_OK,
			NULL);
	int result = gtk_dialog_run(GTK_DIALOG(save_changes_dialog));
	gtk_widget_destroy(save_changes_dialog);
	return result;
}

/* If the document is not saved, ask the user whether he/she wants to save it.
Returns TRUE if we can proceed, FALSE if the user cancelled. */
gboolean
i7_document_verify_save(I7Document *document)
{
	if(!i7_document_get_modified(document))
		return TRUE;

	int result = show_save_changes_dialog(document, TRUE /* allow cancel */);
	switch(result) {
		case GTK_RESPONSE_OK: /* save */
			return i7_document_save(document);
		case GTK_RESPONSE_REJECT: /* quit without saving */
			return TRUE;
	}
	return FALSE; /* various ways of cancelling the dialog */
}

void
i7_document_close(I7Document *document)
{
	if(i7_document_get_modified(document)) {
		int result = show_save_changes_dialog(document, FALSE /* can't cancel */);
		if(result == GTK_RESPONSE_OK)
			i7_document_save(document);
	}
	gtk_widget_destroy(GTK_WIDGET(document));
}

void
i7_document_scroll_to_selection(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->scroll_to_selection(document);
}

void
i7_document_jump_to_line(I7Document *self, unsigned lineno)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTextIter start, end;
	/* Line number is counted from 0 internally, so subtract one */
	gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(priv->buffer), &start, lineno - 1);
	end = start;
	gtk_text_iter_forward_to_line_end(&end);
	gtk_text_buffer_select_range(GTK_TEXT_BUFFER(priv->buffer), &start, &end);
	I7_DOCUMENT_GET_CLASS(self)->scroll_to_selection(self);
}

void
i7_document_update_tabs(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->update_tabs(document);
}

void
i7_document_update_fonts(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->update_fonts(document);
}

void
i7_document_update_font_sizes(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->update_font_sizes(document);
}

void
i7_document_update_font_styles(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	g_idle_add((GSourceFunc)update_style, priv->buffer);
}

/* Recalculate the document's elastic tabstops */
void
i7_document_refresh_elastic_tabstops(I7Document *document)
{
	elastic_recalculate_view(i7_document_get_default_view(document));
}

/* Predicate for gtk_text_iter_forward_find_char: return TRUE if the character
 * is not a tab, and keep track of the number of tabs found so far in
 * *pointer_to_num_tabs. */
static gboolean
true_if_non_tab(gunichar ch, unsigned *pointer_to_num_tabs)
{
	if(ch != '\t')
		return TRUE;
	(*pointer_to_num_tabs)++;
	return FALSE;
}

/* Helper function: remove any hanging indent tags from a range of text, by
 * names of tags: from indent-1 to indent-(max) */
static void
remove_indent_tags(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, unsigned max_tabs)
{
	unsigned count;
	for(count = 1; count <= max_tabs; count++) {
		char *tag_name = g_strdup_printf("indent-%u", count);
		gtk_text_buffer_remove_tag_by_name(buffer, tag_name, start, end);
		g_free(tag_name);
	}
}

/*
 * i7_document_update_indent_tags:
 * @self: the document
 * @orig_start: (allow-none): an iter pointing to the start of a range of text
 * to be recalculated, or %NULL for the start of the text
 * @orig_end: (allow-none): an iter pointing to the end of the range, or %NULL
 * for the end of the text
 *
 * Recalculates the hanging indent text tags in a range of text. @orig_start and
 * @orig_end do not have to be at the start and end of a line, respectively.
 */
void
i7_document_update_indent_tags(I7Document *self, GtkTextIter *orig_start, GtkTextIter *orig_end)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(priv->buffer);
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
	GtkTextIter start, end;
	static unsigned max_tab_tag_created = 0;

	if(orig_start != NULL) {
		start = *orig_start;
		gtk_text_iter_set_line_index(&start, 0); /* backward to line start */
	} else {
		gtk_text_buffer_get_start_iter(buffer, &start);
	}
	if(orig_end != NULL) {
		end = *orig_end;
		gtk_text_iter_forward_to_line_end(&end);
	} else {
		gtk_text_buffer_get_end_iter(buffer, &end);
	}

	GSettings *prefs = i7_app_get_prefs(I7_APP(g_application_get_default()));
	if(!g_settings_get_boolean(prefs, PREFS_INDENT_WRAPPED)) {
		remove_indent_tags(buffer, &start, &end, max_tab_tag_created);
		return;
	}
	unsigned spaces = g_settings_get_uint(prefs, PREFS_TAB_WIDTH);
	if(spaces == 0)
		spaces = DEFAULT_TAB_WIDTH;

	while(gtk_text_iter_compare(&start, &end) < 0) {
		GtkTextIter first_non_tab = start, line_end = start;
		unsigned num_tabs = 0;

		gtk_text_iter_forward_to_line_end(&line_end);
		gtk_text_iter_backward_char(&first_non_tab); /* forward_find_char advances before searching, so counteract that */
		gtk_text_iter_forward_find_char(&first_non_tab, (GtkTextCharPredicate)true_if_non_tab, &num_tabs, &line_end);

		if(num_tabs != 0) {
			GtkTextTag *indent_tag;
			char *indent_tag_name = g_strdup_printf("indent-%d", num_tabs);
			indent_tag = gtk_text_tag_table_lookup(table, indent_tag_name);
			if(indent_tag == NULL) {
				/* The background color is for debugging purposes: */
				/* char *background_color = g_strdup_printf("#f%xf",
					15 - num_tabs); */
				indent_tag = gtk_text_buffer_create_tag(buffer, indent_tag_name,
					/* "background", background_color, */
					"indent", -(4 * num_tabs + 2) * spaces,
					NULL);
				/* g_free(background_color); */
				if(max_tab_tag_created < num_tabs)
					max_tab_tag_created = num_tabs;
			}
			g_free(indent_tag_name);

			if(!gtk_text_iter_has_tag(&start, indent_tag)) {
				remove_indent_tags(buffer, &start, &line_end, max_tab_tag_created);
				gtk_text_buffer_apply_tag(buffer, indent_tag, &start, &line_end);
			}
		} else {
			remove_indent_tags(buffer, &start, &line_end, max_tab_tag_created);
		}
		gtk_text_iter_forward_line(&start);
	}
}

gboolean
i7_document_iter_is_invisible(I7Document *self, GtkTextIter *iter)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return gtk_text_iter_has_tag(iter, priv->invisible_tag);
}

void
i7_document_expand_headings_view(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->expand_headings_view(document);
}

void
i7_document_set_headings_filter_level(I7Document *self, int depth)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	priv->heading_depth = depth;
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(priv->filter));
	/* Refiltering doesn't work when moving to a higher depth, so... */
	i7_document_reindex_headings(self);
}

/* Helper function for starts_blank_or_whitespace_line() */
static gboolean
is_non_whitespace(gunichar ch, gpointer data)
{
	return !g_unichar_isspace(ch);
}

/* Returns TRUE if @iter is at the start of a line containing only whitespace or
nothing. @iter must be at the start of a line. Guarantees that @iter is
unchanged. */
static gboolean
starts_blank_or_whitespace_line(GtkTextIter *iter)
{
	if(gtk_text_iter_ends_line(iter))
		return TRUE;

	GtkTextIter iter2 = *iter, end = *iter;
	gtk_text_iter_forward_to_line_end(&end);
	return (g_unichar_isspace(gtk_text_iter_get_char(&iter2))
		&& !gtk_text_iter_forward_find_char(&iter2, is_non_whitespace, NULL, &end));
}

static int
get_heading_from_string(gchar *text)
{
	int retval = -1;

	gchar *lcase = g_utf8_strdown(text, -1);
	if(strcmp(lcase, "volume") == 0)
		retval = I7_HEADING_VOLUME;
	else if(strcmp(lcase, "book") == 0)
		retval = I7_HEADING_BOOK;
	else if(strcmp(lcase, "part") == 0)
		retval = I7_HEADING_PART;
	else if(strcmp(lcase, "chapter") == 0)
		retval = I7_HEADING_CHAPTER;
	else if(strcmp(lcase, "section") == 0)
		retval = I7_HEADING_SECTION;

	g_assert(retval != -1);

	g_free(lcase);
	return retval;
}

/* Re-scan the source code and rebuild the tree model of headings for the
 * contents view */
void
i7_document_reindex_headings(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	I7App *theapp = I7_APP(g_application_get_default());
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(priv->buffer);
	GtkTreeStore *tree = priv->headings;
	gtk_tree_store_clear(tree);
	/* These keep track of where in the tree the last instance of that section type occurred */
	GtkTreeIter title, volume, book, part, chapter, section, current;
	/* These keep track of where to put the next encountered subsection */
	gboolean volume_used = FALSE, book_used = FALSE, part_used = FALSE, chapter_used = FALSE;

	GtkTextIter lastline, thisline, nextline, end;
	gtk_text_buffer_get_start_iter(buffer, &lastline);
	gtk_text_buffer_get_iter_at_line(buffer, &thisline, 1);
	gtk_text_buffer_get_iter_at_line(buffer, &nextline, 2);
	gchar *text = gtk_text_iter_get_text(&lastline, &thisline);
	/* Include \n */
	char *realtitle = I7_DOCUMENT_GET_CLASS(self)->extract_title(self, text);
	g_free(text);

	gtk_tree_store_append(tree, &title, NULL);
	gtk_tree_store_set(tree, &title,
		I7_HEADINGS_TITLE, realtitle,
		I7_HEADINGS_LINE, 1,
		I7_HEADINGS_DEPTH, -1,
		I7_HEADINGS_BOLD, PANGO_WEIGHT_BOLD,
		-1);
	g_free(realtitle);

	while(gtk_text_iter_forward_lines(&lastline, 3)) {
		/* Swap the iterators around using end as a temporary variable */
		end = nextline;
		nextline = lastline;
		lastline = thisline;
		thisline = end;
		gtk_text_iter_forward_to_line_end(&end);

		GMatchInfo *match = NULL;
		text = NULL;
		if(starts_blank_or_whitespace_line(&lastline)
			&& starts_blank_or_whitespace_line(&nextline)
			&& (text = gtk_text_iter_get_text(&thisline, &end))
			&& g_regex_match(theapp->regices[I7_APP_REGEX_HEADINGS], text, 0, &match))
		{
			gchar *level = g_match_info_fetch_named(match, "level");
			int depth = get_heading_from_string(level);
			g_free(level);
			gchar *secnum = g_match_info_fetch_named(match, "secnum");
			gchar *sectitle = g_match_info_fetch_named(match, "sectitle");
			guint lineno = gtk_text_iter_get_line(&thisline) + 1;
			/* Line numbers counted from 0 */

			switch(depth) {
				case I7_HEADING_VOLUME:
					gtk_tree_store_append(tree, &volume, &title);
					current = volume;
					volume_used = TRUE;
					book_used = part_used = chapter_used = FALSE;
					break;
				case I7_HEADING_BOOK:
					gtk_tree_store_append(tree, &book, volume_used? &volume : &title);
					current = book;
					book_used = TRUE;
					part_used = chapter_used = FALSE;
					break;
				case I7_HEADING_PART:
					gtk_tree_store_append(tree, &part, book_used? &book : volume_used? &volume : &title);
					current = part;
					part_used = TRUE;
					chapter_used = FALSE;
					break;
				case I7_HEADING_CHAPTER:
					gtk_tree_store_append(tree, &chapter, part_used? &part : book_used? &book : volume_used? &volume : &title);
					current = chapter;
					chapter_used = TRUE;
					break;
				default: /* section */
					gtk_tree_store_append(tree, &section, chapter_used? &chapter : part_used? &part : book_used? &book : volume_used? &volume : &title);
					current = section;
			}

			gtk_tree_store_set(tree, &current,
				I7_HEADINGS_TITLE, text,
				I7_HEADINGS_LINE, lineno,
				I7_HEADINGS_DEPTH, depth,
				I7_HEADINGS_SECTION_NUMBER, secnum,
				I7_HEADINGS_SECTION_NAME, sectitle,
				I7_HEADINGS_BOLD, PANGO_WEIGHT_NORMAL,
				-1);
			/* Do not free strings (?) */
		}

		if(text)
			g_free(text);
		if(match)
			g_match_info_free(match);
	}
	i7_document_expand_headings_view(self);

	/* Display appropriate messages in the contents view */
	/* If there is at least one child of the root node in the filtered model,
	then the contents can be shown normally. */
	g_assert(gtk_tree_model_get_iter_first(priv->filter, &current));
	if(gtk_tree_model_iter_has_child(priv->filter, &current))
		I7_DOCUMENT_GET_CLASS(self)->set_contents_display(self, I7_CONTENTS_NORMAL);
	else {
		/* If there is no child showing in the filtered model, but there is one
		in the original headings model, then the filtered model is set to too
		shallow a level. */
		g_assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->headings), &current));
		if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(priv->headings), &current))
			I7_DOCUMENT_GET_CLASS(self)->set_contents_display(self, I7_CONTENTS_TOO_SHALLOW);
		else
			/* Otherwise, there simply were no headings recognized. */
			I7_DOCUMENT_GET_CLASS(self)->set_contents_display(self, I7_CONTENTS_NO_HEADINGS);
	}
}

void
i7_document_show_heading(I7Document *self, GtkTreePath *path)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTreeModel *headings = GTK_TREE_MODEL(priv->headings);
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(priv->buffer);

	GtkTreeIter iter;
	g_assert(gtk_tree_model_get_iter(headings, &iter, path));

	guint startline = 0;
	I7Heading depth = I7_HEADING_NONE;
	gtk_tree_model_get(headings, &iter,
		I7_HEADINGS_LINE, &startline,
		I7_HEADINGS_DEPTH, &depth,
		-1);
	startline--;

	/* Remove the invisible tag */
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_remove_tag(buffer, priv->invisible_tag, &start, &end);

	gtk_tree_path_free(priv->current_heading);
	priv->current_heading = path;

	GAction *previous_section = g_action_map_lookup_action(G_ACTION_MAP(self), "previous-section");
	GAction *next_section = g_action_map_lookup_action(G_ACTION_MAP(self), "next-section");
	GAction *decrease_restriction = g_action_map_lookup_action(G_ACTION_MAP(self), "decrease-restriction");
	GAction *entire_source = g_action_map_lookup_action(G_ACTION_MAP(self), "entire-source");

	/* If the user clicked on the title, show the entire source */
	if(depth == I7_HEADING_NONE) {
		/* we have now shown the entire source */
		g_simple_action_set_enabled(G_SIMPLE_ACTION(previous_section), FALSE);
		g_simple_action_set_enabled(G_SIMPLE_ACTION(next_section), FALSE);
		g_simple_action_set_enabled(G_SIMPLE_ACTION(decrease_restriction), FALSE);
		g_simple_action_set_enabled(G_SIMPLE_ACTION(entire_source), FALSE);
		gtk_text_buffer_place_cursor(buffer, &start);
		return;
	}

	g_simple_action_set_enabled(G_SIMPLE_ACTION(previous_section), TRUE);
	g_simple_action_set_enabled(G_SIMPLE_ACTION(decrease_restriction), TRUE);
	g_simple_action_set_enabled(G_SIMPLE_ACTION(entire_source), TRUE);

	GtkTextIter mid;
	gtk_text_buffer_get_iter_at_line(buffer, &mid, startline);
	gtk_text_buffer_apply_tag(buffer, priv->invisible_tag, &start, &mid);
	gtk_text_buffer_place_cursor(buffer, &mid);

	GtkTreeIter next_iter = iter;
	/* if there is a next heading at the current level, display until there */
	if(!gtk_tree_model_iter_next(headings, &next_iter))
		/* if not, then if there is no next heading at the same level, display
		until the next heading one level out */
		if(!(gtk_tree_model_iter_parent(headings, &next_iter, &iter)
			&& gtk_tree_model_iter_next(headings, &next_iter)))
		{
			/* otherwise, there is no next heading, display until the end of the
			source text */
			g_simple_action_set_enabled(G_SIMPLE_ACTION(next_section), FALSE);
			return;
		}

	guint endline = 0;
	gtk_tree_model_get(headings, &next_iter, I7_HEADINGS_LINE, &endline, -1);
	/* the line should be counted from zero, and also we need to back up
	by one line so as not to display the heading */
	endline -= 2;
	gtk_text_buffer_get_iter_at_line(buffer, &mid, endline);
	gtk_text_buffer_apply_tag(buffer, priv->invisible_tag, &mid, &end);

	g_simple_action_set_enabled(G_SIMPLE_ACTION(next_section), TRUE);
}

GtkTreePath *
i7_document_get_previous_heading(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	/* Don't need to use iters here since paths can go up or back */
	GtkTreePath *path = gtk_tree_path_copy(priv->current_heading);
	/* if there is no previous heading on this level, display the previous
	heading one level out */
	if(!gtk_tree_path_prev(path))
		if(gtk_tree_path_get_depth(path) > 1)
			gtk_tree_path_up(path);
	return path;
}

GtkTreePath *
i7_document_get_next_heading(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTreeModel *headings = GTK_TREE_MODEL(priv->headings);
	GtkTreeIter iter;
	gtk_tree_model_get_iter(headings, &iter, priv->current_heading);
	GtkTreeIter next_iter = iter;
	/* if there is a next heading at the current level, display that */
	if(!gtk_tree_model_iter_next(headings, &next_iter))
		/* if not, then if there is no next heading at the same level, display
		the next heading one level out */
		if(!(gtk_tree_model_iter_parent(headings, &next_iter, &iter)
			&& gtk_tree_model_iter_next(headings, &next_iter)))
			/* otherwise, there is no next heading, display until the end of the
			source text */
			next_iter = iter;
	GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(priv->headings), &next_iter);
	return path;
}

GtkTreePath *
i7_document_get_shallower_heading(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	/* Don't need to use iters here */
	GtkTreePath *path = gtk_tree_path_copy(priv->current_heading);
	if(gtk_tree_path_get_depth(path) > 1)
		gtk_tree_path_up(path);
	return path;
}

static guint
get_current_line_number(GtkTextBuffer *buffer)
{
	GtkTextMark *insert_mark = gtk_text_buffer_get_insert(buffer);
	GtkTextIter insert;
	gtk_text_buffer_get_iter_at_mark(buffer, &insert, insert_mark);
	return (guint)(gtk_text_iter_get_line(&insert) + 1);
}

GtkTreePath *
i7_document_get_deeper_heading(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTreeModel *headings = GTK_TREE_MODEL(priv->headings);

	guint cur_line = get_current_line_number(GTK_TEXT_BUFFER(priv->buffer));
	GtkTreeIter iter, next_iter;
	guint line = 0;
	/* This perhaps won't deal quite as well with changes on the fly */
	gtk_tree_model_get_iter(headings, &iter, priv->current_heading);
	if(gtk_tree_model_iter_has_child(headings, &iter)) {
		gtk_tree_model_iter_nth_child(headings, &next_iter, &iter, 0);
		do {
			gtk_tree_model_get(headings, &next_iter, I7_HEADINGS_LINE, &line, -1);
			if(line > cur_line)
				break;
			iter = next_iter;
		} while(gtk_tree_model_iter_next(headings, &next_iter));
	}

	/* iter is the heading we want to go to; if it had no children, it stays
	the same */
	GtkTreePath *path = gtk_tree_model_get_path(headings, &iter);
	return path;
}

GtkTreePath *
i7_document_get_deepest_heading(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTreeModel *headings = GTK_TREE_MODEL(priv->headings);

	guint cur_line = get_current_line_number(GTK_TEXT_BUFFER(priv->buffer));
	GtkTreeIter iter, next_iter;
	guint line = 0;
	/* Could start at current_heading to be more efficient, but starting at the
	root allows us to deal with changes that have occurred since the last
	navigation action in the headings pane */
	gtk_tree_model_get_iter_first(headings, &iter); /* line 0 */
	while(TRUE) {
		if(gtk_tree_model_iter_has_child(headings, &iter))
			gtk_tree_model_iter_nth_child(headings, &next_iter, &iter, 0);
		else {
			next_iter = iter;
			if(!gtk_tree_model_iter_next(headings, &next_iter)
				&& !(gtk_tree_model_iter_parent(headings, &next_iter, &iter)
				&& gtk_tree_model_iter_next(headings, &next_iter)))
				/* We've reached the end */
				break;
		}
		gtk_tree_model_get(headings, &next_iter, I7_HEADINGS_LINE, &line, -1);
		if(line > cur_line)
			break;
		iter = next_iter;
	}

	/* Now iter is the heading we want to go to */
	GtkTreePath *path = gtk_tree_model_get_path(headings, &iter);
	return path;
}

/* Remove the invisible tag */
void
i7_document_show_entire_source(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(priv->buffer);

	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_remove_tag(buffer, priv->invisible_tag, &start, &end);

	GAction *previous_section = g_action_map_lookup_action(G_ACTION_MAP(self), "previous-section");
	GAction *next_section = g_action_map_lookup_action(G_ACTION_MAP(self), "next-section");
	GAction *decrease_restriction = g_action_map_lookup_action(G_ACTION_MAP(self), "decrease-restriction");
	GAction *entire_source = g_action_map_lookup_action(G_ACTION_MAP(self), "entire-source");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(previous_section), FALSE);
	g_simple_action_set_enabled(G_SIMPLE_ACTION(next_section), FALSE);
	g_simple_action_set_enabled(G_SIMPLE_ACTION(decrease_restriction), FALSE);
	g_simple_action_set_enabled(G_SIMPLE_ACTION(entire_source), FALSE);

	gtk_tree_path_free(priv->current_heading);
	priv->current_heading = gtk_tree_path_new_first();
}

/* Displays the message text in the status bar of the current window. */
void
i7_document_display_status_message(I7Document *self, const char *message, const char *context)
{
	GtkStatusbar *status = GTK_STATUSBAR(self->statusbar);
	guint id = gtk_statusbar_get_context_id(status, context);
	gtk_statusbar_pop(status, id);
	gtk_statusbar_push(status, id, message);
}

void
i7_document_remove_status_message(I7Document *self, const char *context)
{
	GtkStatusbar *status = GTK_STATUSBAR(self->statusbar);
	guint id = gtk_statusbar_get_context_id(status, context);
	gtk_statusbar_pop(status, id);
}

struct StatusData {
	GtkStatusbar *status;
	guint context_id;
	guint message_id;
};

static gboolean
end_flash_message(struct StatusData *data)
{
	gtk_statusbar_remove(data->status, data->context_id, data->message_id);
	g_slice_free(struct StatusData, data);
	return FALSE;
}

void
i7_document_flash_status_message(I7Document *document, const gchar *message, const gchar *context)
{
	struct StatusData *data = g_slice_new0(struct StatusData);
	data->status = GTK_STATUSBAR(document->statusbar);
	data->context_id = gtk_statusbar_get_context_id(data->status, context);
	gtk_statusbar_pop(data->status, data->context_id);
	data->message_id = gtk_statusbar_push(data->status, data->context_id, message);
	g_timeout_add_seconds(1, (GSourceFunc)end_flash_message, data);
}

/* Pulses the progress bar */
void
i7_document_display_progress_busy(I7Document *document)
{
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(document->progressbar));
}

/* Displays a percentage in the progress indicator */
void
i7_document_display_progress_percentage(I7Document *document, gdouble fraction)
{
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(document->progressbar), fraction);
}

/* Displays a message in the progress indicator */
void
i7_document_display_progress_message(I7Document *document, const gchar *message)
{
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(document->progressbar), message);
}

/* Clears the message and progress percentage */
void
i7_document_clear_progress(I7Document *document)
{
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(document->progressbar), 0.0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(document->progressbar), NULL);
}

void
i7_document_set_spellcheck(I7Document *document, gboolean spellcheck)
{
	/* Set the default value for the next time a window is opened */
	I7App *theapp = I7_APP(g_application_get_default());
	GSettings *state = i7_app_get_state(theapp);
	g_settings_set_boolean(state, PREFS_STATE_SPELL_CHECK, spellcheck);

	I7_DOCUMENT_GET_CLASS(document)->set_spellcheck(document, spellcheck);
}

void
i7_document_check_spelling(I7Document *document)
{
	I7_DOCUMENT_GET_CLASS(document)->check_spelling(document);
}

void
i7_document_set_elastic_tabstops(I7Document *document, gboolean elastic)
{
	/* Set the default value for the next time a window is opened */
	I7App *theapp = I7_APP(g_application_get_default());
	GSettings *state = i7_app_get_state(theapp);
	g_settings_set_boolean(state, PREFS_STATE_ELASTIC_TABSTOPS, elastic);

	I7_DOCUMENT_GET_CLASS(document)->set_elastic_tabstops(document, elastic);
}

/* Helper function: progress callback for downloading a single extension.
Indicator appears in the progress bar on the bottom left of the document window. */
static void
single_download_progress(goffset current, goffset total, I7Document *self)
{
	if(current == total) {
		i7_document_display_progress_message(self, _("Installing extension"));
		i7_document_display_progress_percentage(self, 1.0);
	} else {
		i7_document_display_progress_message(self, _("Downloading extension"));
		i7_document_display_progress_percentage(self, (double)current / total);
	}
	while(gtk_events_pending())
		gtk_main_iteration();
}

/**
 * i7_document_download_single_extension:
 * @self: the document
 * @remote_file: a #GFile pointing to the extension (real URI, not
 * <code>library:/</code>)
 * @author: display name of the extension's author
 * @title: display name of the extension's title
 *
 * Instructs the view to display downloading a single extension; calls
 * i7_app_download_extension() and takes care of all the GUI work that goes
 * with it.
 *
 * Returns: %TRUE if the download succeeded, %FALSE if not.
 */
gboolean
i7_document_download_single_extension(I7Document *self, GFile *remote_file, const char *author, const char *title)
{
	GError *error = NULL;
	I7App *theapp = I7_APP(g_application_get_default());

	gboolean success = i7_app_download_extension(theapp, remote_file, NULL, (GFileProgressCallback)single_download_progress, self, &error);

	i7_document_clear_progress(self);

	if(!success) {
		error_dialog(GTK_WINDOW(self), error, _("\"%s\" by %s could not be downloaded. The error was: %s"), title, author, error->message);
		return FALSE;
	}

	char *version = i7_app_get_extension_version(theapp, author, title, NULL);
	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(self), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
		_("Installation complete"));
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
		_("\"%s\" by %s (Version %s) has been installed successfully."), title, author, version);
	g_free(version);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return TRUE;
}

/* Helper function: label text for downloads dialog. Free return value when done */
char *
format_download_label_text(unsigned completed, unsigned total, unsigned failed)
{
	if(failed > 0)
		return g_strdup_printf(_("Installed %d of %d (%d failed)"), completed, total, failed);
	return g_strdup_printf(_("Installed %d of %d"), completed, total);
}

/* Helper function: progress callback for downloading more than one extension.
Indicator appears in a dialog box. */
static void
multi_download_progress(goffset current, goffset total, I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	double current_fraction = (double)current / total;
	double total_fraction = ((double)priv->downloads_completed + priv->downloads_failed + current_fraction) / priv->downloads_total;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_progress), total_fraction);

	char *label = format_download_label_text(priv->downloads_completed, priv->downloads_total, priv->downloads_failed);
	gtk_label_set_text(GTK_LABEL(self->download_label), label);
	g_free(label);

	while(gtk_events_pending())
		gtk_main_iteration();
}

/**
 * i7_document_download_multiple_extensions:
 * @self: the document
 * @n_extensions: the number of extensions to download
 * @ids: (array length=n_extensions): string IDs of extensions to download, to
 * be passed to @callback
 * @remote_files: (array length=n_extensions): #GFile references to extensions
 * to download (real URIs)
 * @authors: (array length=n_extensions): author strings of extensions to
 * download
 * @titles: (array length=n_extensions): title strings of extensions to download
 * @versions: (array length=n_extensions): version strings of extensions to
 * download
 * @callback: (scope call): function to be called after each download has
 * succeeded or failed
 * @data: (closure callback): user data for @callback
 *
 * Instructs the view to display downloading multiple extensions in sequence;
 * calls i7_app_download_extension() and takes care of all the GUI work that
 * goes with it.
 *
 * Calls @callback with a boolean value indicating the success of the download
 * and the id from @ids of that particular download, every time a download
 * succeeds or fails.
 *
 * If the operation is cancelled, @callback will not be called for the remaining
 * downloads after that.
 */
void
i7_document_download_multiple_extensions(I7Document *self, unsigned n_extensions, char * const *ids, GFile **remote_files, char * const *authors, char * const *titles, char * const *versions, I7DocumentExtensionDownloadCallback callback, gpointer data)
{
	GError *error = NULL;
	I7App *theapp = I7_APP(g_application_get_default());
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	priv->cancel_download = g_cancellable_new();
	priv->downloads_completed = 0;
	priv->downloads_total = n_extensions;
	priv->downloads_failed = 0;

	char *label = format_download_label_text(0, priv->downloads_total, 0);
	gtk_label_set_text(GTK_LABEL(self->download_label), label);
	g_free(label);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_progress), 0.0);
	gtk_widget_show_all(self->multi_download_dialog);
	while(gtk_events_pending())
		gtk_main_iteration();

	unsigned ix;
	GString *messages = g_string_new("");
	for(ix = 0; ix < n_extensions; ix++) {
		if(g_cancellable_is_cancelled(priv->cancel_download))
			break;

		gboolean success = i7_app_download_extension(theapp, remote_files[ix], priv->cancel_download, (GFileProgressCallback)multi_download_progress, self, &error);
		callback(success, ids[ix], data);

		if(success)
			priv->downloads_completed++;
		else {
			priv->downloads_failed++;
			g_string_append_printf(messages, _("The extension \"%s\" by %s "
				"(%s) could not be downloaded. The server reported: %s.\n\n"),
				titles[ix], authors[ix], versions[ix], error->message);
			g_clear_error(&error);
		}
		multi_download_progress(0, 1, self);
	}
	char *text = g_string_free(messages, FALSE);
	gtk_widget_hide(self->multi_download_dialog);
	g_clear_object(&priv->cancel_download);

	GtkWidget *dialog;
	if(priv->downloads_failed > 0) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(self), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
			ngettext("%u extension installed successfully, %u failed.", "%u extensions installed successfully, %u failed.", priv->downloads_completed),
			priv->downloads_completed, priv->downloads_failed);
		GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_widget_set_size_request(sw, -1, 200);
		GtkWidget *view = gtk_text_view_new();
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
		gtk_text_buffer_set_text(buffer, text, -1);
		gtk_container_add(GTK_CONTAINER(sw), view);
		gtk_box_pack_start(GTK_BOX(area), sw, TRUE, TRUE, 6);
	} else {
		dialog = gtk_message_dialog_new(GTK_WINDOW(self), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			ngettext("%u extension installed successfully.", "%u extensions installed successfully.", priv->downloads_completed),
			priv->downloads_completed);
	}
	g_free(text);
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

/**
 * i7_document_can_revert:
 * @self: the #I7Document
 *
 * Tell whether the document can be reverted to its last saved version; for
 * this, the document must have a last saved version, and there must have been
 * changes since that version.
 *
 * Returns: %TRUE if the document can be reverted, %FALSE otherwise.
 */
gboolean
i7_document_can_revert(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);

	if(priv->file == NULL)
		return FALSE; /* No saved version to revert to */
	if(!g_file_query_exists(priv->file, NULL))
		return FALSE; /* No saved version to revert to */
	if(!priv->modified)
		return FALSE; /* Can't revert if not changed since last save */

	if (I7_DOCUMENT_GET_CLASS(self)->can_revert != NULL)
		return I7_DOCUMENT_GET_CLASS(self)->can_revert(self);

	return TRUE;
}

/**
 * i7_document_revert:
 * @self: the #I7Document
 *
 * Revert to the last saved version of the document, discarding all unsaved
 * changes.
 */
void
i7_document_revert(I7Document *self)
{
	I7_DOCUMENT_GET_CLASS(self)->revert(self);
}

void
i7_document_set_highlighted_view(I7Document *self, GtkWidget *view)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	priv->highlighted_view = view;
}

GtkWidget *
i7_document_get_highlighted_view(I7Document *self)
{
	I7DocumentPrivate *priv = i7_document_get_instance_private(self);
	return priv->highlighted_view;
}
