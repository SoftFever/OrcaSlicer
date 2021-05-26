#include "NotificationManager.hpp"


#include "GUI.hpp"
#include "ImGuiWrapper.hpp"
#include "PrintHostDialogs.hpp"
#include "wxExtensions.hpp"
#include "../Utils/PrintHost.hpp"
#include "libslic3r/Config.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/bind/bind.hpp>
#include <boost/nowide/convert.hpp>

#include <iostream>

#include <wx/glcanvas.h>

static constexpr float GAP_WIDTH = 10.0f;
static constexpr float SPACE_RIGHT_PANEL = 10.0f;
static constexpr float FADING_OUT_DURATION = 2.0f;
// Time in Miliseconds after next render when fading out is requested
static constexpr int   FADING_OUT_TIMEOUT = 100;

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED, EjectDriveNotificationClickedEvent);
wxDEFINE_EVENT(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED, ExportGcodeNotificationClickedEvent);
wxDEFINE_EVENT(EVT_PRESET_UPDATE_AVAILABLE_CLICKED, PresetUpdateAvailableClickedEvent);

namespace Notifications_Internal{
	ImFont* add_default_font(float pixel_size)
	{
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig config;
		config.SizePixels = pixel_size;
		config.OversampleH = config.OversampleV = 1;
		config.PixelSnapH = true;
		ImFont* font = io.Fonts->AddFontDefault(&config);
		return font;
	}

	static inline void push_style_color(ImGuiCol idx, const ImVec4& col, bool fading_out, float current_fade_opacity)
	{
		if (fading_out)
			ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w * current_fade_opacity));
		else
			ImGui::PushStyleColor(idx, col);
	}

	void open_folder(const std::string& path)
	{
		// Code taken from desktop_open_datadir_folder()

		// Execute command to open a file explorer, platform dependent.
		// FIXME: The const_casts aren't needed in wxWidgets 3.1, remove them when we upgrade.

#ifdef _WIN32
		const wxString widepath = from_u8(path);
		const wchar_t* argv[] = { L"explorer", widepath.GetData(), nullptr };
		::wxExecute(const_cast<wchar_t**>(argv), wxEXEC_ASYNC, nullptr);
#elif __APPLE__
		const char* argv[] = { "open", path.data(), nullptr };
		::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr);
#else
		const char* argv[] = { "xdg-open", path.data(), nullptr };

		// Check if we're running in an AppImage container, if so, we need to remove AppImage's env vars,
		// because they may mess up the environment expected by the file manager.
		// Mostly this is about LD_LIBRARY_PATH, but we remove a few more too for good measure.
		if (wxGetEnv("APPIMAGE", nullptr)) {
			// We're running from AppImage
			wxEnvVariableHashMap env_vars;
			wxGetEnvMap(&env_vars);

			env_vars.erase("APPIMAGE");
			env_vars.erase("APPDIR");
			env_vars.erase("LD_LIBRARY_PATH");
			env_vars.erase("LD_PRELOAD");
			env_vars.erase("UNION_PRELOAD");

			wxExecuteEnv exec_env;
			exec_env.env = std::move(env_vars);

			wxString owd;
			if (wxGetEnv("OWD", &owd)) {
				// This is the original work directory from which the AppImage image was run,
				// set it as CWD for the child process:
				exec_env.cwd = std::move(owd);
			}

			::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, &exec_env);
		}
		else {
			// Looks like we're NOT running from AppImage, we'll make no changes to the environment.
			::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, nullptr);
		}
#endif
	}
}

#if 1
// Reuse ImGUI Windows.
int NotificationManager::NotificationIDProvider::allocate_id() 
{
	int id;
	if (m_released_ids.empty())
		id = ++m_next_id;
	else {
		id = m_released_ids.back();
		m_released_ids.pop_back();
	}
	return id;
}
void NotificationManager::NotificationIDProvider::release_id(int id) 
{
	m_released_ids.push_back(id);
}
#else
// Don't reuse ImGUI Windows, allocate a new ID every time.
int NotificationManager::NotificationIDProvider::allocate_id() { return ++ m_next_id; }
void NotificationManager::NotificationIDProvider::release_id(int) {}
#endif

//------PopNotification--------
NotificationManager::PopNotification::PopNotification(const NotificationData &n, NotificationIDProvider &id_provider, wxEvtHandler* evt_handler) :
	  m_data                (n)
	, m_id_provider   		(id_provider)
	, m_text1               (n.text1)
    , m_hypertext           (n.hypertext)
    , m_text2               (n.text2)
	, m_evt_handler         (evt_handler)
	, m_notification_start  (GLCanvas3D::timestamp_now())
{}

