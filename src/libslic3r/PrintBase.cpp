#include "PrintBase.hpp"

namespace Slic3r
{

size_t PrintStateBase::g_last_timestamp = 0;

tbb::mutex& PrintObjectBase::state_mutex(PrintBase *print)
{ 
	return print->state_mutex();
}

std::function<void()> PrintObjectBase::cancel_callback(PrintBase *print)
{ 
	return print->cancel_callback();
}

} // namespace Slic3r
