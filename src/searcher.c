#include "state.h"
#include <gtk/gtk.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>
#include <gio/gio.h>
#include <gdk/gdkkeysyms.h>

static void launch_child(AppState *st, GtkWidget *child) {
	if (!child) return;
	GtkWidget *vbox = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child));
	GAppInfo *info = g_object_get_data(G_OBJECT(vbox), "app-info");

	if (info) {
		g_app_info_launch(info, NULL, NULL, NULL);
		gtk_widget_set_visible(st->search_box, FALSE);
	}
}

static GtkWidget* get_first_visible_child(GtkFlowBox *flow) {
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(flow));
    while (child) {
        if (gtk_widget_get_visible(child) && gtk_widget_is_sensitive(child)) {
            return child;
        }
        child = gtk_widget_get_next_sibling(child);
    }
    return NULL;
}

static GtkWidget* get_next_visible_child(GtkFlowBox *flow, GtkWidget *current) {
    GtkWidget *child = gtk_widget_get_next_sibling(current);
    while (child) {
        if (gtk_widget_get_visible(child) && gtk_widget_is_sensitive(child)) {
            return child;
        }
        child = gtk_widget_get_next_sibling(child);
    }
    // If we reached the end, wrap around to start
    return get_first_visible_child(flow);
}

static void on_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer user_data) {
    AppState *st = (AppState *)user_data;
		launch_child(st, GTK_WIDGET(child));
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                               guint keyval,
                               guint keycode,
                               GdkModifierType state,
                               gpointer user_data) {
    AppState *st = (AppState *)user_data;
    GtkWindow *win = GTK_WINDOW(st->search_box);
    GtkWidget *focus = gtk_window_get_focus(win);

    // 1. ESCAPE: Close window
    if (keyval == GDK_KEY_Escape) {
        gtk_widget_set_visible(st->search_box, FALSE);
        return TRUE;
    }

    // 2. ENTER: Launch app
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        // If search bar is focused, launch the FIRST visible app
        if (focus == st->search_entry) {
            GtkWidget *first = get_first_visible_child(GTK_FLOW_BOX(st->search_flowbox));
            if (first) {
                launch_child(st, first);
                return TRUE;
            }
        }
        // If an app is focused, the default 'activate' signal usually handles it,
        // but we can enforce it here if we want.
        // (Leaving it to return FALSE lets GTK activate the focused widget)
        return FALSE; 
    }

    // 3. TAB: Cycle focus
    if (keyval == GDK_KEY_Tab) {
        // If search bar is focused -> Move to FIRST visible app
        if (focus == st->search_entry) {
            GtkWidget *first = get_first_visible_child(GTK_FLOW_BOX(st->search_flowbox));
            if (first) gtk_widget_grab_focus(first);
            return TRUE; // Swallow event so we don't insert a tab character
        }
        
        // If an app is focused -> Move to NEXT visible app
        if (focus && gtk_widget_is_ancestor(focus, st->search_flowbox)) {
            // 'focus' might be the internal button/vbox, getting the FlowBoxChild ancestor is safer
            GtkWidget *child = gtk_widget_get_ancestor(focus, GTK_TYPE_FLOW_BOX_CHILD);
            if (child) {
                GtkWidget *next = get_next_visible_child(GTK_FLOW_BOX(st->search_flowbox), child);
                if (next) gtk_widget_grab_focus(next);
                return TRUE;
            }
        }
    }

    // 4. Arrow Keys: Let GTK handle standard grid navigation if focus is in FlowBox
    // We return FALSE to let the event propagate.
    return FALSE;
}