void NotificationManager::PopNotification::render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width)
{

	if (m_state == EState::Unknown)
		init();

	if (m_state == EState::Hidden) {
		m_top_y = initial_y - GAP_WIDTH;
		return;
	}

	if (m_state == EState::ClosePending || m_state == EState::Finished)
	{
		m_state = EState::Finished;
		return;
	}

	Size          cnv_size = canvas.get_canvas_size();
	ImGuiWrapper& imgui = *wxGetApp().imgui();
	ImVec2        mouse_pos = ImGui::GetMousePos();
	float         right_gap = SPACE_RIGHT_PANEL + (move_from_overlay ? overlay_width + m_line_height * 5 : 0);
	bool          fading_pop = false;

	if (m_line_height != ImGui::CalcTextSize("A").y)
		init();

	set_next_window_size(imgui);

	// top y of window
	m_top_y = initial_y + m_window_height;

	ImVec2 win_pos(1.0f * (float)cnv_size.get_width() - right_gap, 1.0f * (float)cnv_size.get_height() - m_top_y);
	imgui.set_next_window_pos(win_pos.x, win_pos.y, ImGuiCond_Always, 1.0f, 0.0f);
	imgui.set_next_window_size(m_window_width, m_window_height, ImGuiCond_Always);

	
	// find if hovered FIXME:  do it only in update state?
	if (m_state == EState::Hovered) {
		m_state = EState::Unknown;
		init(); 
	}
	
	if (mouse_pos.x < win_pos.x && mouse_pos.x > win_pos.x - m_window_width && mouse_pos.y > win_pos.y && mouse_pos.y < win_pos.y + m_window_height) {
		ImGui::SetNextWindowFocus();
		set_hovered();
	}
	
	// color change based on fading out
	if (m_state == EState::FadingOut) {
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), true, m_current_fade_opacity);
		Notifications_Internal::push_style_color(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text), true, m_current_fade_opacity);
		fading_pop = true;
	}
	
	bool bgrnd_color_pop = push_background_color();
	
	
	// name of window indentifies window - has to be unique string
	if (m_id == 0)
		m_id = m_id_provider.allocate_id();
	std::string name = "!!Ntfctn" + std::to_string(m_id);
	
	if (imgui.begin(name, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
		ImVec2 win_size = ImGui::GetWindowSize();

		render_left_sign(imgui);
		render_text(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
		render_close_button(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
		m_minimize_b_visible = false;
		if (m_multiline && m_lines_count > 3)
			render_minimize_button(imgui, win_pos.x, win_pos.y);
	}
	imgui.end();

	if (bgrnd_color_pop)
		ImGui::PopStyleColor();

	if (fading_pop)
		ImGui::PopStyleColor(2);
}
bool NotificationManager::PopNotification::push_background_color()
{
	if (m_is_gray) {
		ImVec4 backcolor(0.7f, 0.7f, 0.7f, 0.5f);
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_state == EState::FadingOut, m_current_fade_opacity);
		return true;
	}
	if (m_data.level == NotificationLevel::ErrorNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_state == EState::FadingOut, m_current_fade_opacity);
		return true;
	}
	if (m_data.level == NotificationLevel::WarningNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		backcolor.y += 0.15f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_state == EState::FadingOut, m_current_fade_opacity);
		return true;
	}
	return false;
}
void NotificationManager::PopNotification::count_spaces()
{
	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	m_left_indentation = m_line_height;
	if (m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		float picture_width = ImGui::CalcTextSize(text.c_str()).x;
		m_left_indentation = picture_width + m_line_height / 2;
	}
	m_window_width_offset = m_left_indentation + m_line_height * 3.f;
	m_window_width = m_line_height * 25;
}
 
void NotificationManager::PopNotification::count_lines()
{
	std::string text		= m_text1;
	size_t      last_end	= 0;
	m_lines_count			= 0;

	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		size_t next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			//next line is ended by '/n'
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		}
		else {
			// find next suitable endline
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset) {
				// more than one line till end
				size_t next_space = text.find_first_of(' ', last_end);
				if (next_space > 0) {
					size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
					while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset) {
						next_space = next_space_candidate;
						next_space_candidate = text.find_first_of(' ', next_space + 1);
					}
					// when one word longer than line.
					if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset) {
						float width_of_a = ImGui::CalcTextSize("a").x;
						int letter_count = (int)((m_window_width - m_window_width_offset) / width_of_a);
						while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset) {
							letter_count++;
						}
						m_endlines.push_back(last_end + letter_count);
						last_end += letter_count;
					}
					else {
						m_endlines.push_back(next_space);
						last_end = next_space + 1;
					}
				}
			}
			else {
				m_endlines.push_back(text.length());
				last_end = text.length();
			}

		}
		m_lines_count++;
	}
	// hypertext calculation
	if (!m_hypertext.empty()) {
		int prev_end = m_endlines.size() > 1 ? m_endlines[m_endlines.size() - 2] : 0; // m_endlines.size() - 2 because we are fitting hypertext instead of last endline
		if (ImGui::CalcTextSize((escape_string_cstyle(text.substr(prev_end, last_end - prev_end)) + m_hypertext).c_str()).x > m_window_width - m_window_width_offset) {
			m_endlines.push_back(last_end);
			m_lines_count++;
		}
	}
}

void NotificationManager::PopNotification::init()
{
	// Do not init closing notification
	if (is_finished())
		return;

	count_spaces();
	count_lines();

	if (m_lines_count == 3)
		m_multiline = true;
	m_notification_start = GLCanvas3D::timestamp_now();
	if (m_state == EState::Unknown)
		m_state = EState::Shown;
}
void NotificationManager::PopNotification::set_next_window_size(ImGuiWrapper& imgui)
{ 
	m_window_height = m_multiline ?
		m_lines_count * m_line_height :
		2 * m_line_height;
	m_window_height += 1 * m_line_height; // top and bottom
}

