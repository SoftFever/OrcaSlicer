#include "NotificationManager.hpp"

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "ImGuiWrapper.hpp"

#include "wxExtensions.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <wx/glcanvas.h>
#include <iostream>




#define NOTIFICATION_MAX_MOVE 3.0f

#define GAP_WIDTH 10.0f
#define SPACE_RIGHT_PANEL 10.0f

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED, EjectDriveNotificationClickedEvent);
wxDEFINE_EVENT(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED, ExportGcodeNotificationClickedEvent);
wxDEFINE_EVENT(EVT_PRESET_UPDATE_AVIABLE_CLICKED, PresetUpdateAviableClickedEvent);

namespace Notifications_Internal{
	void push_style_color(ImGuiCol idx, const ImVec4& col, bool fading_out, float current_fade_opacity)
	{
		if (fading_out)
			ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w * current_fade_opacity));
		else
			ImGui::PushStyleColor(idx, col);
	}
}
//ScalableBitmap bmp_icon;
//------PopNotification--------
NotificationManager::PopNotification::PopNotification(const NotificationData &n, const int id, wxEvtHandler* evt_handler) :
	  m_data                (n)
	, m_id                  (id)    
	, m_remaining_time      (n.duration)
	, m_last_remaining_time (n.duration)
	, m_counting_down       (n.duration != 0)
	, m_text1               (n.text1)
    , m_hypertext           (n.hypertext)
    , m_text2               (n.text2)
	, m_evt_handler         (evt_handler)
{
	init();
}
NotificationManager::PopNotification::~PopNotification()
{
}
NotificationManager::PopNotification::RenderResult NotificationManager::PopNotification::render(GLCanvas3D& canvas, const float& initial_y)
{
	if (m_finished)
		return RenderResult::Finished;
	if (m_close_pending) {
		// request of extra frame will be done in caller function by ret val ClosePending
		m_finished = true;
		return RenderResult::ClosePending;
	}
	if (m_hidden) {
		m_top_y = initial_y - GAP_WIDTH;
		return RenderResult::Static;
	}
	RenderResult    ret_val = m_counting_down ? RenderResult::Countdown : RenderResult::Static;
	Size            cnv_size = canvas.get_canvas_size();
	ImGuiWrapper&   imgui = *wxGetApp().imgui();
	bool            shown = true;
	std::string     name;
	ImVec2          mouse_pos = ImGui::GetMousePos();

	if (m_line_height != ImGui::CalcTextSize("A").y)
		init();
	
	set_next_window_size(imgui);

	//top y of window
	m_top_y = initial_y + m_window_height;
	//top right position
	ImVec2 win_pos(1.0f * (float)cnv_size.get_width() - SPACE_RIGHT_PANEL, 1.0f * (float)cnv_size.get_height() - m_top_y);
	imgui.set_next_window_pos(win_pos.x, win_pos.y, ImGuiCond_Always, 1.0f, 0.0f);
	imgui.set_next_window_size(m_window_width, m_window_height, ImGuiCond_Always);

	//find if hovered
	if (mouse_pos.x < win_pos.x && mouse_pos.x > win_pos.x - m_window_width && mouse_pos.y > win_pos.y&& mouse_pos.y < win_pos.y + m_window_height)
	{
		ImGui::SetNextWindowFocus();
		ret_val = RenderResult::Hovered;
		//reset fading
		m_fading_out = false;
		m_current_fade_opacity = 1.f;
		m_remaining_time = m_data.duration;
		m_countdown_frame = 0;
	}

	if (m_counting_down && m_remaining_time < 0)
		m_close_pending = true;

	if (m_close_pending) {
		// request of extra frame will be done in caller function by ret val ClosePending
		m_finished = true;
		return RenderResult::ClosePending;
	}

	// color change based on fading out
	bool fading_pop = false;
	if (m_fading_out) {
		if (!m_paused)
			m_current_fade_opacity -= 1.f / ((m_fading_time + 1.f) * 60.f);
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_fading_out, m_current_fade_opacity);
		Notifications_Internal::push_style_color(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text), m_fading_out, m_current_fade_opacity);
		fading_pop = true;
	}
	// background color
	if (m_is_gray) {
		ImVec4 backcolor(0.7f, 0.7f, 0.7f, 0.5f);
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	} else if (m_data.level == NotificationLevel::ErrorNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	} else if (m_data.level == NotificationLevel::WarningNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		backcolor.y += 0.15f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	}

	//name of window - probably indentifies window and is shown so last_end add whitespaces according to id
	for (size_t i = 0; i < m_id; i++)
		name += " ";
	if (imgui.begin(name, &shown, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar )) {
		if (shown) {
			
			ImVec2 win_size = ImGui::GetWindowSize();
			
			
			//FIXME: dont forget to us this for texts
			//GUI::format(_utf8(L()));
			
			/*
			//countdown numbers
			ImGui::SetCursorPosX(15);
			ImGui::SetCursorPosY(15);
			imgui.text(std::to_string(m_remaining_time).c_str());
			*/
			if(m_counting_down)
				render_countdown(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
			render_left_sign(imgui);
			render_text(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
			render_close_button(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
			if (m_multiline && m_lines_count > 3)
				render_minimize_button(imgui, win_pos.x, win_pos.y);
		} else {
			// the user clicked on the [X] button ( ImGuiWindowFlags_NoTitleBar means theres no [X] button)
			m_close_pending = true;
			canvas.set_as_dirty();
		}
	}
	imgui.end();
	
	if (fading_pop) {
		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
	}
	if (m_is_gray)
		ImGui::PopStyleColor();
	else if (m_data.level == NotificationLevel::ErrorNotification)
		ImGui::PopStyleColor();
	else if (m_data.level == NotificationLevel::WarningNotification)
		ImGui::PopStyleColor();
	return ret_val;
}
void NotificationManager::PopNotification::init()
{
	std::string text          = m_text1 + " " + m_hypertext;
	int         last_end      = 0;
	            m_lines_count = 0;

	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	m_left_indentation = m_line_height;
	if (m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		float picture_width = ImGui::CalcTextSize(text.c_str()).x;
		m_left_indentation = picture_width + m_line_height / 2;
	}
	m_window_width_offset = m_left_indentation + m_line_height * 2;
	m_window_width = m_line_height * 25;
	
	// count lines
	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		int next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end > 0 && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			//next line is ended by '/n'
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		}
		else {
			// find next suitable endline
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - 3.5f * m_line_height) {// m_window_width_offset) {
				// more than one line till end
				int next_space = text.find_first_of(' ', last_end);
				if (next_space > 0) {
					int next_space_candidate = text.find_first_of(' ', next_space + 1);
					while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset) {
						next_space = next_space_candidate;
						next_space_candidate = text.find_first_of(' ', next_space + 1);
					}
					m_endlines.push_back(next_space);
					last_end = next_space + 1;
				}
			}
			else {
				m_endlines.push_back(text.length());
				last_end = text.length();
			}

		}
		m_lines_count++;
	}
}
void NotificationManager::PopNotification::set_next_window_size(ImGuiWrapper& imgui)
{ 
	if (m_multiline) {
		m_window_height = m_lines_count * m_line_height;
	}else
	{
		m_window_height = 2 * m_line_height;
	}
	m_window_height += 1 * m_line_height; // top and bottom
}

