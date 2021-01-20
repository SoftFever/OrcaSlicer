#include "PlaterJob.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace Slic3r { namespace GUI {

void PlaterJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    }  catch (std::exception &e) {
       show_error(m_plater, _(L("An unexpected error occured: ")) + e.what());
    }
}

}} // namespace Slic3r::GUI
