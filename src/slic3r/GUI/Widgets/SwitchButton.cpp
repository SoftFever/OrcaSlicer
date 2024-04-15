#include "SwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"

#include "../wxExtensions.hpp"
#include "../Utils/MacDarkMode.hpp"

#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

SwitchButton::SwitchButton(wxWindow* parent, wxWindowID id)
	: wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT)
	, m_on(this, "toggle_on", 16)
	, m_off(this, "toggle_off", 16)
    , text_color(
		std::pair{0xfffffe, (int) StateColor::Checked},
		std::pair{0x6B6B6A, (int) StateColor::Normal}
	)
    , track_color(0xDFDFDF)
    , thumb_color(
		std::pair{0x009688, (int) StateColor::Checked},
		std::pair{0xD9D9D9, (int) StateColor::Normal}
	)
    , toggle_track_color(
		std::pair{0x009688, (int) StateColor::Checked},
		std::pair{0xDFDFDF, (int) StateColor::Normal}
	)
    , toggle_thumb_color(
		std::pair{0xF2F3F2, (int) StateColor::Checked},
		std::pair{0xF2F3F2, (int) StateColor::Normal}
	)
{
	SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
	SetFont(Label::Body_12);
	Rescale();
}

void SwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
	Rescale();
}

void SwitchButton::SetTextColor(StateColor const& color)
{
	text_color = color;
}

void SwitchButton::SetTextColor2(StateColor const &color)
{
	text_color2 = color;
}

void SwitchButton::SetTrackColor(StateColor const& color)
{
	track_color = color;
}

void SwitchButton::SetThumbColor(StateColor const& color)
{
	thumb_color = color;
}

void SwitchButton::SetValue(bool value)
{
	if (value != GetValue())
		wxBitmapToggleButton::SetValue(value);
	update();
}