void NotificationManager::PopNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2      win_size(win_size_x, win_size_y);
	ImVec2      win_pos(win_pos_x, win_pos_y);
	float       x_offset = m_left_indentation;
	std::string fulltext = m_text1 + m_hypertext; //+ m_text2;
	ImVec2      text_size = ImGui::CalcTextSize(fulltext.c_str());
	// text posistions are calculated by lines count
	// large texts has "more" button or are displayed whole
	// smaller texts are divided as one liners and two liners
	if (m_lines_count > 2) {
		if (m_multiline) {
			
			int last_end = 0;
			float starting_y = m_line_height/2;//10;
			float shift_y = m_line_height;// -m_line_height / 20;
			for (size_t i = 0; i < m_lines_count; i++) {
			    std::string line = m_text1.substr(last_end , m_endlines[i] - last_end);
				last_end = m_endlines[i] + 1;
				ImGui::SetCursorPosX(x_offset);
				ImGui::SetCursorPosY(starting_y + i * shift_y);
				imgui.text(line.c_str());
			}
			//hyperlink text
			if (!m_hypertext.empty())
			{
				render_hypertext(imgui, x_offset + ImGui::CalcTextSize(m_text1.substr(m_endlines[m_lines_count - 2] + 1, m_endlines[m_lines_count - 1] - m_endlines[m_lines_count - 2] - 1).c_str()).x, starting_y + (m_lines_count - 1) * shift_y, m_hypertext);
			}
			
			
		} else {
			// line1
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(win_size.y / 2 - win_size.y / 6 - m_line_height / 2);
			imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
			// line2
			std::string line = m_text1.substr(m_endlines[0] + 1, m_endlines[1] - m_endlines[0] - 1);
			if (ImGui::CalcTextSize(line.c_str()).x > m_window_width - m_window_width_offset - ImGui::CalcTextSize((".." + _u8L("More")).c_str()).x)
			{
				line = line.substr(0, line.length() - 6);
				line += "..";
			}else
				line += "  ";
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(win_size.y / 2 + win_size.y / 6 - m_line_height / 2);
			imgui.text(line.c_str());
			// "More" hypertext
			render_hypertext(imgui, x_offset + ImGui::CalcTextSize(line.c_str()).x, win_size.y / 2 + win_size.y / 6 - m_line_height / 2, _u8L("More"), true);
		}
	} else {
		//text 1
		float cursor_y = win_size.y / 2 - text_size.y / 2;
		float cursor_x = x_offset;
		if(m_lines_count > 1) {
			// line1
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(win_size.y / 2 - win_size.y / 6 - m_line_height / 2);
			imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
			// line2
			std::string line = m_text1.substr(m_endlines[0] + 1);
			cursor_y = win_size.y / 2 + win_size.y / 6 - m_line_height / 2;
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(cursor_y);
			imgui.text(line.c_str());
			cursor_x = x_offset + ImGui::CalcTextSize(line.c_str()).x;
		} else {
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(cursor_y);
			imgui.text(m_text1.c_str());
			cursor_x = x_offset + ImGui::CalcTextSize(m_text1.c_str()).x;
		}
		//hyperlink text
		if (!m_hypertext.empty())
		{
			render_hypertext(imgui, cursor_x + 4, cursor_y, m_hypertext);
		}

		//notification text 2
		//text 2 is suposed to be after the hyperlink - currently it is not used
		/*
		if (!m_text2.empty())
		{
			ImVec2 part_size = ImGui::CalcTextSize(m_hypertext.c_str());
			ImGui::SetCursorPosX(win_size.x / 2 + text_size.x / 2 - part_size.x + 8 - x_offset);
			ImGui::SetCursorPosY(cursor_y);
			imgui.text(m_text2.c_str());
		}
		*/
	}
}