static gboolean search_filter_func(GtkFlowBoxChild *child, gpointer user_data) {
    GtkSearchEntry *entry = GTK_SEARCH_ENTRY(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    
    if (!text || !*text) return TRUE; 

    GtkWidget *btn = gtk_flow_box_child_get_child(child);
    GAppInfo *info = g_object_get_data(G_OBJECT(btn), "app-info");
    if (!info) return FALSE;

    const char *name = g_app_info_get_name(info);
    const char *id = g_app_info_get_id(info);
    
    char *lower_text = g_ascii_strdown(text, -1);
    char *lower_name = name ? g_ascii_strdown(name, -1) : NULL;
    char *lower_id = id ? g_ascii_strdown(id, -1) : NULL;

    gboolean match = (lower_name && strstr(lower_name, lower_text)) ||
                     (lower_id && strstr(lower_id, lower_text));

    g_free(lower_text);
    g_free(lower_name);
    g_free(lower_id);
    return match;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    GtkFlowBox *box = GTK_FLOW_BOX(user_data);
    gtk_flow_box_invalidate_filter(box);
}

static void searcher_refresh_apps(AppState *st) {
	if (!st || !st->search_flowbox) return;

	GtkWidget *child = gtk_widget_get_first_child(st->search_flowbox);
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);
		gtk_flow_box_remove(GTK_FLOW_BOX(st->search_flowbox), child);
		child = next;
	}

	GList *apps = g_app_info_get_all();
	for (GList *l = apps; l != NULL; l = l->next) {
		GAppInfo *info = (GAppInfo*)l->data;
		if (!g_app_info_should_show(info)) continue;

		GtkWidget *child = gtk_flow_box_child_new();
		gtk_widget_add_css_class(child, "app-btn");
		gtk_widget_set_focusable(child, TRUE);

		GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(vbox, GTK_ALIGN_END);
		GIcon *icon = g_app_info_get_icon(info);

		GtkWidget *img = gtk_image_new_from_gicon(icon);
		gtk_image_set_pixel_size(GTK_IMAGE(img), st->cfg->searcher_icon_size);

		GtkWidget *lbl = gtk_label_new(g_app_info_get_name(info));
		gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(lbl), 12);

		gtk_box_append(GTK_BOX(vbox), img);
		gtk_box_append(GTK_BOX(vbox), lbl);
		// gtk_button_set_child(GTK_BUTTON(btn), vbox);

		gtk_flow_box_child_set_child(GTK_FLOW_BOX_CHILD(child), vbox);
		g_object_set_data_full(G_OBJECT(vbox), "app-info", g_object_ref(info), g_object_unref);
		// g_signal_connect(btn, "clicked", G_CALLBACK(on_search_app_clicked), st);
		gtk_flow_box_append(GTK_FLOW_BOX(st->search_flowbox), child);
	}
	g_list_free_full(apps, g_object_unref);

}

void searcher_init(AppState *st) {
    GtkWidget *win = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_widget_add_css_class(win, "search-window");

    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), st);
    gtk_widget_add_controller(win, key_controller);

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
		gtk_widget_add_css_class(box, "search-container");
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(box, 680, 500);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *entry = gtk_search_entry_new();
    gtk_box_append(GTK_BOX(box), entry);

		st->search_entry = entry;

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget *flow = gtk_flow_box_new();
		gtk_widget_add_css_class(flow, "app-container");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 5);
		gtk_widget_set_halign(flow, GTK_ALIGN_START);
		gtk_widget_set_valign(flow, GTK_ALIGN_START);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), flow);
		gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flow), TRUE);
    g_signal_connect(flow, "child-activated", G_CALLBACK(on_child_activated), st);

		st->search_flowbox = flow;
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), flow);

		searcher_refresh_apps(st);

    gtk_flow_box_set_filter_func(GTK_FLOW_BOX(flow), search_filter_func, entry, NULL);
    g_signal_connect(entry, "search-changed", G_CALLBACK(on_search_changed), flow);

    st->search_box = win;
    gtk_widget_set_visible(win, FALSE);
}

void searcher_toggle(AppState *st) {
    if (!st || !st->search_box) return;
    
    gboolean visible = gtk_widget_get_visible(st->search_box);
    
    if (visible) {
        gtk_widget_set_visible(st->search_box, FALSE);
    } else {
				searcher_refresh_apps(st);

				if (st->search_entry) {
					gtk_editable_set_text(GTK_EDITABLE(st->search_entry), "");
				}

        gtk_widget_set_visible(st->search_box, TRUE);
        gtk_window_present(GTK_WINDOW(st->search_box));

				if (st->search_entry) {
					gtk_widget_grab_focus(st->search_entry);
				}
    }
}