void SwitchButton::Rescale()
{
	if (labels[0].IsEmpty()) { // ORCA: Instead of using scalable vector draw icon to get correct colors for light / dark theme
		//m_on.msw_rescale();
		//m_off.msw_rescale();
        SetBackgroundColour(StaticBox::GetParentBackgroundColor(GetParent()));
		#ifdef __WXOSX__
			auto scale = Slic3r::GUI::mac_max_scaling_factor();
			int  BS    = (int) scale;
		#else
			constexpr int BS = 1;
		#endif
		wxSize     thumbSize;
		wxSize     trackSize;
		wxClientDC dc(this);
		#ifdef __WXOSX__
			dc.SetFont(dc.GetFont().Scaled(scale));
		#endif

		thumbSize	  = dc.GetTextExtent("00");
		thumbSize.x  += BS * 2;
		thumbSize.y  += BS * 2;
		trackSize.x   = thumbSize.x * 2 - BS * 4;
		trackSize.y	  = thumbSize.y + BS * 2;
		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
			#ifdef __WXMSW__
				wxBitmap bmp(trackSize.x, trackSize.y);
				memdc.SelectObject(bmp);
				memdc.SetBackground(wxBrush(GetBackgroundColour()));
				memdc.Clear();
			#else
				wxImage image(trackSize);
				image.InitAlpha();
				memset(image.GetAlpha(), 0, trackSize.GetWidth() * trackSize.GetHeight());
				wxBitmap bmp(std::move(image));
				memdc.SelectObject(bmp);
			#endif
            memdc.SetFont(dc.GetFont());
			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
			{
				#ifdef __WXMSW__
					wxGCDC dc2(memdc);
				#else
					wxDC& dc2(memdc);
				#endif
				dc2.SetBrush(wxBrush(toggle_track_color.colorForStates(state)));
				dc2.SetPen(wxPen(toggle_track_color.colorForStates(state)));
				dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), FromDIP(6)); // ORCA: Use less rounding to match new style
                dc2.SetBrush(wxBrush(toggle_thumb_color.colorForStates(state)));
                dc2.SetPen(wxPen(toggle_thumb_color.colorForStates(state)));
				dc2.DrawRoundedRectangle(
					wxRect(
						{i == 0 ? (BS + 1) : (trackSize.x - thumbSize.x - BS + 1), BS + 1},
						wxSize(thumbSize.x - 2,trackSize.y - 4) // ORCA: Increse margin for thumb to match new style
					),
					FromDIP(4) // ORCA: Use less rounding to match new style
				);
			}
			memdc.SelectObject(wxNullBitmap);
			#ifdef __WXOSX__
				bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
			#endif
				(i == 0 ? m_off : m_on).bmp() = bmp;
		}
	}
	else {
		SetBackgroundColour(StaticBox::GetParentBackgroundColor(GetParent()));
#ifdef __WXOSX__
        auto scale = Slic3r::GUI::mac_max_scaling_factor();
        int BS = (int) scale;
#else
        constexpr int BS = 1;
#endif
		wxSize thumbSize;
		wxSize trackSize;
		wxClientDC dc(this);
#ifdef __WXOSX__
        dc.SetFont(dc.GetFont().Scaled(scale));
#endif
        wxSize textSize[2];
		{
			textSize[0] = dc.GetTextExtent(labels[0]);
			textSize[1] = dc.GetTextExtent(labels[1]);
		}
		float fontScale = 0;
		{
			thumbSize = textSize[0];
			auto size = textSize[1];
			if (size.x > thumbSize.x) thumbSize.x = size.x;
			else size.x = thumbSize.x;
			thumbSize.x += BS * 12;
			thumbSize.y += BS * 6;
			trackSize.x = thumbSize.x + size.x + BS * 10;
			trackSize.y = thumbSize.y + BS * 2;
            auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
            maxWidth *= scale;
#endif
			if (trackSize.x > maxWidth) {
                fontScale   = float(maxWidth) / trackSize.x;
                thumbSize.x -= (trackSize.x - maxWidth) / 2;
                trackSize.x = maxWidth;
			}
		}
		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
#ifdef __WXMSW__
			wxBitmap bmp(trackSize.x, trackSize.y);
			memdc.SelectObject(bmp);
			memdc.SetBackground(wxBrush(GetBackgroundColour()));
			memdc.Clear();
#else
            wxImage image(trackSize);
            image.InitAlpha();
            memset(image.GetAlpha(), 0, trackSize.GetWidth() * trackSize.GetHeight());
            wxBitmap bmp(std::move(image));
            memdc.SelectObject(bmp);
#endif
            memdc.SetFont(dc.GetFont());
            if (fontScale) {
                memdc.SetFont(dc.GetFont().Scaled(fontScale));
                textSize[0] = memdc.GetTextExtent(labels[0]);
                textSize[1] = memdc.GetTextExtent(labels[1]);
			}
			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
            {
#ifdef __WXMSW__
				wxGCDC dc2(memdc);
#else
                wxDC &dc2(memdc);
#endif
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
                //dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), trackSize.y / 2);
                dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), FromDIP(6)); //ORCA: Use less rounding to match new style 
				dc2.SetBrush(wxBrush(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
				dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
				//dc2.DrawRoundedRectangle(wxRect({ i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS}, thumbSize), thumbSize.y / 2);
                dc2.DrawRoundedRectangle(wxRect(
						{i == 0 ? (BS + 1) : (trackSize.x - thumbSize.x - BS + 1), BS + 1},
						wxSize(thumbSize.x - 2, trackSize.y - 4)  // ORCA: Increse margin for thumb to match new style
					),
                    FromDIP(4) // ORCA: Use less rounding to match new style 
				);
			}
            memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
            memdc.DrawText(labels[0], {BS + (thumbSize.x - textSize[0].x) / 2, BS + (thumbSize.y - textSize[0].y) / 2});
            memdc.SetTextForeground(text_color2.count() == 0 ? text_color.colorForStates(state) : text_color2.colorForStates(state));
            memdc.DrawText(labels[1], {trackSize.x - thumbSize.x - BS + (thumbSize.x - textSize[1].x) / 2, BS + (thumbSize.y - textSize[1].y) / 2});
			memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
            bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#endif
			(i == 0 ? m_off : m_on).bmp() = bmp;
		}
	}
	SetSize(m_on.GetBmpSize());
	update();
}

void SwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
}