void NotificationManager::PopNotification::render_hypertext(ImGuiWrapper& imgui, const float text_x, const float text_y, const std::string text, bool more)
{
	//invisible button
	ImVec2 part_size = ImGui::CalcTextSize(text.c_str());
	ImGui::SetCursorPosX(text_x -4);
	ImGui::SetCursorPosY(text_y -5);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));
	if (imgui.button("   ", part_size.x + 6, part_size.y + 10))
	{
		if (more)
		{
			m_multiline = true;
			set_next_window_size(imgui);
		}
		else {
			on_text_click();
			m_close_pending = true;
		}
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();

	//hover color
	ImVec4 orange_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
		orange_color.y += 0.2f;

	//text
	Notifications_Internal::push_style_color(ImGuiCol_Text, orange_color, m_fading_out, m_current_fade_opacity);
	ImGui::SetCursorPosX(text_x);
	ImGui::SetCursorPosY(text_y);
	imgui.text(text.c_str());
	ImGui::PopStyleColor();

	//underline
	ImVec2 lineEnd = ImGui::GetItemRectMax();
	lineEnd.y -= 2;
	ImVec2 lineStart = lineEnd;
	lineStart.x = ImGui::GetItemRectMin().x;
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32((int)(orange_color.x * 255), (int)(orange_color.y * 255), (int)(orange_color.z * 255), (int)(orange_color.w * 255.f * (m_fading_out ? m_current_fade_opacity : 1.f))));

}