void NotificationManager::PopNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2      win_size(win_size_x, win_size_y);
	float       x_offset = m_left_indentation;
	std::string fulltext = m_text1 + m_hypertext; //+ m_text2;
	ImVec2      text_size = ImGui::CalcTextSize(fulltext.c_str());
	// text posistions are calculated by lines count
	// large texts has "more" button or are displayed whole
	// smaller texts are divided as one liners and two liners
	if (m_lines_count > 2) {
		if (m_multiline) {
			
			int last_end = 0;
			float starting_y = m_line_height/2;
			float shift_y = m_line_height;
			std::string line;

			for (size_t i = 0; i < m_lines_count; i++) {
				line.clear();
				ImGui::SetCursorPosX(x_offset);
				ImGui::SetCursorPosY(starting_y + i * shift_y);
				if (m_endlines.size() > i && m_text1.size() >= m_endlines[i]) {
					line = m_text1.substr(last_end, m_endlines[i] - last_end);
					last_end = m_endlines[i];
					if (m_text1.size() > m_endlines[i])
						last_end += (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
					imgui.text(line.c_str());
				}
			}
			//hyperlink text
			if (!m_hypertext.empty()) {
				render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + (line.empty() ? "" : " ")).c_str()).x, starting_y + (m_lines_count - 1) * shift_y, m_hypertext);
			}
			
			
		} else {
			// line1
			if (m_text1.size() >= m_endlines[0]) {
				ImGui::SetCursorPosX(x_offset);
				ImGui::SetCursorPosY(win_size.y / 2 - win_size.y / 6 - m_line_height / 2);
				imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
			}
			// line2
			std::string line;
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(win_size.y / 2 + win_size.y / 6 - m_line_height / 2);
			if (m_text1.size() >= m_endlines[1]) {
				line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0), m_endlines[1] - m_endlines[0] - (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
				if (ImGui::CalcTextSize(line.c_str()).x > m_window_width - m_window_width_offset - ImGui::CalcTextSize((".." + _u8L("More")).c_str()).x) {
					line = line.substr(0, line.length() - 6);
					line += "..";
				} else
					line += "  ";
				imgui.text(line.c_str());
			}
			// "More" hypertext
			render_hypertext(imgui, x_offset + ImGui::CalcTextSize(line.c_str()).x, win_size.y / 2 + win_size.y / 6 - m_line_height / 2, _u8L("More"), true);			
		}
	} else {
		//text 1
		float cursor_y = win_size.y / 2 - text_size.y / 2;
		float cursor_x = x_offset;
		if(m_lines_count > 1) {
			// line1
			if (m_text1.length() >= m_endlines[0]) { // could be equal than substr takes whole string
				ImGui::SetCursorPosX(x_offset);
				ImGui::SetCursorPosY(win_size.y / 2 - win_size.y / 6 - m_line_height / 2);
				imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
			}
			// line2
			ImGui::SetCursorPosX(x_offset);
			cursor_y = win_size.y / 2 + win_size.y / 6 - m_line_height / 2;
			ImGui::SetCursorPosY(cursor_y);
			if (m_text1.length() > m_endlines[0]) { // must be greater otherwise theres nothing to show and m_text1[m_endlines[0]] is beyond last letter
				std::string line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
				imgui.text(line.c_str());
				cursor_x = x_offset + ImGui::CalcTextSize(line.c_str()).x;
			}
		} else {
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(cursor_y);
			imgui.text(m_text1.c_str());
			cursor_x = x_offset + ImGui::CalcTextSize(m_text1.c_str()).x;
		}
		//hyperlink text
		if (!m_hypertext.empty()) {
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
		else if (on_text_click()) {
			close();
		}
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();

	//hover color
	ImVec4 orange_color = ImVec4(.99f, .313f, .0f, 1.0f);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
		orange_color.y += 0.2f;

	//text
	Notifications_Internal::push_style_color(ImGuiCol_Text, orange_color, m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::SetCursorPosX(text_x);
	ImGui::SetCursorPosY(text_y);
	imgui.text(text.c_str());
	ImGui::PopStyleColor();

	//underline
	ImVec2 lineEnd = ImGui::GetItemRectMax();
	lineEnd.y -= 2;
	ImVec2 lineStart = lineEnd;
	lineStart.x = ImGui::GetItemRectMin().x;
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32((int)(orange_color.x * 255), (int)(orange_color.y * 255), (int)(orange_color.z * 255), (int)(orange_color.w * 255.f * (m_state == EState::FadingOut ? m_current_fade_opacity : 1.f))));

}

void NotificationManager::PopNotification::render_close_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y); 
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));


	std::string button_text;
	button_text = ImGui::CloseNotifButton;
	
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - win_size.x / 10.f, win_pos.y),
		                           ImVec2(win_pos.x, win_pos.y + win_size.y - ( m_minimize_b_visible ? 2 * m_line_height : 0)),
		                           true))
	{
		button_text = ImGui::CloseNotifHoverButton;
	}
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.75f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		close();
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.35f);
	ImGui::SetCursorPosY(0);
	if (imgui.button(" ", m_line_height * 2.125, win_size.y - ( m_minimize_b_visible ? 2 * m_line_height : 0)))
	{
		close();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}

void NotificationManager::PopNotification::render_left_sign(ImGuiWrapper& imgui)
{
	if (m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		ImGui::SetCursorPosX(m_line_height / 3);
		ImGui::SetCursorPosY(m_window_height / 2 - m_line_height);
		imgui.text(text.c_str());
	} 
}
void NotificationManager::PopNotification::render_minimize_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y)
{
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_state == EState::FadingOut, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);

	
	//button - if part if treggered
	std::string button_text;
	button_text = ImGui::MinimalizeButton;
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos_x - m_window_width / 10.f, win_pos_y + m_window_height - 2 * m_line_height + 1),
		ImVec2(win_pos_x, win_pos_y + m_window_height),
		true)) 
	{
		button_text = ImGui::MinimalizeHoverButton;
	}
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(m_window_width - m_line_height * 1.8f);
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
	m_minimize_b_visible = true;
}
bool NotificationManager::PopNotification::on_text_click()
{
	if(m_data.callback != nullptr)
		return m_data.callback(m_evt_handler);
	return false;
}
void NotificationManager::PopNotification::update(const NotificationData& n)
{
	m_text1          = n.text1;
	m_hypertext      = n.hypertext;
    m_text2          = n.text2;
	init();
}
bool NotificationManager::PopNotification::compare_text(const std::string& text) const 
{
	std::wstring wt1 = boost::nowide::widen(m_text1);
	std::wstring wt2 = boost::nowide::widen(text);
	wt1.erase(std::remove_if(wt1.begin(), wt1.end(), ::iswspace), wt1.end());
	wt2.erase(std::remove_if(wt2.begin(), wt2.end(), ::iswspace), wt2.end());
	if (wt1.compare(wt2) == 0)
		return true;
	return false;
}

