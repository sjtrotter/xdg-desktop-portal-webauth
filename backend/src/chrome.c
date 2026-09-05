/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "chrome.h"

#include <adwaita.h>

#include "redact.h"

struct WebAuthChrome
{
	GtkWindow* window;
	AdwWindowTitle* title;
	GtkWidget* origin_icon;
	GtkWidget* origin_label;
	GtkWidget* caller_label;
	GtkWidget* hint_label;
	GtkWidget* spinner;
	AdwToolbarView* toolbar;

	WebAuthChromeCancel on_cancel;
	gpointer user_data;
};

/* Application text, rendered as application text: it is put in a label of its
 * own, under a fixed word that says where it came from, and it is escaped
 * because a title carrying markup would otherwise draw whatever it liked. */
static char* application_hint_markup(const char* title_hint)
{
	g_autofree char* escaped = NULL;

	if (title_hint == NULL || *title_hint == '\0')
		return NULL;

	escaped = g_markup_escape_text(title_hint, -1);

	return g_strdup_printf("<small>The application says: %s</small>", escaped);
}

/* WHAT THE FRONTEND ESTABLISHED, said in words rather than in a code. The three
 * kinds are the impl XML's; an empty app id is the fourth case and is the one
 * that has to be loudest. */
static char* caller_markup(const char* app_id, const char* app_id_kind)
{
	g_autofree char* escaped = NULL;

	if (app_id == NULL || *app_id == '\0')
		return g_strdup("<b>An unidentified application</b> asked for this sign-in");

	escaped = g_markup_escape_text(app_id, -1);

	if (g_strcmp0(app_id_kind, "sandboxed") == 0)
		return g_strdup_printf("<b>%s</b> asked for this sign-in (verified)", escaped);

	if (g_strcmp0(app_id_kind, "cgroup") == 0)
		return g_strdup_printf("<b>%s</b> asked for this sign-in (from the running process)",
		                       escaped);

	return g_strdup_printf("<b>%s</b> asked for this sign-in (not verified)", escaped);
}

static void on_cancel_clicked(GtkButton* button, gpointer user_data)
{
	WebAuthChrome* self = user_data;

	if (self->on_cancel != NULL)
		self->on_cancel(self->user_data);
}

static gboolean on_close_request(GtkWindow* window, gpointer user_data)
{
	WebAuthChrome* self = user_data;

	if (self->on_cancel != NULL)
		self->on_cancel(self->user_data);

	/* The transaction destroys the window on its own terms, once it has
	 * answered. */
	return TRUE;
}

static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data)
{
	WebAuthChrome* self = user_data;

	if (keyval != GDK_KEY_Escape)
		return FALSE;

	if (self->on_cancel != NULL)
		self->on_cancel(self->user_data);

	return TRUE;
}