void NotificationManager::PopNotification::render_close_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImVec4 orange_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
	orange_color.w = 0.8f;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_fading_out, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));


	//button - if part if treggered
	std::string button_text;
	button_text = ImGui::CloseIconMarker;
	
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - win_size.x / 10.f, win_pos.y),
		                           ImVec2(win_pos.x, win_pos.y + win_size.y - (m_multiline? 2 * m_line_height : 0)),
		                           true))
	{
		button_text = ImGui::CloseIconHoverMarker;
	}
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.25f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y/2);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		m_close_pending = true;
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - win_size.x / 10.f);
	ImGui::SetCursorPosY(0);
	if (imgui.button(" ", win_size.x / 10.f, win_size.y - (m_multiline ? 2 * m_line_height : 0)))
	{
		m_close_pending = true;
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
void NotificationManager::PopNotification::render_countdown(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	/*
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	
	//countdown dots
	std::string dot_text;
	dot_text = m_remaining_time <= (float)m_data.duration / 4 * 3 ? ImGui::TimerDotEmptyMarker : ImGui::TimerDotMarker;
	ImGui::SetCursorPosX(win_size.x - m_line_height);
	//ImGui::SetCursorPosY(win_size.y / 2 - 24);
	ImGui::SetCursorPosY(0);
	imgui.text(dot_text.c_str());

	dot_text = m_remaining_time < m_data.duration / 2 ? ImGui::TimerDotEmptyMarker : ImGui::TimerDotMarker;
	ImGui::SetCursorPosX(win_size.x - m_line_height);
	//ImGui::SetCursorPosY(win_size.y / 2 - 9);
	ImGui::SetCursorPosY(win_size.y / 2 - m_line_height / 2);
	imgui.text(dot_text.c_str());

	dot_text = m_remaining_time <= m_data.duration / 4 ? ImGui::TimerDotEmptyMarker : ImGui::TimerDotMarker;
	ImGui::SetCursorPosX(win_size.x - m_line_height);
	//ImGui::SetCursorPosY(win_size.y / 2 + 6);
	ImGui::SetCursorPosY(win_size.y - m_line_height);
	imgui.text(dot_text.c_str());
	*/
	if (!m_fading_out && m_remaining_time <= m_data.duration / 4) {
		m_fading_out = true;
		m_fading_time = m_remaining_time;
	}
	
	if (m_last_remaining_time != m_remaining_time) {
		m_last_remaining_time = m_remaining_time;
		m_countdown_frame = 0;
	}
	/*
	//countdown line
	ImVec4 orange_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
	float  invisible_length = ((float)(m_data.duration - m_remaining_time) / (float)m_data.duration * win_size_x);
	invisible_length -= win_size_x / ((float)m_data.duration * 60.f) * (60 - m_countdown_frame);
	ImVec2 lineEnd = ImVec2(win_pos_x - invisible_length, win_pos_y + win_size_y - 5);
	ImVec2 lineStart = ImVec2(win_pos_x - win_size_x, win_pos_y + win_size_y - 5);
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32((int)(orange_color.x * 255), (int)(orange_color.y * 255), (int)(orange_color.z * 255), (int)(orange_color.picture_width * 255.f * (m_fading_out ? m_current_fade_opacity : 1.f))), 2.f);
	if (!m_paused)
		m_countdown_frame++;
		*/
}
void NotificationManager::PopNotification::render_left_sign(ImGuiWrapper& imgui)
{
	if (m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		ImGui::SetCursorPosX(m_line_height / 3);
		ImGui::SetCursorPosY(m_window_height / 2 - m_line_height / 2);
		imgui.text(text.c_str());
	} 
}
void NotificationManager::PopNotification::render_minimize_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y)
{
	ImVec4 orange_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
	orange_color.w = 0.8f;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_fading_out, m_current_fade_opacity);

	
	//button - if part if treggered
	std::string button_text;
	button_text = ImGui::MinimalizeMarker;
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos_x - m_window_width / 10.f, win_pos_y + m_window_height - 2 * m_line_height + 1),
		ImVec2(win_pos_x, win_pos_y + m_window_height),
		true)) 
	{
		button_text = ImGui::MinimalizeHoverMarker;
	}
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(m_window_width - m_line_height * 2.25f);
	ImGui::SetCursorPosY(m_window_height - button_size.y - 5);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		m_multiline = false;
	}
	
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
void NotificationManager::PopNotification::on_text_click()
{
	switch (m_data.type) {
	case NotificationType::ExportToRemovableFinished :
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, EjectDriveNotificationClickedEvent(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED));
		break;
	case NotificationType::SlicingComplete :
		//wxGetApp().plater()->export_gcode(false);
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, ExportGcodeNotificationClickedEvent(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED));
		break;
	case NotificationType::PresetUpdateAviable :
		//wxGetApp().plater()->export_gcode(false);
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, PresetUpdateAviableClickedEvent(EVT_PRESET_UPDATE_AVIABLE_CLICKED));
		break;
	case NotificationType::NewAppAviable:
		wxLaunchDefaultBrowser("https://github.com/prusa3d/PrusaSlicer/releases");
		break;
	default:
		break;
	}
}
void NotificationManager::PopNotification::update(const NotificationData& n)
{
	m_text1          = n.text1;
	m_hypertext      = n.hypertext;
    m_text2          = n.text2;
	init();
}
bool NotificationManager::PopNotification::compare_text(const std::string& text)
{
	std::string t1(m_text1);
	std::string t2(text);
	t1.erase(std::remove_if(t1.begin(), t1.end(), ::isspace), t1.end());
	t2.erase(std::remove_if(t2.begin(), t2.end(), ::isspace), t2.end());
	if (t1.compare(t2) == 0)
		return true;
	return false;
}