bool NotificationManager::PopNotification::update_state(bool paused, const int64_t delta)
{

	m_next_render = std::numeric_limits<int64_t>::max();

	if (m_state == EState::Unknown) {
		init();
		return true;
	}

	if (m_state == EState::Hidden) {
		return false;
	}

	int64_t now = GLCanvas3D::timestamp_now();

	// reset timers - hovered state is set in render 
	if (m_state == EState::Hovered) { 
		m_current_fade_opacity = 1.0f;
		m_state = EState::Unknown;
		init();
	// Timers when not fading
	} else if (m_state != EState::NotFading && m_state != EState::FadingOut && m_data.duration != 0 && !paused) {
		int64_t up_time = now - m_notification_start;
		if (up_time >= m_data.duration * 1000) {
			m_state					= EState::FadingOut;
			m_fading_start			= now;
		} else {
			m_next_render = m_data.duration * 1000 - up_time;
		}	
	}
	// Timers when fading
	if (m_state == EState::FadingOut && !paused) {
		int64_t curr_time		= now - m_fading_start;
		int64_t next_render		= FADING_OUT_TIMEOUT - delta;
		m_current_fade_opacity	= std::clamp(1.0f - 0.001f * static_cast<float>(curr_time) / FADING_OUT_DURATION, 0.0f, 1.0f);
		if (m_current_fade_opacity <= 0.0f) {
			m_state = EState::Finished;
			return true;
		} else if (next_render <= 20) {
			m_next_render = FADING_OUT_TIMEOUT;
			return true;
		} else {
			m_next_render = next_render;
			return false;
		}
	}

	if (m_state == EState::Finished) {
		return true;
	}

	if (m_state == EState::ClosePending) {
		m_state = EState::Finished;
		return true;
	}
	return false;
}

NotificationManager::SlicingCompleteLargeNotification::SlicingCompleteLargeNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, bool large) :
	  NotificationManager::PopNotification(n, id_provider, evt_handler)
{
	set_large(large);
}
void NotificationManager::SlicingCompleteLargeNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	if (!m_is_large)
		PopNotification::render_text(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	else {
        ImVec2 win_size(win_size_x, win_size_y);
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
void NotificationManager::SlicingCompleteLargeNotification::set_print_info(const std::string &info)
{
	m_print_info = info;
	m_has_print_info = true;
	if (m_is_large)
		m_lines_count = 2;
}
void NotificationManager::SlicingCompleteLargeNotification::set_large(bool l)
{
	m_is_large = l;
	//FIXME this information should not be lost (change m_data?)
//	m_counting_down = !l;
	m_hypertext = l ? _u8L("Export G-Code.") : std::string();
	m_state = l ? EState::Shown : EState::Hidden;
	init();
}
//---------------ExportFinishedNotification-----------
void NotificationManager::ExportFinishedNotification::count_spaces()
{
	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	m_left_indentation = m_line_height;
	if (m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		float picture_width = ImGui::CalcTextSize(text.c_str()).x;
		m_left_indentation = picture_width + m_line_height / 2;
	}
	//TODO count this properly
	m_window_width_offset = m_left_indentation + m_line_height * (m_to_removable ? 6.f : 3.f);
	m_window_width = m_line_height * 25;
}

void NotificationManager::ExportFinishedNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	
	float       x_offset = m_left_indentation;
	std::string fulltext = m_text1 + m_hypertext; //+ m_text2;
	// Lines are always at least two and m_multiline is always true for ExportFinishedNotification.
	// First line has "Export Finished" text and than hyper text open folder.
	// Following lines are path to gcode.
    size_t last_end = 0;
	float starting_y = m_line_height / 2;//10;
	float shift_y = m_line_height;// -m_line_height / 20;
	for (size_t i = 0; i < m_lines_count; i++) {
		if (m_text1.size() >= m_endlines[i]) {
			std::string line = m_text1.substr(last_end, m_endlines[i] - last_end);
			last_end = m_endlines[i];
			if (m_text1.size() > m_endlines[i])
				last_end += (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
			ImGui::SetCursorPosX(x_offset);
			ImGui::SetCursorPosY(starting_y + i * shift_y);
			imgui.text(line.c_str());
			//hyperlink text
			if ( i == 0 )  {
				render_hypertext(imgui, x_offset + ImGui::CalcTextSize(line.c_str()).x + ImGui::CalcTextSize("   ").x, starting_y, _u8L("Open Folder."));
			}
		}
	}

}

void NotificationManager::ExportFinishedNotification::render_close_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	PopNotification::render_close_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	if(m_to_removable)
		render_eject_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
}

void NotificationManager::ExportFinishedNotification::render_eject_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::string button_text;
	button_text = ImGui::EjectButton;
	
    if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - m_line_height * 5.f, win_pos.y),
		ImVec2(win_pos.x - m_line_height * 2.5f, win_pos.y + win_size.y),
		true))
	{
		button_text = ImGui::EjectHoverButton;
		// tooltip
		long time_now = wxGetLocalTime();
		if (m_hover_time > 0 && m_hover_time < time_now) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
			ImGui::BeginTooltip();
			imgui.text(_u8L("Eject drive") + " " + GUI::shortkey_ctrl_prefix() + "T");
			ImGui::EndTooltip();
			ImGui::PopStyleColor();
		} 
		if (m_hover_time == 0)
			m_hover_time = time_now;
	} else 
		m_hover_time = 0;

	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 5.0f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, EjectDriveNotificationClickedEvent(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED));
		close();
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 4.625f);
	ImGui::SetCursorPosY(0);
	if (imgui.button("  ", m_line_height * 2.f, win_size.y))
	{
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, EjectDriveNotificationClickedEvent(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED));
		close();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
