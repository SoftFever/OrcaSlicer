#ifndef slic3r_GUI_HintNotification_hpp_
#define slic3r_GUI_HintNotification_hpp_

#include "NotificationManager.hpp"

namespace Slic3r {
namespace GUI {

// Database of hints updatable
struct HintData
{
	std::string        text;
	std::string        hypertext;
	std::string		   follow_text;
	std::string		   disabled_tags;
	std::string        enabled_tags;
	bool               runtime_disable; // if true - hyperlink will check before every click if not in disabled mode
	std::string        documentation_link;
	std::function<void(void)> callback { nullptr };
};

class HintDatabase
{
public:
    static HintDatabase& get_instance()
    {
        static HintDatabase    instance; // Guaranteed to be destroyed.
                                         // Instantiated on first use.
        return instance;
    }
private:
	HintDatabase()
		: m_hint_id(0)
	{}
public:
	HintDatabase(HintDatabase const&) = delete;
	void operator=(HintDatabase const&) = delete;

	// return true if HintData filled;
	HintData* get_hint(bool up = true);
	size_t    get_count() { 
		if (!m_initialized) 
			return 0;
		return m_loaded_hints.size(); 
	}
private:
	void	init();
	void	load_hints_from_file(const boost::filesystem::path& path);
	size_t						m_hint_id;
	bool						m_initialized { false };
	std::vector<HintData>       m_loaded_hints;

};
// Notification class - shows current Hint ("Did you know") 
class NotificationManager::HintNotification : public NotificationManager::PopNotification
{
public:
	HintNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler, bool new_hint)
		: PopNotification(n, id_provider, evt_handler)
	{
		retrieve_data(new_hint ? 0 : -1);
	}
	virtual void	init() override;
	void			open_next() { retrieve_data(0); }
protected:
	virtual void	set_next_window_size(ImGuiWrapper& imgui) override;
	virtual void	count_spaces() override;
	virtual void	count_lines() override;
	virtual bool	on_text_click() override;
	virtual void	render_text(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
	virtual void	render_close_button(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
	virtual void	render_minimize_button(ImGuiWrapper& imgui,
								const float win_pos_x, const float win_pos_y) override {}
	void			render_preferences_button(ImGuiWrapper& imgui,
								const float win_pos_x, const float win_pos_y);
	void			render_right_arrow_button(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y);
	void			render_documentation_button(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y);
	void			render_logo(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y);
	// recursion counter -1 tells to retrieve same hint as last time
	void			retrieve_data(int recursion_counter = 0);
	void			open_documentation();

	bool						m_has_hint_data { false };
	std::function<void(void)>	m_hypertext_callback;
	std::string					m_disabled_tags;
	std::string					m_enabled_tags;
	bool                        m_runtime_disable;
	std::string                 m_documentation_link;
	float						m_close_b_y { 0 };
	float						m_close_b_w { 0 };
	// hover of buttons
	long                      m_docu_hover_time { 0 };
	long                      m_prefe_hover_time{ 0 };
};

} //namespace Slic3r 
} //namespace GUI 

#endif //slic3r_GUI_HintNotification_hpp_