NotificationManager::SlicingCompleteLargeNotification::SlicingCompleteLargeNotification(const NotificationData& n, const int id, wxEvtHandler* evt_handler, bool large) :
	  NotificationManager::PopNotification(n, id, evt_handler)
{
	set_large(large);
}
void NotificationManager::SlicingCompleteLargeNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	if (!m_is_large)
		PopNotification::render_text(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	else {
		ImVec2 win_size(win_size_x, win_size_y);
		ImVec2 win_pos(win_pos_x, win_pos_y);

		ImVec2 text1_size = ImGui::CalcTextSize(m_text1.c_str());
		float x_offset = m_left_indentation;
		std::string fulltext = m_text1 + m_hypertext + m_text2;
		ImVec2 text_size = ImGui::CalcTextSize(fulltext.c_str());
		float cursor_y = win_size.y / 2 - text_size.y / 2;
		if (m_has_print_info) {
			x_offset = 20;
			cursor_y = win_size.y / 2 + win_size.y / 6 - text_size.y / 2;
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(cursor_y);
			imgui.text(m_print_info.c_str());
			cursor_y = win_size.y / 2 - win_size.y / 6 - text_size.y / 2;
		}
		ImGui::SetCursorPosX(x_offset);
		ImGui::SetCursorPosY(cursor_y);
		imgui.text(m_text1.c_str());

		render_hypertext(imgui, x_offset + text1_size.x + 4, cursor_y, m_hypertext);
		
	}
}
void NotificationManager::SlicingCompleteLargeNotification::set_print_info(std::string info)
{
	m_print_info = info;
	m_has_print_info = true;
	if(m_is_large)
		m_lines_count = 2;
}
void NotificationManager::SlicingCompleteLargeNotification::set_large(bool l)
{
	m_is_large = l;
	m_counting_down = !l;
	m_hypertext = l ? _u8L("Export G-Code.") : std::string();
	m_hidden = !l;
}
//------NotificationManager--------
NotificationManager::NotificationManager(wxEvtHandler* evt_handler) :
	m_evt_handler(evt_handler)
{
}
NotificationManager::~NotificationManager()
{
	for (PopNotification* notification : m_pop_notifications)
	{
		delete notification;
	}
}
void NotificationManager::push_notification(const NotificationType type, GLCanvas3D& canvas, int timestamp)
{
	auto it = std::find_if(basic_notifications.begin(), basic_notifications.end(),
		boost::bind(&NotificationData::type, _1) == type);	
	if (it != basic_notifications.end())
		push_notification_data( *it, canvas, timestamp);
}
void NotificationManager::push_notification(const std::string& text, GLCanvas3D& canvas, int timestamp)
{
	push_notification_data({ NotificationType::CustomNotification, NotificationLevel::RegularNotification, 10, text }, canvas, timestamp );
}
void NotificationManager::push_notification(const std::string& text, NotificationManager::NotificationLevel level, GLCanvas3D& canvas, int timestamp)
{
	switch (level)
	{
	case Slic3r::GUI::NotificationManager::NotificationLevel::RegularNotification:
		push_notification_data({ NotificationType::CustomNotification, level, 10, text }, canvas, timestamp);
		break;
	case Slic3r::GUI::NotificationManager::NotificationLevel::ErrorNotification:
		push_notification_data({ NotificationType::CustomNotification, level, 0, text }, canvas, timestamp);

		break;
	case Slic3r::GUI::NotificationManager::NotificationLevel::ImportantNotification:
		push_notification_data({ NotificationType::CustomNotification, level, 0, text }, canvas, timestamp);
		break;
	default:
		break;
	}
}
void NotificationManager::push_slicing_error_notification(const std::string& text, GLCanvas3D& canvas)
{
	set_all_slicing_errors_gray(false);
	push_notification_data({ NotificationType::SlicingError, NotificationLevel::ErrorNotification, 0,  _u8L("ERROR:") + "\n" + text }, canvas, 0);
	close_notification_of_type(NotificationType::SlicingComplete);
}
void NotificationManager::push_slicing_warning_notification(const std::string& text, bool gray, GLCanvas3D& canvas, size_t oid, int warning_step)
{
	NotificationData data { NotificationType::SlicingWarning, NotificationLevel::WarningNotification, 0,  _u8L("WARNING:") + "\n" + text };

	NotificationManager::SlicingWarningNotification* notification = new NotificationManager::SlicingWarningNotification(data, m_next_id++, m_evt_handler);
	notification->set_object_id(oid);
	notification->set_warning_step(warning_step);
	if 
		(push_notification_data(notification, canvas, 0)) {
		notification->set_gray(gray);		
	}
	else {
		delete notification;
	}
	
}
void NotificationManager::push_plater_error_notification(const std::string& text, GLCanvas3D& canvas)
{
	push_notification_data({ NotificationType::PlaterError, NotificationLevel::ErrorNotification, 0,  _u8L("ERROR:") + "\n" + text }, canvas, 0);
}
void NotificationManager::push_plater_warning_notification(const std::string& text, GLCanvas3D& canvas)
{
	push_notification_data({ NotificationType::PlaterWarning, NotificationLevel::WarningNotification, 0,  _u8L("WARNING:") + "\n" + text }, canvas, 0);
}
void NotificationManager::close_plater_error_notification()
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterError) {
			notification->close();
		}
	}
}
void NotificationManager::close_plater_warning_notification(const std::string& text)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterWarning && notification->compare_text(_u8L("WARNING:") + "\n" + text)) {
			notification->close();
		}
	}
}
void NotificationManager::set_all_slicing_errors_gray(bool g)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingError) {
			notification->set_gray(g);
		}
	}
}
void NotificationManager::set_all_slicing_warnings_gray(bool g)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingWarning) {
			notification->set_gray(g);
		}
	}
}
void NotificationManager::set_slicing_warning_gray(const std::string& text, bool g)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingWarning && notification->compare_text(text)) {
			notification->set_gray(g);
		}
	}
}
void NotificationManager::close_slicing_errors_and_warnings()
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingError || notification->get_type() == NotificationType::SlicingWarning) {
			notification->close();
		}
	}
}
void NotificationManager::push_slicing_complete_notification(GLCanvas3D& canvas, int timestamp, bool large)
{
	std::string hypertext;
	int         time = 10;
	if(large)
	{
		hypertext = _u8L("Export G-Code.");
		time = 0;
	}
	NotificationData data{ NotificationType::SlicingComplete, NotificationLevel::RegularNotification, time,  _u8L("Slicing finished."), hypertext };

	NotificationManager::SlicingCompleteLargeNotification* notification = new NotificationManager::SlicingCompleteLargeNotification(data, m_next_id++, m_evt_handler, large);
	if (push_notification_data(notification, canvas, timestamp)) {
	} else {
		delete notification;
	}	
}
void NotificationManager::set_slicing_complete_print_time(std::string info)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingComplete) {
			dynamic_cast<SlicingCompleteLargeNotification*>(notification)->set_print_info(info);
			break;
		}
	}
}
void NotificationManager::set_slicing_complete_large(bool large)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingComplete) {
			dynamic_cast<SlicingCompleteLargeNotification*>(notification)->set_large(large);
			break;
		}
	}
}
void NotificationManager::close_notification_of_type(const NotificationType type)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == type) {
			notification->close();
		}
	}
}
void NotificationManager::compare_warning_oids(const std::vector<size_t>& living_oids)
{
	for (PopNotification* notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingWarning) {
			auto w = dynamic_cast<SlicingWarningNotification*>(notification);
			bool found = false;
			for (size_t oid : living_oids) {
				if (w->get_object_id() == oid) {
					found = true;
					break;
				}
			}
			if (!found)
				notification->close();
		}
	}
}
bool NotificationManager::push_notification_data(const NotificationData &notification_data,  GLCanvas3D& canvas, int timestamp)
{
	PopNotification* n = new PopNotification(notification_data, m_next_id++, m_evt_handler);
	bool r = push_notification_data(n, canvas, timestamp);
	if (!r)
		delete n;
	return r;
}
bool NotificationManager::push_notification_data(NotificationManager::PopNotification* notification, GLCanvas3D& canvas, int timestamp)
{
	// if timestamped notif, push only new one
	if (timestamp != 0) {
		if (m_used_timestamps.find(timestamp) == m_used_timestamps.end()) {
			m_used_timestamps.insert(timestamp);
		} else {
			return false;
		}
	}
	if (!this->find_older(notification)) {
			m_pop_notifications.emplace_back(notification);
		canvas.request_extra_frame();
		return true;
	} else {
		m_pop_notifications.back()->update(notification->get_data());
		canvas.request_extra_frame();
		return false;
	}
}
void NotificationManager::render_notifications(GLCanvas3D& canvas)
{
	float    last_x = 0.0f;
	float    current_height = 0.0f;
	bool     request_next_frame = false;
	bool     render_main = false;
	bool     hovered = false;	
	sort_notifications();
	// iterate thru notifications and render them / erease them
	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end();) {
		if ((*it)->get_finished()) {
			delete (*it);
			it = m_pop_notifications.erase(it);
		} else {
			(*it)->set_paused(m_hovered);
			PopNotification::RenderResult res = (*it)->render(canvas, last_x);
			if (res != PopNotification::RenderResult::Finished) {
				last_x = (*it)->get_top() + GAP_WIDTH;
				current_height = std::max(current_height, (*it)->get_current_top());
				render_main = true;
			}
			if (res == PopNotification::RenderResult::Countdown || res == PopNotification::RenderResult::ClosePending || res == PopNotification::RenderResult::Finished)
				request_next_frame = true;
			if (res == PopNotification::RenderResult::Hovered)
				hovered = true;
			++it;
		}
	}
	m_hovered = hovered;

	//actualizate timers and request frame if needed
	wxWindow* p = dynamic_cast<wxWindow*> (wxGetApp().plater());
	while (p->GetParent())
		p = p->GetParent();
	wxTopLevelWindow* top_level_wnd = dynamic_cast<wxTopLevelWindow*>(p);
	if (!top_level_wnd->IsActive())
		return;

	if (!m_hovered && m_last_time < wxGetLocalTime())
	{
		if (wxGetLocalTime() - m_last_time == 1)
		{
			for(auto notification : m_pop_notifications)
			{
				notification->substract_remaining_time();
			}
		}
		m_last_time = wxGetLocalTime();
	}

	if (request_next_frame)
		canvas.request_extra_frame();
}


