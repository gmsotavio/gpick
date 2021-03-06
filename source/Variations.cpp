/*
 * Copyright (c) 2009-2016, Albertas Vyšniauskas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of the software author nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Variations.h"
#include "ColorObject.h"
#include "ColorSource.h"
#include "ColorSourceManager.h"
#include "DragDrop.h"
#include "GlobalState.h"
#include "ToolColorNaming.h"
#include "uiUtilities.h"
#include "ColorList.h"
#include "MathUtil.h"
#include "ColorRYB.h"
#include "gtk/ColorWidget.h"
#include "gtk/ColorWheel.h"
#include "ColorWheelType.h"
#include "uiColorInput.h"
#include "CopyPaste.h"
#include "Converter.h"
#include "DynvHelpers.h"
#include "I18N.h"
#include "color_names/ColorNames.h"
#include "StandardMenu.h"
#include "Clipboard.h"
#include <gdk/gdkkeysyms.h>
#include <boost/format.hpp>
#include <math.h>
#include <string.h>
#include <sstream>
#include <iostream>
using namespace std;

#define VAR_COLOR_WIDGETS 8
#define MAX_COLOR_LINES 3

#define COMPONENT_ID_HSL_HUE 1
#define COMPONENT_ID_HSL_SATURATION 2
#define COMPONENT_ID_HSL_LIGHTNESS 3
#define COMPONENT_ID_LAB_LIGHTNESS 4

typedef struct VariationType{
	const char *name;
	const char *symbol;
	const char *unique_name;
	int component_id;
	double strength_mult;
}VariationType;

const VariationType variation_types[] = {
	{N_("Hue"), "H<span font='8' rise='8000'>HSL</span>", "hsl_hue", COMPONENT_ID_HSL_HUE, 1},
	{N_("Saturation"), "S<span font='8' rise='8000'>HSL</span>", "hsl_saturation", COMPONENT_ID_HSL_SATURATION, 1},
	{N_("Lightness"), "L<span font='8' rise='8000'>HSL</span>", "hsl_lightness", COMPONENT_ID_HSL_LIGHTNESS, 1},
	{N_("Lightness (Lab)"), "L<span font='8' rise='8000'>Lab</span>", "lab_lightness", COMPONENT_ID_LAB_LIGHTNESS, 1},
};

typedef struct VariationsArgs{
	ColorSource source;
	GtkWidget* main;
	GtkWidget* statusbar;
	GtkWidget *strength;
	GtkWidget *last_focused_color;
	GtkWidget *color_previews;
	GtkWidget *all_colors;
	struct{
		GtkWidget *color;
		GtkWidget *var_colors[VAR_COLOR_WIDGETS + 1];
		const VariationType *type;
	}color[MAX_COLOR_LINES];
	struct dynvSystem *params;
	ColorList *preview_color_list;
	GlobalState* gs;
}VariationsArgs;

struct VariationsColorNameAssigner: public ToolColorNameAssigner
{
	protected:
		stringstream m_stream;
		const char *m_ident;
	public:
		VariationsColorNameAssigner(GlobalState *gs):
			ToolColorNameAssigner(gs)
		{
		}
		void assign(ColorObject *color_object, const Color *color, const char *ident)
		{
			m_ident = ident;
			ToolColorNameAssigner::assign(color_object, color);
		}
		virtual std::string getToolSpecificName(ColorObject *color_object, const Color *color)
		{
			m_stream.str("");
			m_stream << color_names_get(m_gs->getColorNames(), color, false) << " " << _("variations") << " " << m_ident;
			return m_stream.str();
		}
};

static boost::format format_ignore_arg_errors(const std::string &f_string) {
	boost::format fmter(f_string);
	fmter.exceptions(boost::io::all_error_bits ^ (boost::io::too_many_args_bit | boost::io::too_few_args_bit));
	return fmter;
}


static int set_rgb_color(VariationsArgs *args, ColorObject* color, uint32_t color_index);
static int set_rgb_color_by_widget(VariationsArgs *args, ColorObject* color, GtkWidget* color_widget);

static void calc(VariationsArgs *args, bool preview, bool save_settings){

	double strength = gtk_range_get_value(GTK_RANGE(args->strength));

	if (save_settings){
		dynv_set_float(args->params, "strength", strength);
	}

	Color color, hsl, lab, r, hsl_mod, lab_mod;

	for (int i = 0; i < MAX_COLOR_LINES; ++i){
		gtk_color_get_color(GTK_COLOR(args->color[i].color), &color);

		switch (args->color[i].type->component_id){
		case COMPONENT_ID_HSL_HUE:
		case COMPONENT_ID_HSL_SATURATION:
		case COMPONENT_ID_HSL_LIGHTNESS:
			color_rgb_to_hsl(&color, &hsl);
			break;
		case COMPONENT_ID_LAB_LIGHTNESS:
			{
				color_rgb_to_lab_d50(&color, &lab);
			}
			break;
		}

		for (int j = 0; j < VAR_COLOR_WIDGETS + 1; ++j){
			if (j == VAR_COLOR_WIDGETS / 2) continue;

			switch (args->color[i].type->component_id){
			case COMPONENT_ID_HSL_HUE:
				color_copy(&hsl, &hsl_mod);
				hsl_mod.hsl.hue = wrap_float(hsl.hsl.hue + (args->color[i].type->strength_mult * strength * (j - VAR_COLOR_WIDGETS / 2)) / 400.0);
				color_hsl_to_rgb(&hsl_mod, &r);
				break;
			case COMPONENT_ID_HSL_SATURATION:
				color_copy(&hsl, &hsl_mod);
				hsl_mod.hsl.saturation = clamp_float(hsl.hsl.saturation + (args->color[i].type->strength_mult * strength * (j - VAR_COLOR_WIDGETS / 2)) / 400.0, 0, 1);
				color_hsl_to_rgb(&hsl_mod, &r);
				break;
			case COMPONENT_ID_HSL_LIGHTNESS:
				color_copy(&hsl, &hsl_mod);
				hsl_mod.hsl.lightness = clamp_float(hsl.hsl.lightness + (args->color[i].type->strength_mult * strength * (j - VAR_COLOR_WIDGETS / 2)) / 400.0, 0, 1);
				color_hsl_to_rgb(&hsl_mod, &r);
				break;
			case COMPONENT_ID_LAB_LIGHTNESS:
				color_copy(&lab, &lab_mod);
				lab_mod.lab.L = clamp_float(lab.lab.L + (args->color[i].type->strength_mult * strength * (j - VAR_COLOR_WIDGETS / 2)) / 4.0, 0, 100);
				color_lab_to_rgb_d50(&lab_mod, &r);
				color_rgb_normalize(&r);
				break;
			}

			gtk_color_set_color(GTK_COLOR(args->color[i].var_colors[j]), &r, "");
		}
	}
}

static void update(GtkWidget *widget, VariationsArgs *args ){
	color_list_remove_all(args->preview_color_list);
	calc(args, true, false);
}


static void on_color_paste(GtkWidget *widget, gpointer item) {
	VariationsArgs* args=(VariationsArgs*)item;

	GtkWidget* color_widget = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "color_widget"));

	ColorObject* color_object;
	if (copypaste_get_color_object(&color_object, args->gs) == 0){
		set_rgb_color_by_widget(args, color_object, color_widget);
		color_object->release();
	}
}


static void on_color_edit(GtkWidget *widget, gpointer item) {
	VariationsArgs* args=(VariationsArgs*)item;

	GtkWidget* color_widget = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "color_widget"));

	Color c;
	gtk_color_get_color(GTK_COLOR(color_widget), &c);
	ColorObject* color_object = color_list_new_color_object(args->gs->getColorList(), &c);
	ColorObject* new_color_object = 0;

	if (dialog_color_input_show(GTK_WINDOW(gtk_widget_get_toplevel(args->main)), args->gs, color_object, &new_color_object ) == 0){

		set_rgb_color_by_widget(args, new_color_object, color_widget);

		new_color_object->release();
	}

	color_object->release();
}

static string identify_color_widget(GtkWidget *widget, VariationsArgs *args)
{
	if (args->all_colors == widget){
		return _("all colors");
	}else for (int i = 0; i < MAX_COLOR_LINES; ++i){
		if (args->color[i].color == widget){
			try{
				return (format_ignore_arg_errors(_("primary %d")) % (i + 1)).str();
			}catch(const boost::io::format_error &e){
				return (format_ignore_arg_errors("primary %d") % (i + 1)).str();
			}
		}
		for (int j = 0; j <= VAR_COLOR_WIDGETS; ++j){
			if (args->color[i].var_colors[j] == widget){
				if (j > VAR_COLOR_WIDGETS / 2)
					j--;
				try{
					return (format_ignore_arg_errors(_("result %d line %d")) % (j + 1) % (i + 1)).str();
				}catch(const boost::io::format_error &e){
					return (format_ignore_arg_errors("result %d line %d") % (j + 1) % (i + 1)).str();
				}
			}
		}
	}
	return "unknown";
}

static void add_color_to_palette(GtkWidget *color_widget, VariationsColorNameAssigner &name_assigner, VariationsArgs *args)
{
	Color c;
	ColorObject *color_object;
	string widget_ident;
	gtk_color_get_color(GTK_COLOR(color_widget), &c);
	color_object = color_list_new_color_object(args->gs->getColorList(), &c);
	widget_ident = identify_color_widget(color_widget, args);
	name_assigner.assign(color_object, &c, widget_ident.c_str());
	color_list_add_color_object(args->gs->getColorList(), color_object, 1);
	color_object->release();
}

static void on_color_add_to_palette(GtkWidget *widget, gpointer item) {
	VariationsArgs* args = (VariationsArgs*)item;
	GtkWidget *color_widget = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "color_widget"));
	VariationsColorNameAssigner name_assigner(args->gs);
	add_color_to_palette(color_widget, name_assigner, args);
}

static void on_color_add_all_to_palette(GtkWidget *widget, gpointer item) {
	VariationsArgs* args = (VariationsArgs*)item;
	VariationsColorNameAssigner name_assigner(args->gs);

	for (int i = 0; i < MAX_COLOR_LINES; ++i){
		for (int j = 0; j < VAR_COLOR_WIDGETS + 1; ++j){
			add_color_to_palette(args->color[i].var_colors[j], name_assigner, args);
		}
	}
}

static gboolean color_focus_in_cb(GtkWidget *widget, GdkEventFocus *event, VariationsArgs *args){
	args->last_focused_color = widget;
	return false;
}

static void on_color_activate(GtkWidget *widget, VariationsArgs* args)
{
	Color color;
	gtk_color_get_color(GTK_COLOR(widget), &color);
	ColorObject *color_object = color_list_new_color_object(args->gs->getColorList(), &color);
	string name = color_names_get(args->gs->getColorNames(), &color, dynv_get_bool_wd(args->gs->getSettings(), "gpick.color_names.imprecision_postfix", true));
	color_object->setName(name);
	color_list_add_color_object(args->gs->getColorList(), color_object, 1);
	color_object->release();
}

static void type_toggled_cb(GtkWidget *widget, VariationsArgs *args) {
	GtkWidget* color_widget = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "color_widget"));
	const VariationType *var_type = static_cast<const VariationType*>(g_object_get_data(G_OBJECT(widget), "variation_type"));

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))){

		int line_id = -1;
		for (int i = 0; i < MAX_COLOR_LINES; ++i){
			if (args->color[i].color == color_widget){
				line_id = i;
				break;
			}
		}

		args->color[line_id].type = var_type;

		Color c;
		gtk_color_get_color(GTK_COLOR(color_widget), &c);

		ColorObject *color_object = color_list_new_color_object(args->gs->getColorList(), &c);
		set_rgb_color(args, color_object, line_id);
		color_object->release();

	}
}

static void color_show_menu(GtkWidget* widget, VariationsArgs* args, GdkEventButton *event ){
	GtkWidget *menu;
	GtkWidget* item;

	menu = gtk_menu_new ();

	item = gtk_menu_item_new_with_image(_("_Add to palette"), gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_MENU));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (on_color_add_to_palette), args);
	g_object_set_data(G_OBJECT(item), "color_widget", widget);

	item = gtk_menu_item_new_with_image(_("A_dd all to palette"), gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_MENU));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (on_color_add_all_to_palette), args);
	g_object_set_data(G_OBJECT(item), "color_widget", widget);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	Color c;
	gtk_color_get_color(GTK_COLOR(widget), &c);
	ColorObject *color_object = color_list_new_color_object(args->gs->getColorList(), &c);
	StandardMenu::appendMenu(menu, color_object, args->gs);
	color_object->release();

	int line_id = -1;
	bool all_colors = false;
	if (args->all_colors == widget){
		all_colors = true;
	}else for (int i = 0; i < MAX_COLOR_LINES; ++i){
		if (args->color[i].color == widget){
			line_id = i;
		}
	}

	if (line_id >= 0 || all_colors){
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());

		if (!all_colors){
			GSList *group = 0;
			for (uint32_t i = 0; i < sizeof(variation_types) / sizeof(VariationType); i++){
				item = gtk_radio_menu_item_new_with_label(group, _(variation_types[i].name));
				group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
				if (args->color[line_id].type == &variation_types[i]){
					gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), true);
				}
				g_object_set_data(G_OBJECT(item), "color_widget", widget);
				g_object_set_data(G_OBJECT(item), "variation_type", (void*)&variation_types[i]);
				g_signal_connect(G_OBJECT(item), "toggled", G_CALLBACK(type_toggled_cb), args);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			}
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
		}

		item = gtk_menu_item_new_with_image (_("_Edit..."), gtk_image_new_from_stock(GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (on_color_edit), args);
		g_object_set_data(G_OBJECT(item), "color_widget", widget);

		item = gtk_menu_item_new_with_image (_("_Paste"), gtk_image_new_from_stock(GTK_STOCK_PASTE, GTK_ICON_SIZE_MENU));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (on_color_paste), args);
		g_object_set_data(G_OBJECT(item), "color_widget", widget);

		if (copypaste_is_color_object_available(args->gs) != 0){
			gtk_widget_set_sensitive(item, false);
		}
	}

	gtk_widget_show_all (GTK_WIDGET(menu));

	gint32 button, event_time;
	if (event){
		button = event->button;
		event_time = event->time;
	}else{
		button = 0;
		event_time = gtk_get_current_event_time ();
	}

	gtk_menu_popup (GTK_MENU (menu), nullptr, nullptr, nullptr, nullptr, button, event_time);

	g_object_ref_sink(menu);
	g_object_unref(menu);
}


static gboolean on_color_button_press (GtkWidget *widget, GdkEventButton *event, VariationsArgs* args) {
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS){
		color_show_menu(widget, args, event);
	}
	return false;
}

static void on_color_popup_menu(GtkWidget *widget, VariationsArgs* args){
	color_show_menu(widget, args, 0);
}


static gboolean on_color_key_press (GtkWidget *widget, GdkEventKey *event, VariationsArgs* args){
	guint modifiers = gtk_accelerator_get_default_mod_mask();

	Color c;
	ColorObject* color_object;
	GtkWidget* color_widget = widget;

	switch(event->keyval){
		case GDK_KEY_c:
			if ((event->state & modifiers) == GDK_CONTROL_MASK){
				gtk_color_get_color(GTK_COLOR(color_widget), &c);
				Clipboard::set(c, args->gs);
				return true;
			}
			return false;
			break;

		case GDK_KEY_v:
			if ((event->state&modifiers) == GDK_CONTROL_MASK){
				if (copypaste_get_color_object(&color_object, args->gs) == 0){
					set_rgb_color_by_widget(args, color_object, color_widget);
					color_object->release();
				}
				return true;
			}
			return false;
			break;

		default:
			return false;
		break;
	}
	return false;
}

static int source_destroy(VariationsArgs *args){

	Color c;
	char tmp[32];
	for (gint i = 0; i < MAX_COLOR_LINES; ++i){
		sprintf(tmp, "type%d", i);
		dynv_set_string(args->params, tmp, args->color[i].type->unique_name);

		sprintf(tmp, "color%d", i);
		gtk_color_get_color(GTK_COLOR(args->color[i].color), &c);
		dynv_set_color(args->params, tmp, &c);
	}

	gtk_color_get_color(GTK_COLOR(args->all_colors), &c);
	dynv_set_color(args->params, "all_colors", &c);

	color_list_destroy(args->preview_color_list);
	dynv_system_release(args->params);
	gtk_widget_destroy(args->main);
	delete args;
	return 0;
}

static int source_get_color(VariationsArgs *args, ColorObject** color){
	VariationsColorNameAssigner name_assigner(args->gs);
	Color c;
	string widget_ident;
	if (args->last_focused_color){
		gtk_color_get_color(GTK_COLOR(args->last_focused_color), &c);
		widget_ident = identify_color_widget(args->last_focused_color, args);
	}else{
		gtk_color_get_color(GTK_COLOR(args->color[0].color), &c);
		widget_ident = identify_color_widget(args->color[0].color, args);
	}
	*color = color_list_new_color_object(args->gs->getColorList(), &c);
	name_assigner.assign(*color, &c, widget_ident.c_str());
	return 0;
}

static int set_rgb_color_by_widget(VariationsArgs *args, ColorObject* color_object, GtkWidget* color_widget)
{
	if (args->all_colors){
		set_rgb_color(args, color_object, -1);
		return 0;
	}else for (int i = 0; i < MAX_COLOR_LINES; ++i){
		if (args->color[i].color == color_widget){
			set_rgb_color(args, color_object, i);
			return 0;
		}
	}
	return -1;
}

static int set_rgb_color(VariationsArgs *args, ColorObject* color_object, uint32_t color_index)
{
	Color c = color_object->getColor();
	if (color_index == (uint32_t)-1){
		gtk_color_set_color(GTK_COLOR(args->all_colors), &c, "");
		for (int i = 0; i < MAX_COLOR_LINES; ++i){
			gtk_color_set_color(GTK_COLOR(args->color[i].color), &c, args->color[i].type->symbol);
		}
	}else{
		gtk_color_set_color(GTK_COLOR(args->color[color_index].color), &c, args->color[color_index].type->symbol);
	}
	update(0, args);
	return 0;
}


static int source_set_color(VariationsArgs *args, ColorObject* color_object)
{
	if (args->last_focused_color){
		return set_rgb_color_by_widget(args, color_object, args->last_focused_color);
	}else{
		return set_rgb_color(args, color_object, 0);
	}
}

static int source_activate(VariationsArgs *args)
{
	auto chain = args->gs->getTransformationChain();
	gtk_color_set_transformation_chain(GTK_COLOR(args->all_colors), chain);
	for (int i = 0; i < MAX_COLOR_LINES; ++i){
		gtk_color_set_transformation_chain(GTK_COLOR(args->color[i].color), chain);
		for (int j = 0; j < VAR_COLOR_WIDGETS + 1; ++j){
			gtk_color_set_transformation_chain(GTK_COLOR(args->color[i].var_colors[j]), chain);
		}
	}
	gtk_statusbar_push(GTK_STATUSBAR(args->statusbar), gtk_statusbar_get_context_id(GTK_STATUSBAR(args->statusbar), "empty"), "");
	return 0;
}

static int source_deactivate(VariationsArgs *args){
	color_list_remove_all(args->preview_color_list);
	calc(args, true, true);
	return 0;
}

static ColorObject* get_color_object(struct DragDrop* dd){
	VariationsArgs* args = (VariationsArgs*)dd->userdata;
	ColorObject* color_object;
	if (source_get_color(args, &color_object) == 0){
		return color_object;
	}
	return 0;
}

static int set_color_object_at(struct DragDrop* dd, ColorObject* color_object, int x, int y, bool move){
	VariationsArgs* args = static_cast<VariationsArgs*>(dd->userdata);
	set_rgb_color(args, color_object, (uintptr_t)dd->userdata2);
	return 0;
}

static ColorSource* source_implement(ColorSource *source, GlobalState *gs, struct dynvSystem *dynv_namespace){
	VariationsArgs* args = new VariationsArgs;

	args->params = dynv_system_ref(dynv_namespace);
	args->statusbar = gs->getStatusBar();

	color_source_init(&args->source, source->identificator, source->hr_name);
	args->source.destroy = (int (*)(ColorSource *source))source_destroy;
	args->source.get_color = (int (*)(ColorSource *source, ColorObject** color))source_get_color;
	args->source.set_color = (int (*)(ColorSource *source, ColorObject* color))source_set_color;
	args->source.deactivate = (int (*)(ColorSource *source))source_deactivate;
	args->source.activate = (int (*)(ColorSource *source))source_activate;

	GtkWidget *table, *vbox, *hbox, *widget, *hbox2;
	hbox = gtk_hbox_new(FALSE, 0);
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 5);

	args->color_previews = gtk_table_new(MAX_COLOR_LINES, VAR_COLOR_WIDGETS + 1, false);
	gtk_box_pack_start(GTK_BOX(vbox), args->color_previews, true, true, 0);

	struct DragDrop dd;
	dragdrop_init(&dd, gs);

	dd.userdata = args;
	dd.get_color_object = get_color_object;
	dd.set_color_object_at = set_color_object_at;


	widget = gtk_color_new();
	gtk_color_set_rounded(GTK_COLOR(widget), true);
	gtk_color_set_hcenter(GTK_COLOR(widget), true);
	gtk_color_set_roundness(GTK_COLOR(widget), 5);

	gtk_table_attach(GTK_TABLE(args->color_previews), widget, VAR_COLOR_WIDGETS / 2, VAR_COLOR_WIDGETS / 2 + 1, 0, 1, GtkAttachOptions(GTK_FILL | GTK_EXPAND), GtkAttachOptions(0), 0, 0);

	args->all_colors = widget;

	g_signal_connect(G_OBJECT(widget), "button-press-event", G_CALLBACK(on_color_button_press), args);
	g_signal_connect(G_OBJECT(widget), "activated", G_CALLBACK(on_color_activate), args);
	g_signal_connect(G_OBJECT(widget), "key_press_event", G_CALLBACK(on_color_key_press), args);
	g_signal_connect(G_OBJECT(widget), "popup-menu", G_CALLBACK(on_color_popup_menu), args);
	g_signal_connect(G_OBJECT(widget), "focus-in-event", G_CALLBACK(color_focus_in_cb), args);

	//setup drag&drop
	gtk_widget_set_size_request(widget, 50, 20);

	gtk_drag_dest_set( widget, GtkDestDefaults(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT), 0, 0, GDK_ACTION_COPY);
	gtk_drag_source_set( widget, GDK_BUTTON1_MASK, 0, 0, GDK_ACTION_COPY);
	dd.handler_map = dynv_system_get_handler_map(gs->getColorList()->params);
	dd.userdata2 = (void*)-1;
	dragdrop_widget_attach(widget, DragDropFlags(DRAGDROP_SOURCE | DRAGDROP_DESTINATION), &dd);


	for (intptr_t i = 0; i < MAX_COLOR_LINES; ++i){
		for (intptr_t j = 0; j < VAR_COLOR_WIDGETS + 1; ++j){

			widget = gtk_color_new();
			gtk_color_set_rounded(GTK_COLOR(widget), true);
			gtk_color_set_hcenter(GTK_COLOR(widget), true);
			gtk_color_set_roundness(GTK_COLOR(widget), 5);

			gtk_table_attach(GTK_TABLE(args->color_previews), widget, j, j + 1, i + 1, i + 2, GtkAttachOptions(GTK_FILL | GTK_EXPAND), GtkAttachOptions(0), 0, 0);

			args->color[i].var_colors[j] = widget;
			args->color[i].type = &variation_types[i];

			g_signal_connect(G_OBJECT(widget), "button-press-event", G_CALLBACK(on_color_button_press), args);
			g_signal_connect(G_OBJECT(widget), "activated", G_CALLBACK(on_color_activate), args);
			g_signal_connect(G_OBJECT(widget), "key_press_event", G_CALLBACK(on_color_key_press), args);
			g_signal_connect(G_OBJECT(widget), "popup-menu", G_CALLBACK(on_color_popup_menu), args);
			g_signal_connect(G_OBJECT(widget), "focus-in-event", G_CALLBACK(color_focus_in_cb), args);

			//setup drag&drop
			if (j == VAR_COLOR_WIDGETS / 2){
				gtk_widget_set_size_request(widget, 50, 30);

				gtk_drag_dest_set( widget, GtkDestDefaults(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT), 0, 0, GDK_ACTION_COPY);
				gtk_drag_source_set( widget, GDK_BUTTON1_MASK, 0, 0, GDK_ACTION_COPY);
				dd.handler_map = dynv_system_get_handler_map(gs->getColorList()->params);
				dd.userdata2 = (void*)i;
				dragdrop_widget_attach(widget, DragDropFlags(DRAGDROP_SOURCE | DRAGDROP_DESTINATION), &dd);

				args->color[i].color = args->color[i].var_colors[j];

			}else{
				gtk_widget_set_size_request(widget, 30, 25);

				gtk_drag_source_set( widget, GDK_BUTTON1_MASK, 0, 0, GDK_ACTION_COPY);
				dd.handler_map = dynv_system_get_handler_map(gs->getColorList()->params);
				dd.userdata2 = (void*)i;
				dragdrop_widget_attach(widget, DragDropFlags(DRAGDROP_SOURCE), &dd);
			}
		}
	}

	Color c;
	color_set(&c, 0.5);
	char tmp[32];
	for (gint i = 0; i < MAX_COLOR_LINES; ++i){
		sprintf(tmp, "type%d", i);
		const char *type_name = dynv_get_string_wd(args->params, tmp, "lab_lightness");

		for (uint32_t j = 0; j < sizeof(variation_types) / sizeof(VariationType); j++){
			if (g_strcmp0(variation_types[j].unique_name, type_name) == 0){
				args->color[i].type = &variation_types[j];
				break;
			}
		}
		sprintf(tmp, "color%d", i);
		gtk_color_set_color(GTK_COLOR(args->color[i].color), dynv_get_color_wdc(args->params, tmp, &c), args->color[i].type->symbol);
	}
	gtk_color_set_color(GTK_COLOR(args->all_colors), dynv_get_color_wdc(args->params, "all_colors", &c), "");

	hbox2 = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, false, false, 0);

	gint table_y;
	table = gtk_table_new(5, 2, false);
	gtk_box_pack_start(GTK_BOX(hbox2), table, true, true, 0);
	table_y = 0;

	gtk_table_attach(GTK_TABLE(table), gtk_label_aligned_new(_("Strength:"),0,0.5,0,0),0,1,table_y,table_y+1,GtkAttachOptions(GTK_FILL),GTK_FILL,0,0);
	args->strength = gtk_hscale_new_with_range(1, 100, 1);
	gtk_range_set_value(GTK_RANGE(args->strength), dynv_get_float_wd(args->params, "strength", 30));
	g_signal_connect(G_OBJECT(args->strength), "value-changed", G_CALLBACK (update), args);
	gtk_table_attach(GTK_TABLE(table), args->strength,1,2,table_y,table_y+1,GtkAttachOptions(GTK_FILL | GTK_EXPAND),GTK_FILL,0,0);
	table_y++;

	struct dynvHandlerMap* handler_map=dynv_system_get_handler_map(gs->getColorList()->params);
	ColorList* preview_color_list = color_list_new(handler_map);
	dynv_handler_map_release(handler_map);

	args->preview_color_list = preview_color_list;
	args->gs = gs;
	gtk_widget_show_all(hbox);
	update(0, args);
	args->main = hbox;
	args->source.widget = hbox;
	return (ColorSource*)args;
}

int variations_source_register(ColorSourceManager *csm)
{
	ColorSource *color_source = new ColorSource;
	color_source_init(color_source, "variations", _("Variations"));
	color_source->implement = (ColorSource* (*)(ColorSource *source, GlobalState *gs, struct dynvSystem *dynv_namespace))source_implement;
	color_source->default_accelerator = GDK_KEY_v;
	color_source_manager_add_source(csm, color_source);
	return 0;
}