bool NotificationManager::ExportFinishedNotification::on_text_click()
{
	Notifications_Internal::open_folder(m_export_dir_path);
	return false;
}
//------ProgressBar----------------
void NotificationManager::ProgressBarNotification::init()
{
	PopNotification::init();
	//m_lines_count++;
	if(m_lines_count >= 2) {
		m_lines_count = 3;
		m_multiline = true;
		while (m_endlines.size() < 3)
			m_endlines.push_back(m_endlines.back());
	} else {
		m_lines_count = 2;
		m_endlines.push_back(m_endlines.back());
	}
	if(m_state == EState::Shown)
		m_state = EState::NotFading;
}


void NotificationManager::ProgressBarNotification::count_lines()
{
	std::string text		= m_text1 + " " + m_hypertext;
	size_t      last_end	= 0;
	m_lines_count			= 0;

	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		size_t next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			//next line is ended by '/n'
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		}
		else {
			// find next suitable endline
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset) {
				// more than one line till end
				size_t next_space = text.find_first_of(' ', last_end);
				if (next_space > 0) {
					size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
					while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset) {
						next_space = next_space_candidate;
						next_space_candidate = text.find_first_of(' ', next_space + 1);
					}
					// when one word longer than line. Or the last space is too early.
					if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset ||
						ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x < (m_window_width - m_window_width_offset) / 4 * 3
						) {
						float width_of_a = ImGui::CalcTextSize("a").x;
						int letter_count = (int)((m_window_width - m_window_width_offset) / width_of_a);
						while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset) {
							letter_count++;
						}
						m_endlines.push_back(last_end + letter_count);
						last_end += letter_count;
					}
					else {
						m_endlines.push_back(next_space);
						last_end = next_space + 1;
					}
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



void NotificationManager::ProgressBarNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	// line1 - we do not print any more text than what fits on line 1. Line 2 is bar.
	if (m_multiline) {
		// two lines text, one line bar
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(m_line_height / 4);
		imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(m_line_height + m_line_height / 4);
		std::string line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0), m_endlines[1] - m_endlines[0] - (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
		imgui.text(line.c_str());
		if (m_has_cancel_button)
			render_cancel_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
		render_bar(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	} else {
		//one line text, one line bar
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(/*win_size_y / 2 - win_size_y / 6 -*/ m_line_height / 4);
		imgui.text(m_text1.substr(0, m_endlines[0]).c_str());
		if (m_has_cancel_button)
			render_cancel_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
		render_bar(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	}

	
}
void NotificationManager::ProgressBarNotification::render_bar(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec4 orange_color			= ImVec4(.99f, .313f, .0f, 1.0f);
	ImVec4 gray_color			= ImVec4(.34f, .34f, .34f, 1.0f);
	ImVec2 lineEnd				= ImVec2(win_pos_x - m_window_width_offset, win_pos_y + win_size_y / 2 + (m_multiline ? m_line_height / 2 : 0));
	ImVec2 lineStart			= ImVec2(win_pos_x - win_size_x + m_left_indentation, win_pos_y + win_size_y / 2 + (m_multiline ? m_line_height / 2 : 0));
	ImVec2 midPoint				= ImVec2(lineStart.x + (lineEnd.x - lineStart.x) * m_percentage, lineStart.y);
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32((int)(gray_color.x * 255), (int)(gray_color.y * 255), (int)(gray_color.z * 255), (1.0f * 255.f)), m_line_height * 0.2f);
	ImGui::GetWindowDrawList()->AddLine(lineStart, midPoint, IM_COL32((int)(orange_color.x * 255), (int)(orange_color.y * 255), (int)(orange_color.z * 255), (1.0f * 255.f)), m_line_height * 0.2f);
}
//------PrintHostUploadNotification----------------
void NotificationManager::PrintHostUploadNotification::init()
{
	ProgressBarNotification::init();
	if (m_state == EState::NotFading && m_uj_state == UploadJobState::PB_COMPLETED)
		m_state = EState::Shown;
}
void NotificationManager::PrintHostUploadNotification::count_spaces()
{
	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	m_left_indentation = m_line_height;
	if (m_uj_state == UploadJobState::PB_ERROR) {
		std::string text;
		text = (m_data.level == NotificationLevel::ErrorNotification ? ImGui::ErrorMarker : ImGui::WarningMarker);
		float picture_width = ImGui::CalcTextSize(text.c_str()).x;
		m_left_indentation = picture_width + m_line_height / 2;
	}
	m_window_width_offset = m_line_height * 6; //(m_has_cancel_button ? 6 : 4);
	m_window_width = m_line_height * 25;
}
bool NotificationManager::PrintHostUploadNotification::push_background_color()
{

	if (m_uj_state == UploadJobState::PB_ERROR) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_state == EState::FadingOut, m_current_fade_opacity);
		return true;
	}
	return false;
}
void NotificationManager::PrintHostUploadNotification::set_percentage(float percent)
{
	m_percentage = percent;
	if (percent >= 1.0f) {
		m_uj_state = UploadJobState::PB_COMPLETED;
		m_has_cancel_button = false;
		init();
	} else if (percent < 0.0f) {
		error();
	} else {
		m_uj_state = UploadJobState::PB_PROGRESS;
		m_has_cancel_button = true;
	}
}
void NotificationManager::PrintHostUploadNotification::render_bar(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	std::string text;
	switch (m_uj_state) {
	case Slic3r::GUI::NotificationManager::PrintHostUploadNotification::UploadJobState::PB_PROGRESS:
	{
		ProgressBarNotification::render_bar(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
		float uploaded = m_file_size * m_percentage;
		std::stringstream stream;
		stream << std::fixed << std::setprecision(2) << (int)(m_percentage * 100) << "% - " << uploaded << " of " << m_file_size << "MB uploaded";
		text = stream.str();
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(win_size_y / 2 + win_size_y / 6 - (m_multiline ? 0 : m_line_height / 4));
		break;
	}
	case Slic3r::GUI::NotificationManager::PrintHostUploadNotification::UploadJobState::PB_ERROR:
		text = _u8L("ERROR");
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(win_size_y / 2 + win_size_y / 6 - (m_multiline ? m_line_height / 4 : m_line_height / 2));
		break;
	case Slic3r::GUI::NotificationManager::PrintHostUploadNotification::UploadJobState::PB_CANCELLED:
		text = _u8L("CANCELED");
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(win_size_y / 2 + win_size_y / 6 - (m_multiline ? m_line_height / 4 : m_line_height / 2));
		break;
	case Slic3r::GUI::NotificationManager::PrintHostUploadNotification::UploadJobState::PB_COMPLETED:
		text = _u8L("COMPLETED");
		ImGui::SetCursorPosX(m_left_indentation);
		ImGui::SetCursorPosY(win_size_y / 2 + win_size_y / 6 - (m_multiline ? m_line_height / 4 : m_line_height / 2));
		break;
	}
	
	imgui.text(text.c_str());

}
void NotificationManager::PrintHostUploadNotification::render_left_sign(ImGuiWrapper& imgui)
{
	if (m_uj_state == UploadJobState::PB_ERROR) {
		std::string text;
		text = ImGui::ErrorMarker;
		ImGui::SetCursorPosX(m_line_height / 3);
		ImGui::SetCursorPosY(m_window_height / 2 - m_line_height);
		imgui.text(text.c_str());
	}
}
void NotificationManager::PrintHostUploadNotification::render_cancel_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::string button_text;
	button_text = ImGui::CancelButton;

	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - m_line_height * 5.f, win_pos.y),
		ImVec2(win_pos.x - m_line_height * 2.5f, win_pos.y + win_size.y),
		true))
	{
		button_text = ImGui::CancelHoverButton;
		// tooltip
		long time_now = wxGetLocalTime();
		if (m_hover_time > 0 && m_hover_time < time_now) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
			ImGui::BeginTooltip();
			imgui.text(_u8L("Cancel upload") + " " + GUI::shortkey_ctrl_prefix() + "T");
			ImGui::EndTooltip();
			ImGui::PopStyleColor();
		}
		if (m_hover_time == 0)
			m_hover_time = time_now;
	}
	else
		m_hover_time = 0;

	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 5.0f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		wxGetApp().printhost_job_queue().cancel(m_job_id - 1);
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 4.625f);
	ImGui::SetCursorPosY(0);
	if (imgui.button("  ", m_line_height * 2.f, win_size.y))
	{
		wxGetApp().printhost_job_queue().cancel(m_job_id - 1);
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	
}
//------NotificationManager--------
NotificationManager::NotificationManager(wxEvtHandler* evt_handler) :
	m_evt_handler(evt_handler)
{
}
void NotificationManager::push_notification(const NotificationType type, int timestamp)
{
	auto it = std::find_if(basic_notifications.begin(), basic_notifications.end(),
		boost::bind(&NotificationData::type, boost::placeholders::_1) == type);
	assert(it != basic_notifications.end());
	if (it != basic_notifications.end())
		push_notification_data(*it, timestamp);
}
void NotificationManager::push_notification(const std::string& text, int timestamp)
{
	push_notification_data({ NotificationType::CustomNotification, NotificationLevel::RegularNotification, 10, text }, timestamp);
}

