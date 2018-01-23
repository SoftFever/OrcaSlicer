namespace Slic3r {

namespace GUI {
	class Tab;
};

class TabIface {
public:
	TabIface() : m_tab(nullptr) {}
	TabIface(GUI::Tab *tab) : m_tab(tab) {}
//	TabIface(const TabIface &rhs) : m_tab(rhs.m_tab) {}
    void        load_current_preset();
    void        rebuild_page_tree();
protected:
	GUI::Tab   *m_tab;
};

}; // namespace Slic3r
