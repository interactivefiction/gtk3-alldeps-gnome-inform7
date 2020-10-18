/* Copyright (C) 2006-2009, 2010, 2012, 2014 P. F. Chimento
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

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "app.h"
#include "builder.h"
#include "error.h"
#include "newdialog.h"
#include "story.h"
#include "welcomedialog.h"

void
on_welcome_new_button_clicked(GtkButton *button, I7App *app)
{
	GtkWidget *welcomedialog = gtk_widget_get_toplevel(GTK_WIDGET(button));
	GtkWidget *newdialog = create_new_dialog();
	gtk_widget_destroy(welcomedialog);
	gtk_widget_show(newdialog);
}

void
on_welcome_open_button_clicked(GtkButton *button, I7App *app)
{
	GtkWidget *welcomedialog = gtk_widget_get_toplevel(GTK_WIDGET(button));
	gtk_widget_hide(welcomedialog);

	I7Story *story = i7_story_new_from_dialog(app);

	if(!story) {
		/* Take us back to the welcome dialog */
		gtk_widget_show(welcomedialog);
		return;
	}

	gtk_widget_destroy(welcomedialog);
}

void
on_welcome_reopen_button_clicked(GtkButton *button, I7App *app)
{
	GtkWidget *welcomedialog = gtk_widget_get_toplevel(GTK_WIDGET(button));
	GFile *file = i7_app_get_last_opened_project(app);
	g_assert(file); /* Button not sensitive if no last project */

	I7Story *story = i7_story_new_from_file(app, file);
	g_object_unref(file);

	if (story)
		gtk_widget_destroy(welcomedialog);
}

GtkWidget *
create_welcome_dialog(GtkApplication *theapp)
{
	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource("/com/inform7/IDE/ui/welcomedialog.ui");
	gtk_builder_connect_signals(builder, theapp);
	GtkWidget *retval = GTK_WIDGET(load_object(builder, "welcomedialog"));
	gtk_window_set_application(GTK_WINDOW(retval), theapp);

	/* If there is no "last project", make the reopen button inactive */
	g_autoptr(GFile) last_project = i7_app_get_last_opened_project(I7_APP(theapp));
	if (last_project)
		gtk_widget_set_sensitive(GTK_WIDGET(load_object(builder, "welcome_reopen_button")), TRUE);

	return retval;
}