void NotificationManager::push_notification(NotificationType type,
                                            NotificationLevel level,
                                            const std::string& text,
                                            const std::string& hypertext,
                                            std::function<bool(wxEvtHandler*)> callback,
                                            int timestamp)
{
	int duration = 0;
	switch (level) {
	case NotificationLevel::RegularNotification: 	duration = 10; break;
	case NotificationLevel::ErrorNotification: 		break;
	case NotificationLevel::ImportantNotification: 	break;
	default:
		assert(false);
		return;
	}
    push_notification_data({ type, level, duration, text, hypertext, callback }, timestamp);
}
void NotificationManager::push_slicing_error_notification(const std::string& text)
{
	set_all_slicing_errors_gray(false);
	push_notification_data({ NotificationType::SlicingError, NotificationLevel::ErrorNotification, 0,  _u8L("ERROR:") + "\n" + text }, 0);
	close_notification_of_type(NotificationType::SlicingComplete);
}
void NotificationManager::push_slicing_warning_notification(const std::string& text, bool gray, ObjectID oid, int warning_step)
{
	NotificationData data { NotificationType::SlicingWarning, NotificationLevel::WarningNotification, 0,  _u8L("WARNING:") + "\n" + text };

	auto notification = std::make_unique<NotificationManager::SlicingWarningNotification>(data, m_id_provider, m_evt_handler);
	notification->object_id = oid;
	notification->warning_step = warning_step;
	if (push_notification_data(std::move(notification), 0)) {
		m_pop_notifications.back()->set_gray(gray);
	}
}
void NotificationManager::push_plater_error_notification(const std::string& text)
{
	push_notification_data({ NotificationType::PlaterError, NotificationLevel::ErrorNotification, 0,  _u8L("ERROR:") + "\n" + text }, 0);
}

void NotificationManager::close_plater_error_notification(const std::string& text)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterError && notification->compare_text(_u8L("ERROR:") + "\n" + text)) {
			notification->close();
		}
	}
}

