#include "NotificationManager.hpp"

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"
#include "ImGuiWrapper.hpp"

#include "wxExtensions.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/bind/placeholders.hpp>

#include <iostream>

#include <wx/glcanvas.h>

static constexpr float GAP_WIDTH = 10.0f;
static constexpr float SPACE_RIGHT_PANEL = 10.0f;
#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
static constexpr float FADING_OUT_DURATION = 2.0f;
// Time in Miliseconds after next render is requested
static constexpr int   FADING_OUT_TIMEOUT = 100;
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 

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
	, m_remaining_time      (n.duration)
	, m_last_remaining_time (n.duration)
	, m_counting_down       (n.duration != 0)
	, m_text1               (n.text1)
    , m_hypertext           (n.hypertext)
    , m_text2               (n.text2)
	, m_evt_handler         (evt_handler)
{
	//init();
}
#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
void NotificationManager::PopNotification::render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width)
{
	if (m_hidden) {
		m_top_y = initial_y - GAP_WIDTH;
		return;
	}

	Size cnv_size = canvas.get_canvas_size();
	ImGuiWrapper& imgui = *wxGetApp().imgui();
	ImVec2 mouse_pos = ImGui::GetMousePos();
	float right_gap = SPACE_RIGHT_PANEL + (move_from_overlay ? overlay_width + m_line_height * 5 : 0);

	if (m_line_height != ImGui::CalcTextSize("A").y)
		init();

	set_next_window_size(imgui);

	// top y of window
	m_top_y = initial_y + m_window_height;

	ImVec2 win_pos(1.0f * (float)cnv_size.get_width() - right_gap, 1.0f * (float)cnv_size.get_height() - m_top_y);
	imgui.set_next_window_pos(win_pos.x, win_pos.y, ImGuiCond_Always, 1.0f, 0.0f);
	imgui.set_next_window_size(m_window_width, m_window_height, ImGuiCond_Always);

	// find if hovered
	m_hovered = false;
	if (mouse_pos.x < win_pos.x && mouse_pos.x > win_pos.x - m_window_width && mouse_pos.y > win_pos.y && mouse_pos.y < win_pos.y + m_window_height) {
		ImGui::SetNextWindowFocus();
		m_hovered = true;
	}

	// color change based on fading out
	bool fading_pop = false;
	if (m_fading_out) {
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_fading_out, m_current_fade_opacity);
		Notifications_Internal::push_style_color(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text), m_fading_out, m_current_fade_opacity);
		fading_pop = true;
	}

	// background color
	if (m_is_gray) {
		ImVec4 backcolor(0.7f, 0.7f, 0.7f, 0.5f);
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	}
	else if (m_data.level == NotificationLevel::ErrorNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	}
	else if (m_data.level == NotificationLevel::WarningNotification) {
		ImVec4 backcolor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		backcolor.x += 0.3f;
		backcolor.y += 0.15f;
		Notifications_Internal::push_style_color(ImGuiCol_WindowBg, backcolor, m_fading_out, m_current_fade_opacity);
	}

	// name of window - probably indentifies window and is shown so last_end add whitespaces according to id
	if (m_id == 0)
		m_id = m_id_provider.allocate_id();
	std::string name = "!!Ntfctn" + std::to_string(m_id);
	if (imgui.begin(name, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
		ImVec2 win_size = ImGui::GetWindowSize();

		//FIXME: dont forget to us this for texts
		//GUI::format(_utf8(L()));

		/*
		//countdown numbers
		ImGui::SetCursorPosX(15);
		ImGui::SetCursorPosY(15);
		imgui.text(std::to_string(m_remaining_time).c_str());
		*/

		render_left_sign(imgui);
		render_text(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
		render_close_button(imgui, win_size.x, win_size.y, win_pos.x, win_pos.y);
		m_minimize_b_visible = false;
		if (m_multiline && m_lines_count > 3)
			render_minimize_button(imgui, win_pos.x, win_pos.y);
	}
	imgui.end();

	if (m_is_gray || m_data.level == NotificationLevel::ErrorNotification || m_data.level == NotificationLevel::WarningNotification)
		ImGui::PopStyleColor();

	if (fading_pop)
		ImGui::PopStyleColor(2);
}
#else
NotificationManager::PopNotification::RenderResult NotificationManager::PopNotification::render(GLCanvas3D& canvas, const float& initial_y, bool move_from_overlay, float overlay_width)
{
	if (!m_initialized) {
		init();
	}
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
	ImVec2          mouse_pos = ImGui::GetMousePos();
	float           right_gap = SPACE_RIGHT_PANEL + (move_from_overlay ? overlay_width + m_line_height * 5 : 0);

	if (m_line_height != ImGui::CalcTextSize("A").y)
		init();
	
	set_next_window_size(imgui);

	//top y of window
	m_top_y = initial_y + m_window_height;
	//top right position

	ImVec2 win_pos(1.0f * (float)cnv_size.get_width() - right_gap, 1.0f * (float)cnv_size.get_height() - m_top_y);
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
	if (! m_id)
		m_id = m_id_provider.allocate_id();
	std::string name;
	{
		// Create a unique ImGUI window name. The name may be recycled using a name of an already released notification.
		char buf[32];
		sprintf(buf, "!!Ntfctn%d", m_id);
		name = buf;
	}
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
			m_minimize_b_visible = false;
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
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
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
void NotificationManager::PopNotification::init()
{
	std::string text          = m_text1 + " " + m_hypertext;
	int         last_end      = 0;
	            m_lines_count = 0;

	count_spaces();
	
	// count lines
	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		int next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end > 0 && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			//next line is ended by '/n'
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		} else {
			// find next suitable endline
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset) {
				// more than one line till end
				int next_space = text.find_first_of(' ', last_end);
				if (next_space > 0) {
					int next_space_candidate = text.find_first_of(' ', next_space + 1);
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
					} else {
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
	if (m_lines_count == 3)
		m_multiline = true;
	m_initialized = true;
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
				if(i < m_lines_count - 1)
					last_end = m_endlines[i] + (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
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
			std::string line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0), m_endlines[1] - m_endlines[0] - (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
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
			std::string line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
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
			m_close_pending = on_text_click();
		}
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();

	//hover color
	ImVec4 orange_color = ImVec4(.99f, .313f, .0f, 1.0f);//ImGui::GetStyleColorVec4(ImGuiCol_Button);
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
		m_close_pending = true;
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.35f);
	ImGui::SetCursorPosY(0);
	if (imgui.button(" ", m_line_height * 2.125, win_size.y - ( m_minimize_b_visible ? 2 * m_line_height : 0)))
	{
		m_close_pending = true;
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
#if !ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
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
#endif // !ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
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
	Notifications_Internal::push_style_color(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_fading_out, m_current_fade_opacity);

	
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

#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
void NotificationManager::PopNotification::update_state()
{
	if (!m_initialized)
		init();

	if (m_hidden) {
		m_state = EState::Hidden;
		return;
	}

	if (m_hovered) {
		// reset fading
		m_fading_out = false;
		m_current_fade_opacity = 1.0f;
		m_remaining_time = m_data.duration;
	}

	if (m_counting_down) {
		if (m_fading_out && m_current_fade_opacity <= 0.0f)
			m_finished = true;
		else if (!m_fading_out && m_remaining_time == 0) {
			m_fading_out = true;
			m_fading_start = wxGetLocalTimeMillis();
		}
	}

	if (m_finished) {
		m_state = EState::Finished;
		return;
	}
	if (m_close_pending) {
		m_finished = true;
		m_state = EState::ClosePending;
		return;
	}
	if (m_fading_out) {
		if (!m_paused) {
			m_state = EState::FadingOutStatic;
			wxMilliClock_t curr_time      = wxGetLocalTimeMillis() - m_fading_start;
			wxMilliClock_t no_render_time = wxGetLocalTimeMillis() - m_last_render_fading;
			m_current_fade_opacity = std::clamp(1.0f - 0.001f * static_cast<float>(curr_time.GetValue()) / FADING_OUT_DURATION, 0.0f, 1.0f);
			if (no_render_time > FADING_OUT_TIMEOUT) {
				m_last_render_fading = wxGetLocalTimeMillis();
				m_state = EState::FadingOutRender;
			}
		}
		
	}
}
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 

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
	m_counting_down = !l;
	m_hypertext = l ? _u8L("Export G-Code.") : std::string();
	m_hidden = !l;
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
	
	ImVec2      win_size(win_size_x, win_size_y);
	ImVec2      win_pos(win_pos_x, win_pos_y);
	float       x_offset = m_left_indentation;
	std::string fulltext = m_text1 + m_hypertext; //+ m_text2;
	ImVec2      text_size = ImGui::CalcTextSize(fulltext.c_str());
	// Lines are always at least two and m_multiline is always true for ExportFinishedNotification.
	// First line has "Export Finished" text and than hyper text open folder.
	// Following lines are path to gcode.
	int last_end = 0;
	float starting_y = m_line_height / 2;//10;
	float shift_y = m_line_height;// -m_line_height / 20;
	for (size_t i = 0; i < m_lines_count; i++) {
		std::string line = m_text1.substr(last_end, m_endlines[i] - last_end);
		if (i < m_lines_count - 1)
			last_end = m_endlines[i] + (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
		ImGui::SetCursorPosX(x_offset);
		ImGui::SetCursorPosY(starting_y + i * shift_y);
		imgui.text(line.c_str());
		//hyperlink text
		if ( i == 0 )  {
			render_hypertext(imgui, x_offset + ImGui::CalcTextSize(m_text1.substr(0, last_end).c_str()).x + ImGui::CalcTextSize("   ").x, starting_y, _u8L("Open Folder."));
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
	ImVec4 orange_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
	orange_color.w = 0.8f;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	Notifications_Internal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_fading_out, m_current_fade_opacity);
	Notifications_Internal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_fading_out, m_current_fade_opacity);
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
			imgui.text(_u8L("Eject drive"));
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
		m_close_pending = true;
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 4.625f);
	ImGui::SetCursorPosY(0);
	if (imgui.button("  ", m_line_height * 2.f, win_size.y))
	{
		assert(m_evt_handler != nullptr);
		if (m_evt_handler != nullptr)
			wxPostEvent(m_evt_handler, EjectDriveNotificationClickedEvent(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED));
		m_close_pending = true;
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
	m_lines_count++;
	m_endlines.push_back(m_endlines.back());
}
void NotificationManager::ProgressBarNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	PopNotification::render_text(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	render_bar(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
}
void NotificationManager::ProgressBarNotification::render_bar(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	float bar_y = win_size_y / 2 - win_size_y / 6 + m_line_height;
	ImVec4 orange_color = ImVec4(.99f, .313f, .0f, 1.0f);
	float  invisible_length = 0;//((float)(m_data.duration - m_remaining_time) / (float)m_data.duration * win_size_x);
	//invisible_length -= win_size_x / ((float)m_data.duration * 60.f) * (60 - m_countdown_frame);
	ImVec2 lineEnd = ImVec2(win_pos_x - invisible_length - m_window_width_offset, win_pos_y + win_size_y/2 + m_line_height / 2);
	ImVec2 lineStart = ImVec2(win_pos_x - win_size_x + m_left_indentation, win_pos_y + win_size_y/2 + m_line_height / 2);
	ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, IM_COL32((int)(orange_color.x * 255), (int)(orange_color.y * 255), (int)(orange_color.z * 255), (1.0f * 255.f)), m_line_height * 0.7f);
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
void NotificationManager::push_plater_warning_notification(const std::string& text)
{
	push_notification_data({ NotificationType::PlaterWarning, NotificationLevel::WarningNotification, 0,  _u8L("WARNING:") + "\n" + text }, 0);
	// dissaper if in preview
	set_in_preview(m_in_preview);
}
void NotificationManager::close_plater_error_notification(const std::string& text)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterError && notification->compare_text(_u8L("ERROR:") + "\n" + text)) {
			notification->close();
		}
	}
}
void NotificationManager::close_plater_warning_notification(const std::string& text)
{
	for (std::unique_ptr<PopNotification> &notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::PlaterWarning && notification->compare_text(_u8L("WARNING:") + "\n" + text)) {
			notification->close();
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
	NotificationData data{ NotificationType::SlicingComplete, NotificationLevel::RegularNotification, time,  _u8L("Slicing finished."), hypertext, [](wxEvtHandler* evnthndlr){
		if (evnthndlr != nullptr) wxPostEvent(evnthndlr, ExportGcodeNotificationClickedEvent(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED)); return true; } };
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
void  NotificationManager::push_progress_bar_notification(const std::string& text, float percentage)
{
	NotificationData data{ NotificationType::ProgressBar, NotificationLevel::ProgressBarNotification, 0, text };
	push_notification_data(std::make_unique<NotificationManager::ProgressBarNotification>(data, m_id_provider, m_evt_handler, 0), 0);
}
void NotificationManager::set_progress_bar_percentage(const std::string& text, float percentage)
{
	bool found = false;
	for (std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->get_type() == NotificationType::ProgressBar && notification->compare_text(text)) {
			dynamic_cast<ProgressBarNotification*>(notification.get())->set_percentage(percentage);
			wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
			found = true;
		}
	}
	if (!found) {
		push_progress_bar_notification(text, percentage);
	}
}
bool NotificationManager::push_notification_data(const NotificationData& notification_data, int timestamp)
{
	return push_notification_data(std::make_unique<PopNotification>(notification_data, m_id_provider, m_evt_handler), timestamp);
}
bool NotificationManager::push_notification_data(std::unique_ptr<NotificationManager::PopNotification> notification, int timestamp)
{
#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
	m_requires_update = true;
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 

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
		canvas.request_extra_frame();
		return false;
	} else {
		m_pop_notifications.emplace_back(std::move(notification));
		canvas.request_extra_frame();
		return true;
	}
}
#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
void NotificationManager::render_notifications(float overlay_width)
{
	sort_notifications();

	GLCanvas3D& canvas = *wxGetApp().plater()->get_current_canvas3D();
	float last_y = 0.0f;

	for (const auto& notification : m_pop_notifications) {
		if (notification->get_state() != PopNotification::EState::Hidden) {
			notification->render(canvas, last_y, m_move_from_overlay && !m_in_preview, overlay_width);
			if (notification->get_state() != PopNotification::EState::Finished)
				last_y = notification->get_top() + GAP_WIDTH;
		}
		
	}
}
#else
void NotificationManager::render_notifications(float overlay_width)
{
	float    last_x = 0.0f;
	float    current_height = 0.0f;
	bool     request_next_frame = false;
	bool     render_main = false;
	bool     hovered = false;
	sort_notifications();

	GLCanvas3D& canvas = *wxGetApp().plater()->get_current_canvas3D();

	// iterate thru notifications and render them / erase them
	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end();) { 
		if ((*it)->is_finished()) {
			it = m_pop_notifications.erase(it);
		} else {
			(*it)->set_paused(m_hovered);
			PopNotification::RenderResult res = (*it)->render(canvas, last_x, m_move_from_overlay && !m_in_preview, overlay_width);
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

	{
		// Control the fade-out.
		// time in seconds
		long now = wxGetLocalTime();
		// Pausing fade-out when the mouse is over some notification.
		if (!m_hovered && m_last_time < now)
		{
			if (now - m_last_time == 1)
			{
				for (auto &notification : m_pop_notifications)
				{
					notification->substract_remaining_time();
				}
			}
			m_last_time = now;
		}
	}

	if (request_next_frame)
		//FIXME this is very expensive for fade-out control.
		// If any of the notifications is fading out, 100% of the CPU/GPU is consumed.
		canvas.request_extra_frame();
}
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 

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
			if (new_type == NotificationType::CustomNotification || new_type == NotificationType::PlaterWarning) {
				if (!(*it)->compare_text(new_text))
					continue;
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
    }
}

#if ENABLE_NEW_NOTIFICATIONS_FADE_OUT 
void NotificationManager::update_notifications()
{
	static size_t last_size = m_pop_notifications.size();

	for (auto it = m_pop_notifications.begin(); it != m_pop_notifications.end();) {
		std::unique_ptr<PopNotification>& notification = *it;
		if (notification->get_state() == PopNotification::EState::Finished)
			it = m_pop_notifications.erase(it);
		else {
			notification->set_paused(m_hovered);
			notification->update_state();
			++it;
		}
	}

	m_requires_update = false;
	for (const std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->requires_update()) {
			m_requires_update = true;
			break;
		}
	}

	// update hovering state
	m_hovered = false;
	for (const std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
		if (notification->is_hovered()) {
			m_hovered = true;
			break;
		}
	}

	// Reuire render if some notification was just deleted.
	size_t curr_size = m_pop_notifications.size();
	m_requires_render = m_hovered || (last_size != curr_size);
	last_size = curr_size;

	// Ask notification if it needs render
	if (!m_requires_render) {
		for (const std::unique_ptr<PopNotification>& notification : m_pop_notifications) {
			if (notification->requires_render()) {
				m_requires_render = true;
				break;
			}
		}
	}
	// Make sure there will be update after last notification erased
	if (m_requires_render)
		m_requires_update = true;

	// actualizate timers
	wxWindow* p = dynamic_cast<wxWindow*>(wxGetApp().plater());
	while (p->GetParent() != nullptr)
		p = p->GetParent();
	wxTopLevelWindow* top_level_wnd = dynamic_cast<wxTopLevelWindow*>(p);
	if (!top_level_wnd->IsActive())
		return;

	{
		// Control the fade-out.
		// time in seconds
		long now = wxGetLocalTime();
		// Pausing fade-out when the mouse is over some notification.
		if (!m_hovered && m_last_time < now) {
			if (now - m_last_time >= 1) {
				for (auto& notification : m_pop_notifications) {
					//if (notification->get_state() != PopNotification::EState::Static)
						notification->substract_remaining_time();
				}
			}
			m_last_time = now;
		}
	}
}
#endif // ENABLE_NEW_NOTIFICATIONS_FADE_OUT 

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
