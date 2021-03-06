
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

#include "asset.h"
#include "assetedit.h"
#include "awindow.h"
#include "awindowgui.h"
#include "bcprogressbox.h"
#include "bcsignals.h"
#include "cinelerra.h"
#include "cache.h"
#include "clip.h"
#include "cplayback.h"
#include "cwindow.h"
#include "file.h"
#include "filempeg.h"
#include "filesystem.h"
#include "formattools.h"
#include "indexfile.h"
#include "language.h"
#include "mainerror.h"
#include "mainindexes.h"
#include "mwindow.h"
#include "mwindowgui.h"
#include "theme.h"
#include "new.h"
#include "preferences.h"
#include "selection.h"
#include "transportque.h"
#include "interlacemodes.h"
#include "edl.h"
#include "edlsession.h"

#include <inttypes.h>
#include <string.h>



AssetEdit::AssetEdit(MWindow *mwindow)
 : Thread()
{
	this->mwindow = mwindow;
	asset = 0;
	window = 0;
	set_synchronous(0);
}


AssetEdit::~AssetEdit()
{
}


void AssetEdit::edit_asset(Asset *asset)
{
	if(asset)
	{
// Allow more than one window
		this->asset = asset;
		Thread::start();
	}
}


int AssetEdit::set_asset(Asset *asset)
{
	this->asset = asset;
	return 0;
}