WebAuthChrome* webauth_chrome_new(const char* app_id, const char* app_id_kind,
                                  const char* title_hint, WebAuthChromeCancel on_cancel,
                                  gpointer user_data)
{
	WebAuthChrome* self = g_new0(WebAuthChrome, 1);
	AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());
	GtkWidget* cancel = gtk_button_new_with_mnemonic("_Cancel");
	GtkWidget* banner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget* origin_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkEventController* keys = gtk_event_controller_key_new();
	g_autofree char* caller = caller_markup(app_id, app_id_kind);
	g_autofree char* hint = application_hint_markup(title_hint);

	self->on_cancel = on_cancel;
	self->user_data = user_data;

	self->window = GTK_WINDOW(adw_window_new());
	gtk_window_set_title(self->window, WEBAUTH_CHROME_WINDOW_TITLE);
	gtk_window_set_default_size(self->window, 620, 760);
	gtk_window_set_modal(self->window, TRUE);

	self->title = ADW_WINDOW_TITLE(adw_window_title_new(WEBAUTH_CHROME_WINDOW_TITLE, ""));
	adw_header_bar_set_title_widget(header, GTK_WIDGET(self->title));
	adw_header_bar_set_show_end_title_buttons(header, FALSE);

	g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_clicked), self);
	adw_header_bar_pack_start(header, cancel);

	self->spinner = gtk_spinner_new();
	adw_header_bar_pack_end(header, self->spinner);

	/* The lock is never colour alone: the icon has a text label beside it and
	 * both change together. */
	self->origin_icon = gtk_image_new_from_icon_name("channel-secure-symbolic");
	self->origin_label = gtk_label_new("");
	gtk_label_set_selectable(GTK_LABEL(self->origin_label), FALSE);
	gtk_label_set_ellipsize(GTK_LABEL(self->origin_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_halign(self->origin_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(origin_row), self->origin_icon);
	gtk_box_append(GTK_BOX(origin_row), self->origin_label);

	self->caller_label = gtk_label_new(NULL);
	gtk_label_set_use_markup(GTK_LABEL(self->caller_label), TRUE);
	gtk_label_set_markup(GTK_LABEL(self->caller_label), caller);
	gtk_label_set_wrap(GTK_LABEL(self->caller_label), TRUE);
	gtk_widget_set_halign(self->caller_label, GTK_ALIGN_START);

	gtk_widget_set_margin_top(banner, 8);
	gtk_widget_set_margin_bottom(banner, 8);
	gtk_widget_set_margin_start(banner, 12);
	gtk_widget_set_margin_end(banner, 12);
	gtk_box_append(GTK_BOX(banner), self->caller_label);
	gtk_box_append(GTK_BOX(banner), origin_row);

	if (hint != NULL)
	{
		self->hint_label = gtk_label_new(NULL);
		gtk_label_set_use_markup(GTK_LABEL(self->hint_label), TRUE);
		gtk_label_set_markup(GTK_LABEL(self->hint_label), hint);
		gtk_label_set_wrap(GTK_LABEL(self->hint_label), TRUE);
		gtk_label_set_lines(GTK_LABEL(self->hint_label), 2);
		gtk_label_set_ellipsize(GTK_LABEL(self->hint_label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_halign(self->hint_label, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(banner), self->hint_label);
	}

	self->toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
	adw_toolbar_view_add_top_bar(self->toolbar, GTK_WIDGET(header));
	adw_toolbar_view_add_top_bar(self->toolbar, banner);
	adw_window_set_content(ADW_WINDOW(self->window), GTK_WIDGET(self->toolbar));

	gtk_accessible_update_property(GTK_ACCESSIBLE(self->window), GTK_ACCESSIBLE_PROPERTY_LABEL,
	                               WEBAUTH_CHROME_WINDOW_TITLE, -1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->window),
	                               GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
	                               gtk_label_get_text(GTK_LABEL(self->caller_label)), -1);

	g_signal_connect(self->window, "close-request", G_CALLBACK(on_close_request), self);

	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), self);
	gtk_widget_add_controller(GTK_WIDGET(self->window), keys);

	return self;
}

GtkWindow* webauth_chrome_window(WebAuthChrome* self)
{
	return self->window;
}

void webauth_chrome_set_content(WebAuthChrome* self, GtkWidget* content)
{
	adw_toolbar_view_set_content(self->toolbar, content);
}

void webauth_chrome_set_origin(WebAuthChrome* self, const char* origin, gboolean secure)
{
	g_autofree char* text = NULL;

	if (origin == NULL || *origin == '\0')
	{
		gtk_label_set_text(GTK_LABEL(self->origin_label), "no page loaded");
		adw_window_title_set_subtitle(self->title, "");
		return;
	}

	text = g_strdup_printf("%s %s", secure ? "Secure connection to" : "INSECURE connection to",
	                       origin);

	gtk_label_set_text(GTK_LABEL(self->origin_label), text);
	gtk_image_set_from_icon_name(GTK_IMAGE(self->origin_icon),
	                             secure ? "channel-secure-symbolic" : "channel-insecure-symbolic");
	adw_window_title_set_subtitle(self->title, origin);
	gtk_accessible_update_property(GTK_ACCESSIBLE(self->origin_label),
	                               GTK_ACCESSIBLE_PROPERTY_LABEL, text, -1);
}

void webauth_chrome_set_busy(WebAuthChrome* self, gboolean busy)
{
	gtk_spinner_set_spinning(GTK_SPINNER(self->spinner), busy);
}

void webauth_chrome_free(WebAuthChrome* self)
{
	if (self == NULL)
		return;

	if (self->window != NULL)
	{
		g_signal_handlers_disconnect_by_data(self->window, self);
		gtk_window_destroy(self->window);
		self->window = NULL;
	}

	g_free(self);
}
