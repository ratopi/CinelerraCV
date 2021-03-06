
/*
 * CINELERRA
 * Copyright (C) 2008 Adam Williams <broadcast at earthling dot net>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

#include "bcdisplayinfo.h"
#include "clip.h"
#include "bchash.h"
#include "filexml.h"
#include "language.h"
#include "picon_png.h"
#include "swapchannels.h"
#include "vframe.h"



#include <stdint.h>
#include <string.h>






REGISTER_PLUGIN(SwapMain)








SwapConfig::SwapConfig()
{
	red = RED_SRC;
	green = GREEN_SRC;
	blue = BLUE_SRC;
    alpha = ALPHA_SRC;
}


int SwapConfig::equivalent(SwapConfig &that)
{
	return (red == that.red &&
		green == that.green &&
		blue == that.blue &&
		alpha == that.alpha);
}

void SwapConfig::copy_from(SwapConfig &that)
{
	red = that.red;
	green = that.green;
	blue = that.blue;
	alpha = that.alpha;
}








SwapWindow::SwapWindow(SwapMain *plugin, int x, int y)
 : PluginWindow(plugin->gui_string,
	x,
	y,
	250, 
	170)
{
	this->plugin = plugin;
}

SwapWindow::~SwapWindow()
{
}

	
void SwapWindow::create_objects()
{
	int x = 10, y = 10;
	int margin = 30;

	add_subwindow(new BC_Title(x, y, _("Swap channels")));
	y += margin;
	add_subwindow(new BC_Title(x + 160, y + 5, _("-> Red")));
	add_subwindow(red = new SwapMenu(plugin, &(plugin->config.red), x, y));
	red->create_objects();
	y += margin;
	add_subwindow(new BC_Title(x + 160, y + 5, _("-> Green")));
	add_subwindow(green = new SwapMenu(plugin, &(plugin->config.green), x, y));
	green->create_objects();
	y += margin;
	add_subwindow(new BC_Title(x + 160, y + 5, _("-> Blue")));
	add_subwindow(blue = new SwapMenu(plugin, &(plugin->config.blue), x, y));
	blue->create_objects();
	y += margin;
	add_subwindow(new BC_Title(x + 160, y + 5, _("-> Alpha")));
	add_subwindow(alpha = new SwapMenu(plugin, &(plugin->config.alpha), x, y));
	alpha->create_objects();

	show_window();
	flush();
}








SwapMenu::SwapMenu(SwapMain *client, int *output, int x, int y)
 : BC_PopupMenu(x, y, 150, client->output_to_text(*output))
{
	this->client = client;
	this->output = output;
}

int SwapMenu::handle_event()
{
	client->send_configure_change();
	return 1;
}

int SwapMenu::create_objects()
{
	add_item(new SwapItem(this, client->output_to_text(RED_SRC)));
	add_item(new SwapItem(this, client->output_to_text(GREEN_SRC)));
	add_item(new SwapItem(this, client->output_to_text(BLUE_SRC)));
	add_item(new SwapItem(this, client->output_to_text(ALPHA_SRC)));
	add_item(new SwapItem(this, client->output_to_text(NO_SRC)));
	add_item(new SwapItem(this, client->output_to_text(MAX_SRC)));
	return 0;
}




SwapItem::SwapItem(SwapMenu *menu, const char *title)
 : BC_MenuItem(title)
{
	this->menu = menu;
}

int SwapItem::handle_event()
{
	menu->set_text(get_text());
	*(menu->output) = menu->client->text_to_output(get_text());
	menu->handle_event();
	return 1;
}









PLUGIN_THREAD_OBJECT(SwapMain, SwapThread, SwapWindow)










SwapMain::SwapMain(PluginServer *server)
 : PluginVClient(server)
{
	reset();
	PLUGIN_CONSTRUCTOR_MACRO
}

SwapMain::~SwapMain()
{
	PLUGIN_DESTRUCTOR_MACRO
	
	if(temp) delete temp;
}

void SwapMain::reset()
{
	temp = 0;
}


const char* SwapMain::plugin_title()  { return N_("Swap channels"); }
int SwapMain::is_synthesis() { return 1; }
int SwapMain::is_realtime()  { return 1; }


SHOW_GUI_MACRO(SwapMain, SwapThread)
NEW_PICON_MACRO(SwapMain)
SET_STRING_MACRO(SwapMain)
RAISE_WINDOW_MACRO(SwapMain)

int SwapMain::load_defaults()
{
	char directory[1024], string[1024];
// set the default directory
	sprintf(directory, "%sswapchannels.rc", BCASTDIR);

// load the defaults
	defaults = new BC_Hash(directory);
	defaults->load();

	config.red = defaults->get("RED", config.red);
	config.green = defaults->get("GREEN", config.green);
	config.blue = defaults->get("BLUE", config.blue);
	config.alpha = defaults->get("ALPHA", config.alpha);
	return 0;
}

int SwapMain::save_defaults()
{
	defaults->update("RED", config.red);
	defaults->update("GREEN", config.green);
	defaults->update("BLUE", config.blue);
	defaults->update("ALPHA", config.alpha);
	defaults->save();
	return 0;
}

void SwapMain::save_data(KeyFrame *keyframe)
{
	FileXML output;

// cause data to be stored directly in text
	output.set_shared_string(keyframe->data, MESSAGESIZE);
	output.tag.set_title("SWAPCHANNELS");
	output.tag.set_property("RED", config.red);
	output.tag.set_property("GREEN", config.green);
	output.tag.set_property("BLUE", config.blue);
	output.tag.set_property("ALPHA", config.alpha);
	output.append_tag();
	output.tag.set_title("/SWAPCHANNELS");
	output.append_tag();
	output.append_newline();
	output.terminate_string();
// data is now in *text
}

void SwapMain::read_data(KeyFrame *keyframe)
{
	FileXML input;

	input.set_shared_string(keyframe->data, strlen(keyframe->data));

	int result = 0;

	while(!result)
	{
		result = input.read_tag();

		if(!result)
		{
			if(input.tag.title_is("SWAPCHANNELS"))
			{
				config.red = input.tag.get_property("RED", config.red);
				config.green = input.tag.get_property("GREEN", config.green);
				config.blue = input.tag.get_property("BLUE", config.blue);
				config.alpha = input.tag.get_property("ALPHA", config.alpha);
			}
		}
	}
}

void SwapMain::update_gui()
{
	if(thread) 
	{
		load_configuration();
		thread->window->lock_window();
		thread->window->red->set_text(output_to_text(config.red));
		thread->window->green->set_text(output_to_text(config.green));
		thread->window->blue->set_text(output_to_text(config.blue));
		thread->window->alpha->set_text(output_to_text(config.alpha));
		thread->window->unlock_window();
	}
}


int SwapMain::load_configuration()
{
	KeyFrame *prev_keyframe;
	prev_keyframe = get_prev_keyframe(get_source_position());
	
 	read_data(prev_keyframe);
	return 0;
}
























#define MAXMINSRC(src, max) \
	(src == MAX_SRC ? max : 0)

#define SWAP_CHANNELS(type, max, components) \
{ \
	int h = input_ptr->get_h(); \
	int w = input_ptr->get_w(); \
	int red = config.red; \
	int green = config.green; \
	int blue = config.blue; \
	int alpha = config.alpha; \
 \
	if(components == 3) \
	{ \
		if(red == ALPHA_SRC) red = MAX_SRC; \
		if(green == ALPHA_SRC) green = MAX_SRC; \
		if(blue == ALPHA_SRC) blue = MAX_SRC; \
	} \
 \
 \
	for(int i = 0; i < h; i++) \
	{ \
		type *inrow = (type*)input_ptr->get_rows()[i]; \
		type *outrow = (type*)temp->get_rows()[i]; \
 \
		for(int j = 0; j < w; j++) \
		{ \
			if(red < 4) \
				*outrow++ = *(inrow + red); \
			else \
				*outrow++ = MAXMINSRC(red, max); \
 \
			if(green < 4) \
				*outrow++ = *(inrow + green); \
			else \
				*outrow++ = MAXMINSRC(green, max); \
 \
			if(blue < 4) \
				*outrow++ = *(inrow + blue); \
			else \
				*outrow++ = MAXMINSRC(blue, max); \
 \
			if(components == 4) \
			{ \
				if(alpha < 4) \
					*outrow++ = *(inrow + alpha); \
				else \
					*outrow++ = MAXMINSRC(alpha, max); \
			} \
 \
			inrow += components; \
		} \
	} \
 \
 	output_ptr->copy_from(temp); \
}



int SwapMain::process_realtime(VFrame *input_ptr, VFrame *output_ptr)
{
	load_configuration();


	if(!temp) 
		temp = new VFrame(0, 
			input_ptr->get_w(), 
			input_ptr->get_h(), 
			input_ptr->get_color_model());

	switch(input_ptr->get_color_model())
	{
		case BC_RGB_FLOAT:
			SWAP_CHANNELS(float, 1, 3);
			break;
		case BC_RGBA_FLOAT:
			SWAP_CHANNELS(float, 1, 4);
			break;
		case BC_RGB888:
		case BC_YUV888:
			SWAP_CHANNELS(unsigned char, 0xff, 3);
			break;
		case BC_RGBA8888:
		case BC_YUVA8888:
			SWAP_CHANNELS(unsigned char, 0xff, 4);
			break;
		case BC_RGB161616:
		case BC_YUV161616:
			SWAP_CHANNELS(uint16_t, 0xffff, 3);
			break;
		case BC_RGBA16161616:
		case BC_YUVA16161616:
			SWAP_CHANNELS(uint16_t, 0xffff, 4);
			break;
	}
	
	
	return 0;
}


const char* SwapMain::output_to_text(int value)
{
	switch(value)
	{
		case RED_SRC:
			return _("Red");
			break;
		case GREEN_SRC:
			return _("Green");
			break;
		case BLUE_SRC:
			return _("Blue");
			break;
		case ALPHA_SRC:
			return _("Alpha");
			break;
		case NO_SRC:
			return _("0%");
			break;
		case MAX_SRC:
			return _("100%");
			break;
		default:
			return "";
			break;
	}
}

int SwapMain::text_to_output(char *text)
{
	if(!strcmp(text, _("Red"))) return RED_SRC;
	if(!strcmp(text, _("Green"))) return GREEN_SRC;
	if(!strcmp(text, _("Blue"))) return BLUE_SRC;
	if(!strcmp(text, _("Alpha"))) return ALPHA_SRC;
	if(!strcmp(text, _("0%"))) return NO_SRC;
	if(!strcmp(text, _("100%"))) return MAX_SRC;
	return 0;
}