void NotificationManager::push_plater_warning_notification(const std::string& text)
{
	// Find if was not hidden 
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterWarning && notification->compare_text(_u8L("WARNING:") + "\n" + text)) {
			if (notification->get_state() == PopNotification::EState::Hidden) {
				//dynamic_cast<PlaterWarningNotification*>(notification.get())->show();
				return;
			}
		}
	}

	NotificationData data{ NotificationType::PlaterWarning, NotificationLevel::WarningNotification, 0,  _u8L("WARNING:") + "\n" + text };

	auto notification = std::make_unique<NotificationManager::PlaterWarningNotification>(data, m_id_provider, m_evt_handler);
	push_notification_data(std::move(notification), 0);
	// dissaper if in preview
	apply_in_preview();
}

void NotificationManager::close_plater_warning_notification(const std::string& text)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterWarning && notification->compare_text(_u8L("WARNING:") + "\n" + text)) {
			dynamic_cast<PlaterWarningNotification*>(notification.get())->real_close();
		}
	}
}
void NotificationManager::set_all_slicing_errors_gray(bool g)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingError) {
			notification->set_gray(g);
		}
	}
}
void NotificationManager::set_all_slicing_warnings_gray(bool g)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingWarning) {
			notification->set_gray(g);
		}
	}
}
/*
void NotificationManager::set_slicing_warning_gray(const std::string& text, bool g)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingWarning && notification->compare_text(text)) {
			notification->set_gray(g);
		}
	}
}
*/
void NotificationManager::close_slicing_errors_and_warnings()
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingError || notification->get_type() == NotificationType::SlicingWarning) {
			notification->close();
		}
	}
}
void NotificationManager::close_slicing_error_notification(const std::string& text)
{
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingError && notification->compare_text(_u8L("ERROR:") + "\n" + text)) {
			notification->close();
		}
	}
}
void NotificationManager::push_slicing_complete_notification(int timestamp, bool large)
{
	std::string hypertext;
	int         time = 10;
    if (has_slicing_error_notification())
        return;
    if (large) {
		hypertext = _u8L("Export G-Code.");
		time = 0;
	}
    NotificationData data{ NotificationType::SlicingComplete, NotificationLevel::RegularNotification, time,  _u8L("Slicing finished."), hypertext,
                           [](wxEvtHandler* evnthndlr){
                               if (evnthndlr != nullptr)
                                   wxPostEvent(evnthndlr, ExportGcodeNotificationClickedEvent(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED));
                               return true;
                           }
    };
	push_notification_data(std::make_unique<NotificationManager::SlicingCompleteLargeNotification>(data, m_id_provider, m_evt_handler, large), timestamp);
}
void NotificationManager::set_slicing_complete_print_time(const std::string &info)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingComplete) {
			dynamic_cast<SlicingCompleteLargeNotification*>(notification.get())->set_print_info(info);
			break;
		}
	}
}
void NotificationManager::set_slicing_complete_large(bool large)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::SlicingComplete) {
			dynamic_cast<SlicingCompleteLargeNotification*>(notification.get())->set_large(large);
			break;
		}
	}
}
void NotificationManager::close_notification_of_type(const NotificationType type)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == type) {
			notification->close();
		}
	}
}
void NotificationManager::remove_slicing_warnings_of_released_objects(const std::vector<ObjectID>& living_oids)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications)
		if (notification->get_type() == NotificationType::SlicingWarning) {
			if (! std::binary_search(living_oids.begin(), living_oids.end(),
				static_cast<SlicingWarningNotification*>(notification.get())->object_id))
				notification->close();
		}
}
void NotificationManager::push_exporting_finished_notification(const std::string& path, const std::string& dir_path, bool on_removable)
{
	close_notification_of_type(NotificationType::ExportFinished);
	NotificationData data{ NotificationType::ExportFinished, NotificationLevel::RegularNotification, on_removable ? 0 : 20,  _u8L("Exporting finished.") + "\n" + path };
	push_notification_data(std::make_unique<NotificationManager::ExportFinishedNotification>(data, m_id_provider, m_evt_handler, on_removable, path, dir_path), 0);
}

void  NotificationManager::push_upload_job_notification(int id, float filesize, const std::string& filename, const std::string& host, float percentage)
{
	// find if upload with same id was not already in notification
	// done by compare_jon_id not compare_text thus has to be performed here
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PrintHostUpload && dynamic_cast<PrintHostUploadNotification*>(notification.get())->compare_job_id(id)) {
			return;
		}
	}
	std::string text = PrintHostUploadNotification::get_upload_job_text(id, filename, host);
	NotificationData data{ NotificationType::PrintHostUpload, NotificationLevel::ProgressBarNotification, 10, text };
	push_notification_data(std::make_unique<NotificationManager::PrintHostUploadNotification>(data, m_id_provider, m_evt_handler, 0, id, filesize), 0);
}
void NotificationManager::set_upload_job_notification_percentage(int id, const std::string& filename, const std::string& host, float percentage)
{
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PrintHostUpload) {
			PrintHostUploadNotification* phun = dynamic_cast<PrintHostUploadNotification*>(notification.get());
			if (phun->compare_job_id(id)) {
				phun->set_percentage(percentage);
				wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
				break;
			}
		}
	}
}
void NotificationManager::upload_job_notification_show_canceled(int id, const std::string& filename, const std::string& host)
{
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PrintHostUpload) {
			PrintHostUploadNotification* phun = dynamic_cast<PrintHostUploadNotification*>(notification.get());
			if (phun->compare_job_id(id)) {
				phun->cancel();
				wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
				break;
			}
		}
	}
}
void NotificationManager::upload_job_notification_show_error(int id, const std::string& filename, const std::string& host)
{
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PrintHostUpload) {
			PrintHostUploadNotification* phun = dynamic_cast<PrintHostUploadNotification*>(notification.get());
			if(phun->compare_job_id(id)) {
				phun->error();
				wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
				break;
			}
		}
	}
}
bool NotificationManager::push_notification_data(const NotificationData& notification_data, int timestamp)
{
	return push_notification_data(std::make_unique<PopNotification>(notification_data, m_id_provider, m_evt_handler), timestamp);
}
bool NotificationManager::push_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, int timestamp)
{
	// if timestamped notif, push only new one
	if (timestamp != 0) {
		if (m_used_timestamps.find(timestamp) == m_used_timestamps.end()) {
			m_used_timestamps.insert(timestamp);
		} else {
			return false;
		}
	}

	GLCanvas3D& canvas = *wxGetApp().plater()->get_current_canvas3D();

	if (this->activate_existing(notification.get())) {
		m_pop_notifications.back()->update(notification->get_data());
		canvas.schedule_extra_frame(0);
		return false;
	} else {
		m_pop_notifications.emplace_back(std::move(notification));
		canvas.schedule_extra_frame(0);
		return true;
	}
}

