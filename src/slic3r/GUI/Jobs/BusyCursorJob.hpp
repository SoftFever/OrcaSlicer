#ifndef BUSYCURSORJOB_HPP
#define BUSYCURSORJOB_HPP

#include "JobNew.hpp"

#include <wx/utils.h>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

struct CursorSetterRAII
{
    JobNew::Ctl &ctl;
    CursorSetterRAII(JobNew::Ctl &c) : ctl{c}
    {
        ctl.call_on_main_thread([] { wxBeginBusyCursor(); });
    }
    ~CursorSetterRAII()
    {
        try {
            ctl.call_on_main_thread([] { wxEndBusyCursor(); });
        } catch(...) {
            BOOST_LOG_TRIVIAL(error) << "Can't revert cursor from busy to normal";
        }
    }
};

template<class JobSubclass>
class BusyCursored : public JobNew
{
    JobSubclass m_job;

public:
    template<class... Args>
    BusyCursored(Args &&...args) : m_job{std::forward<Args>(args)...}
    {}

    void process(Ctl &ctl) override
    {
        CursorSetterRAII cursor_setter{ctl};
        m_job.process(ctl);
    }

    void finalize(bool canceled, std::exception_ptr &eptr) override
    {
        m_job.finalize(canceled, eptr);
    }
};


}
}

#endif // BUSYCURSORJOB_HPP
