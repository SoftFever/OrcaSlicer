#include "PrintBase.hpp"

namespace Slic3r
{

tbb::mutex& PrintObjectBase::cancel_mutex(PrintBase *print)
{ 
	return print->cancel_mutex();
}

std::function<void()> PrintObjectBase::cancel_callback(PrintBase *print)
{ 
	return print->cancel_callback();
}

} // namespace Slic3r