void NotificationManager::render_notifications(GLCanvas3D& canvas, float overlay_width)
{
	sort_notifications();
	
	float last_y = 0.0f;

	for (const auto& notification : m_pop_notifications) {
		if (notification->get_state() != PopNotification::EState::Hidden) {
			notification->render(canvas, last_y, m_move_from_overlay && !m_in_preview, overlay_width);
			if (notification->get_state() != PopNotification::EState::Finished)
				last_y = notification->get_top() + GAP_WIDTH;
		}
		
	}
	m_last_render = GLCanvas3D::timestamp_now();
}

bool NotificationManager::update_notifications(GLCanvas3D& canvas)
{
	// no update if not top window
	wxWindow* p = dynamic_cast<wxWindow*>(wxGetApp().plater());
	while (p->GetParent() != nullptr)
		p = p->GetParent();
	wxTopLevelWindow* top_level_wnd = dynamic_cast<wxTopLevelWindow*>(p);
	if (!top_level_wnd->IsActive())
		return false;

	// next_render() returns numeric_limits::max if no need for frame
	const int64_t max = std::numeric_limits<int64_t>::max();
	int64_t       next_render = max;
	const int64_t time_since_render = GLCanvas3D::timestamp_now() - m_last_render;
	bool		  request_render = false;
	// During render, each notification detects if its currently hovered and changes its state to EState::Hovered
	// If any notification is hovered, all restarts its countdown 
	bool          hover = false;
	for (const std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->is_hovered()) {
			hover = true;
			break;
		}
	}
	// update state of all notif and erase finished
	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end();) {
		std::unique_ptr<PopNotification>& notification = *it;
		request_render |= notification->update_state(hover, time_since_render);
		next_render = std::min<int64_t>(next_render, notification->next_render());
		if (notification->get_state() == PopNotification::EState::Finished)
			it = m_pop_notifications.erase(it);
		else 
			++it;
	}

	// request next frame in future
	if (next_render < max)
		canvas.schedule_extra_frame(int(next_render));

	return request_render;
}

void NotificationManager::sort_notifications()
{
	// Stable sorting, so that the order of equal ranges is stable.
	std::stable_sort(m_pop_notifications.begin(), m_pop_notifications.end(), [](const std::unique_ptr<PopNotification> &n1, const std::unique_ptr<PopNotification> &n2) {
		int n1l = (int)n1->get_data().level;
		int n2l = (int)n2->get_data().level;
		if (n1l == n2l && n1->is_gray() && !n2->is_gray())
			return true;
		return (n1l < n2l);
		});
}

bool NotificationManager::activate_existing(const NotificationManager::PopNotification* notification)
{
	NotificationType   new_type = notification->get_type();
	const std::string &new_text = notification->get_data().text1;
	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end(); ++it) {
		if ((*it)->get_type() == new_type && !(*it)->is_finished()) {
			if (std::find(m_multiple_types.begin(), m_multiple_types.end(), new_type) != m_multiple_types.end()) {
				// If found same type and same text, return true - update will be performed on the old notif
				if ((*it)->compare_text(new_text) == false) {
					continue;
				}
			} else if (new_type == NotificationType::SlicingWarning) {
				auto w1 = dynamic_cast<const SlicingWarningNotification*>(notification);
				auto w2 = dynamic_cast<const SlicingWarningNotification*>(it->get());
				if (w1 != nullptr && w2 != nullptr) {
					if (!(*it)->compare_text(new_text) || w1->object_id != w2->object_id) {
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
    for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
        if (notification->get_type() == NotificationType::PlaterWarning) 
            notification->hide(preview);
        if (notification->get_type() == NotificationType::SignDetected)
            notification->hide(!preview);
    }
}

bool NotificationManager::has_slicing_error_notification()
{
	return std::any_of(m_pop_notifications.begin(), m_pop_notifications.end(), [](auto &n) {
    	return n->get_type() == NotificationType::SlicingError;
    });
}

void NotificationManager::new_export_began(bool on_removable)
{
	close_notification_of_type(NotificationType::ExportFinished);
	// If we want to hold information of ejecting removable on later export finished notifications
	/*
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::ExportToRemovableFinished) {
			if (!on_removable) {
				const NotificationData old_data = notification->get_data();
				notification->update( {old_data.type, old_data.level ,old_data.duration, std::string(), old_data.hypertext} );
			} else {
				notification->close();
			}
			return;
		}
	}
	*/
}
void NotificationManager::device_ejected()
{
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::ExportFinished && dynamic_cast<ExportFinishedNotification*>(notification.get())->m_to_removable)
			notification->close();
	}
}

}//namespace GUI
}//namespace Slic3r