void NotificationManager::sort_notifications()
{
	std::sort(m_pop_notifications.begin(), m_pop_notifications.end(), [](PopNotification* n1, PopNotification* n2) {
		int n1l = (int)n1->get_data().level;
		int n2l = (int)n2->get_data().level;
		if (n1l == n2l && n1->get_is_gray() && !n2->get_is_gray())
			return true;
		return (n1l < n2l);
		});
}

bool NotificationManager::find_older(NotificationManager::PopNotification* notification)
{
	NotificationType type = notification->get_type();
	std::string text = notification->get_data().text1;
	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end(); ++it) {
		if((*it)->get_type() == type && !(*it)->get_finished()) {
			if (type == NotificationType::CustomNotification || type == NotificationType::PlaterWarning) {
				if (!(*it)->compare_text(text))
					continue;
			}else if (type == NotificationType::SlicingWarning) {
				auto w1 = dynamic_cast<SlicingWarningNotification*>(notification);
				auto w2 = dynamic_cast<SlicingWarningNotification*>(*it);
				if (w1 != nullptr && w2 != nullptr) {
					if (!(*it)->compare_text(text) || w1->get_object_id() != w2->get_object_id()) {
						continue;
					}
				} else {
					continue;
				}
			}

			if (it != m_pop_notifications.end() - 1)
				std::rotate(it, it + 1, m_pop_notifications.end());
			return true;
		}
	}
	return false;
}

void NotificationManager::set_in_preview(bool preview) 
{ 
    m_in_preview = preview;
    for (PopNotification* notification : m_pop_notifications) {
        if (notification->get_type() == NotificationType::PlaterWarning) 
            notification->hide(preview);     
    }
}
void NotificationManager::dpi_changed()
{

}

}//namespace GUI
}//namespace Slic3r