void AssetEdit::run()
{
	if(asset)
	{
		new_asset = new Asset(asset->path);
		*new_asset = *asset;
		int result = 0;

		window = new AssetEditWindow(mwindow, this);
		window->create_objects();
		window->raise_window();
		result = window->run_window();

 		if(!result)
 		{
 			if(!asset->equivalent(*new_asset, 1, 1))
 			{
				if(new_asset->audio_data && SampleRateSelection::limits(&new_asset->sample_rate) < 0)
					errorbox(_("Sample rate is out of limits (%d..%d).\nCorrection applied."),
						MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
				if(new_asset->video_data && FrameRateSelection::limits(&new_asset->frame_rate) < 0)
					errorbox(_("Frame rate is out of limits (%d..%d).\nCorrection applied."),
						MIN_FRAME_RATE, MAX_FRAME_RATE);
				mwindow->gui->lock_window();
				mwindow->remove_asset_from_caches(asset);
// Omit index status from copy since an index rebuild may have been
// happening when new_asset was created but not be happening anymore.
				asset->copy_from(new_asset, 0);

				mwindow->gui->update(0,
					2,
					0,
					0,
					0, 
					0,
					0);

// Start index rebuilding
				if(asset->audio_data)
				{
					char source_filename[BCTEXTLEN];
					char index_filename[BCTEXTLEN];
					IndexFile::get_index_filename(source_filename, 
						mwindow->preferences->index_directory,
						index_filename, 
						asset->path);
					remove(index_filename);
					asset->index_status = INDEX_NOTTESTED;
					mwindow->mainindexes->add_next_asset(0, asset);
					mwindow->mainindexes->start_build();
				}
				mwindow->gui->unlock_window();


				mwindow->awindow->gui->async_update_assets();

				mwindow->restart_brender();
				mwindow->sync_parameters(CHANGE_ALL);
 			}
 		}

		Garbage::delete_object(new_asset);
		delete window;
		window = 0;
	}
}








AssetEditWindow::AssetEditWindow(MWindow *mwindow, AssetEdit *asset_edit)
 : BC_Window(MWindow::create_title(N_("Asset Info")),
 	mwindow->gui->get_abs_cursor_x(1) - 400 / 2, 
	mwindow->gui->get_abs_cursor_y(1) - 550 / 2, 
	410,
	660,
	400,
	560,
	0,
	0,
	1)
{
	this->mwindow = mwindow;
	this->asset_edit = asset_edit;
	this->asset = asset_edit->new_asset;
	if(asset->format == FILE_PCM)
		allow_edits = 1;
	else
		allow_edits = 0;
}

int AssetEditWindow::create_objects()
{
	int y = 10, x = 10, x1 = 10, x2 = 170;
	char string[BCTEXTLEN];
	int vmargin;
	int hmargin1 = 180, hmargin2 = 290;
	FileSystem fs;
	BC_Title *title;
	BC_TextBox  *textboxw;
	BC_CheckBox *chkboxw;
	BC_ListBox  *listboxw;
	Interlaceautofix *ilacefixoption_chkboxw;

	if(allow_edits) 
		vmargin = 30;
	else
		vmargin = 20;

	add_subwindow(path_text = new AssetEditPathText(this, y));
	add_subwindow(path_button = new AssetEditPath(mwindow, 
		this, 
		path_text, 
		y, 
		asset->path, 
		MWindow::create_title(N_("Asset path")), _("Select a file for this asset:")));
	y += 30;

	add_subwindow(new BC_Title(x, y, _("File format:")));
	x = x2;
	add_subwindow(new BC_Title(x, y,
		ContainerSelection::container_to_text(asset->format),
		MEDIUMFONT, mwindow->theme->edit_font_color));
	x = x1;
	y += 20;

	int64_t bytes = 1;
	int subtitle_tracks = 0;
	if(asset->format == FILE_MPEG &&
		asset->video_data)
	{
// Get length from TOC
		FileMPEG::get_info(asset, &bytes, &subtitle_tracks);
	}
	else
	{
		bytes = fs.get_size(asset->path);
	}
	add_subwindow(new BC_Title(x, y, _("Bytes:")));
	sprintf(string, "%" PRId64, bytes);
	Units::punctuate(string);
	

	add_subwindow(new BC_Title(x2, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
	y += 20;
	x = x1;

	double length;
	if(asset->audio_length > 0)
		length = (double)asset->audio_length / asset->sample_rate;
	if(asset->video_length > 0)
		length = MAX(length, (double)asset->video_length / asset->frame_rate);
	int64_t bitrate;
	if(!EQUIV(length, 0))
		bitrate = (int64_t)(bytes * 8 / length);
	else
		bitrate = bytes;
	add_subwindow(new BC_Title(x, y, _("Bitrate (bits/sec):")));
	sprintf(string, "%" PRId64, bitrate);

	Units::punctuate(string);
	add_subwindow(new BC_Title(x2, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));

	y += 30;
	x = x1;

	if(asset->audio_data)
	{
		add_subwindow(new BC_Bar(x, y, get_w() - x * 2));
		y += 5;

		add_subwindow(new BC_Title(x, y, _("Audio:"), LARGEFONT, RED));

		y += 30;

		if(asset->get_compression_text(1, 0))
		{
			add_subwindow(new BC_Title(x, y, _("Compression:")));
			x = x2;
			add_subwindow(new BC_Title(x, 
				y, 
				asset->get_compression_text(1, 0), 
				MEDIUMFONT, 
				mwindow->theme->edit_font_color));
			y += vmargin;
			x = x1;
		}

		add_subwindow(new BC_Title(x, y, _("Channels:")));
		sprintf(string, "%d", asset->channels);

		x = x2;
		if(allow_edits)
		{
			BC_TumbleTextBox *textbox = new AssetEditChannels(this, string, x, y);
			textbox->create_objects();
			y += vmargin;
		}
		else
		{
			add_subwindow(new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
			y += 20;
		}

		x = x1;
		add_subwindow(new BC_Title(x, y, _("Sample rate:")));
		sprintf(string, "%d", asset->sample_rate);

		x = x2;
//		if(allow_edits)
		if(1)
		{
			BC_TextBox *textbox;
			add_subwindow(textbox = new SampleRateSelection(x, y, this,
				&asset->sample_rate));
			textbox->update(asset->sample_rate);
		}
		else
		{
			add_subwindow(new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
		}

		y += 30;
		x = x1;

		add_subwindow(new BC_Title(x, y, _("Bits:")));
		x = x2;
		if(allow_edits)
		{
			SampleBitsSelection *bitspopup;
			add_subwindow(bitspopup = new SampleBitsSelection(x, y, this,
				&asset->bits,
				SBITS_LINEAR| SBITS_ULAW | SBITS_ADPCM | SBITS_IMA4));
			bitspopup->update_size(asset->bits);
		}
		else
			add_subwindow(new BC_Title(x, y,
				SampleBitsSelection::name(asset->bits),
				MEDIUMFONT, mwindow->theme->edit_font_color));
		x = x1;
		y += vmargin;
		add_subwindow(new BC_Title(x, y, _("Header length:")));
		sprintf(string, "%d", asset->header);

		x = x2;
		if(allow_edits)
			add_subwindow(new AssetEditHeader(this, string, x, y));
		else
			add_subwindow(new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));

		y += vmargin;
		x = x1;

		add_subwindow(new BC_Title(x, y, _("Byte order:")));

		if(allow_edits)
		{
			x = x2;

			add_subwindow(lohi = new AssetEditByteOrderLOHI(this, 
				asset->byte_order, 
				x, 
				y));
			x += 70;
			add_subwindow(hilo = new AssetEditByteOrderHILO(this, 
				!asset->byte_order, 
				x, 
				y));
			y += vmargin;
		}
		else
		{
			x = x2;
			if(asset->byte_order)
				add_subwindow(new BC_Title(x, y, _("Lo-Hi"), MEDIUMFONT, mwindow->theme->edit_font_color));
			else
				add_subwindow(new BC_Title(x, y, _("Hi-Lo"), MEDIUMFONT, mwindow->theme->edit_font_color));
			y += vmargin;
		}


		x = x1;
		if(allow_edits)
		{
//			add_subwindow(new BC_Title(x, y, _("Values are signed:")));
			add_subwindow(new AssetEditSigned(this, asset->signed_, x, y));
		}
		else
		{
			if(!asset->signed_ && asset->bits == 8)
				add_subwindow(new BC_Title(x, y, _("Values are unsigned")));
			else
				add_subwindow(new BC_Title(x, y, _("Values are signed")));
		}

		y += 30;
	}

	x = x1;
	if(asset->video_data)
	{
		add_subwindow(new BC_Bar(x, y, get_w() - x * 2));
		y += 5;

		add_subwindow(new BC_Title(x, y, _("Video:"), LARGEFONT, RED));


		y += 30;
		x = x1;
		if(asset->get_compression_text(0,1))
		{
			add_subwindow(new BC_Title(x, y, _("Compression:")));
			x = x2;
			add_subwindow(new BC_Title(x, 
				y, 
				asset->get_compression_text(0,1), 
				MEDIUMFONT, 
				mwindow->theme->edit_font_color));
			y += vmargin;
			x = x1;
		}

		add_subwindow(new BC_Title(x, y, _("Frame rate:")));
		x = x2;
		sprintf(string, "%.2f", asset->frame_rate);
		BC_TextBox *framerate;

		add_subwindow(framerate = new FrameRateSelection(x, y, this,
			&asset->frame_rate));
		framerate->update(asset->frame_rate);

		y += 30;
		x = x1;
		add_subwindow(new BC_Title(x, y, _("Width:")));
		x = x2;
		sprintf(string, "%d", asset->width);
		add_subwindow(new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
		
		y += vmargin;
		x = x1;
		add_subwindow(new BC_Title(x, y, _("Height:")));
		x = x2;
		sprintf(string, "%d", asset->height);
		add_subwindow(title = new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
		y += title->get_h() + 5;

		if(asset->format == FILE_MPEG)
		{
			x = x1;
			add_subwindow(new BC_Title(x, y, _("Subtitle tracks:")));
			x = x2;
			sprintf(string, "%d", subtitle_tracks);
			add_subwindow(title = new BC_Title(x, y, string, MEDIUMFONT, mwindow->theme->edit_font_color));
			y += title->get_h() + 5;
		}

		// --------------------
		add_subwindow(title = new BC_Title(x1, y, _("Fix interlacing:")));
		add_subwindow(ilacefixoption_chkboxw = new Interlaceautofix(mwindow,this, x2, y));
		y += ilacefixoption_chkboxw->get_h() + 5;

		// --------------------
		add_subwindow(title = new BC_Title(x1, y, _("Asset's interlacing:")));
		add_subwindow(ilacemode_selection = new AssetInterlaceMode(x2, y,
			this, &asset->interlace_mode));
		ilacemode_selection->update(asset->interlace_mode);
		ilacemode_selection->autofix = ilacefixoption_chkboxw;
		y += ilacemode_selection->get_h() + 5;

		// --------------------
		add_subwindow(title = new BC_Title(x1, y, _("Interlace correction:")));
		add_subwindow(ilacefix_selection = new InterlaceFixSelection(x2, y,
			this, &asset->interlace_fixmethod));
		ilacefixoption_chkboxw->showhideotherwidgets();
		y += ilacefix_selection->get_h() + 5;

		x = x1;
		add_subwindow(new BC_Title(x, y, _("Reel Name:")));
		x = x2;
		add_subwindow(new AssetEditReelName(this, x, y));
		y += 30;
		
		x = x1;
		add_subwindow(new BC_Title(x, y, _("Reel Number:")));
		x = x2;
		add_subwindow(new AssetEditReelNumber(this, x, y));
		y += 30;
		
		x = x1;
		add_subwindow(new BC_Title(x, y, _("Time Code Start:")));
		x = x2;

// Calculate values to enter into textboxes
		char tc[12];
		
		Units::totext(tc,
			asset->tcstart / asset->frame_rate,
			TIME_HMSF,
			asset->sample_rate,
			asset->frame_rate);
		
		char *tc_hours = tc;
		char *tc_minutes = strchr(tc, ':') + 1;
		*(tc_minutes - 1) = 0;
		char *tc_seconds = strchr(tc_minutes, ':') + 1;
		*(tc_seconds - 1) = 0;
		char *tc_rest = strchr(tc_seconds, ':') + 1;
		*(tc_rest - 1) = 0;
		
		add_subwindow(new AssetEditTCStartTextBox(this, atoi(tc_hours), x, y,
			(int) (asset->frame_rate * 60 * 60)));
		x += 30;
		add_subwindow(new BC_Title(x, y, ":"));
		x += 10;
		add_subwindow(new AssetEditTCStartTextBox(this, atoi(tc_minutes), x, y,
			(int) (asset->frame_rate * 60)));
		x += 30;
		add_subwindow(new BC_Title(x, y, ":"));
		x += 10;
		add_subwindow(new AssetEditTCStartTextBox(this, atoi(tc_seconds), x, y,
			(int) (asset->frame_rate)));
		x += 30;
		add_subwindow(new BC_Title(x, y, ":"));
		x += 10;
		add_subwindow(new AssetEditTCStartTextBox(this, atoi(tc_rest), x, y, 1));


		y += 30;
	}

	add_subwindow(new BC_OKButton(this));
	add_subwindow(new BC_CancelButton(this));
	show_window();
	flush();
	return 0;
}

AssetEditChannels::AssetEditChannels(AssetEditWindow *fwindow, 
	char *text, 
	int x,
	int y)
 : BC_TumbleTextBox(fwindow, 
		(int)atol(text),
		(int)1,
		(int)MAXCHANNELS,
		x, 
		y, 
		50)
{
	this->fwindow = fwindow;
}

int AssetEditChannels::handle_event()
{
	fwindow->asset->channels = atol(get_text());
	return 1;
}


Interlaceautofix::Interlaceautofix(MWindow *mwindow,AssetEditWindow *fwindow, int x, int y)
 : BC_CheckBox(x, y, fwindow->asset->interlace_autofixoption, _("Automatically Fix Interlacing"))
{
	this->fwindow = fwindow;
	this->mwindow = mwindow;
}

Interlaceautofix::~Interlaceautofix()
{
}

int Interlaceautofix::handle_event()
{
	fwindow->asset->interlace_autofixoption = get_value();
	showhideotherwidgets();
	return 1;
}

void Interlaceautofix::showhideotherwidgets()
{
	if(get_value())
	{
		int method = InterlaceFixSelection::automode(mwindow->edl->session->interlace_mode,
			fwindow->asset->interlace_mode);
		fwindow->ilacefix_selection->disable(0);
		fwindow->ilacefix_selection->update(method);
	}
	else
	{
		fwindow->ilacefix_selection->enable(1);
		fwindow->ilacefix_selection->update(fwindow->asset->interlace_fixmethod);
	}
}

AssetInterlaceMode::AssetInterlaceMode(int x, int y, BC_WindowBase *base_gui, int *value)
 : AInterlaceModeSelection(x, y, base_gui, value)
{
	autofix = 0;
}

int AssetInterlaceMode::handle_event()
{
	int result = Selection::handle_event();

	if(autofix)
		autofix->showhideotherwidgets();
	return result;
}


AssetEditHeader::AssetEditHeader(AssetEditWindow *fwindow, char *text, int x, int y)
 : BC_TextBox(x, y, 100, 1, text)
{
	this->fwindow = fwindow;
}

int AssetEditHeader::handle_event()
{
	fwindow->asset->header = atol(get_text());
	return 1;
}

AssetEditByteOrderLOHI::AssetEditByteOrderLOHI(AssetEditWindow *fwindow, 
	int value, 
	int x,
	int y)
 : BC_Radial(x, y, value, _("Lo-Hi"))
{
	this->fwindow = fwindow;
}

int AssetEditByteOrderLOHI::handle_event()
{
	fwindow->asset->byte_order = 1;
	fwindow->hilo->update(0);
	update(1);
	return 1;
}

AssetEditByteOrderHILO::AssetEditByteOrderHILO(AssetEditWindow *fwindow, 
	int value, 
	int x, 
	int y)
 : BC_Radial(x, y, value, _("Hi-Lo"))
{
	this->fwindow = fwindow;
}

int AssetEditByteOrderHILO::handle_event()
{
	fwindow->asset->byte_order = 0;
	fwindow->lohi->update(0);
	update(1);
	return 1;
}

AssetEditSigned::AssetEditSigned(AssetEditWindow *fwindow, 
	int value, 
	int x, 
	int y)
 : BC_CheckBox(x, y, value, _("Values are signed"))
{
	this->fwindow = fwindow;
}

int AssetEditSigned::handle_event()
{
	fwindow->asset->signed_ = get_value();
	return 1;
}







AssetEditPathText::AssetEditPathText(AssetEditWindow *fwindow, int y)
 : BC_TextBox(5, y, 300, 1, fwindow->asset->path) 
{
	this->fwindow = fwindow; 
}
AssetEditPathText::~AssetEditPathText() 
{
}
int AssetEditPathText::handle_event() 
{
	strcpy(fwindow->asset->path, get_text());
	return 1;
}

AssetEditPath::AssetEditPath(MWindow *mwindow, AssetEditWindow *fwindow, BC_TextBox *textbox, int y, const char *text, const char *window_title, const char *window_caption)
 : BrowseButton(mwindow, fwindow, textbox, 310, y, text, window_title, window_caption, 0) 
{ 
	this->fwindow = fwindow; 
}
AssetEditPath::~AssetEditPath() {}


AssetEditReelName::AssetEditReelName(AssetEditWindow *fwindow, int x, int y)
 : BC_TextBox(x, y, 190, 1, fwindow->asset->reel_name, 1, MEDIUMFONT, 1)
{
	this->fwindow = fwindow;
}
AssetEditReelName::~AssetEditReelName()
{
}
int AssetEditReelName::handle_event()
{
	strcpy(fwindow->asset->reel_name, get_utf8text());
	return 1;
}





AssetEditReelNumber::AssetEditReelNumber(AssetEditWindow *fwindow, int x, int y)
 : BC_TextBox(x, y, 190, 1, fwindow->asset->reel_number)
{
	this->fwindow = fwindow;
}
AssetEditReelNumber::~AssetEditReelNumber()
{
}
int AssetEditReelNumber::handle_event()
{
	char *text = get_text() + strlen(get_text()) - 1;
	
	// Don't let user enter an invalid character -- only numbers here
	if(*text < 48 ||
		*text > 57)
	{
		*text = '\0';
	}

	fwindow->asset->reel_number = atoi(get_text());
	return 1;
}





AssetEditTCStartTextBox::AssetEditTCStartTextBox(AssetEditWindow *fwindow, int value, int x, int y, int multiplier)
 : BC_TextBox(x, y, 30, 1, value)
{
	this->fwindow = fwindow;
	this->multiplier = multiplier;
	previous = value;
}
AssetEditTCStartTextBox::~AssetEditTCStartTextBox()
{
}
int AssetEditTCStartTextBox::handle_event()
{
	fwindow->asset->tcstart -= previous * multiplier;
	fwindow->asset->tcstart += atoi(get_text()) * multiplier;
	previous = atoi(get_text());
	return 1;
}
