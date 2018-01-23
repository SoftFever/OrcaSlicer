#include "TabIface.hpp"
#include "Tab.h"

namespace Slic3r {

void TabIface::load_current_preset() { m_tab->load_current_preset(); }
void TabIface::rebuild_page_tree() { m_tab->rebuild_page_tree(); }

}; // namespace Slic3r
