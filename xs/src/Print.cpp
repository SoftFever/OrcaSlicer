#include "Print.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

namespace Slic3r {

bool
PrintState::started(PrintStep step) const
{
    return this->_started.find(step) != this->_started.end();
}

bool
PrintState::done(PrintStep step) const
{
    return this->_done.find(step) != this->_done.end();
}

void
PrintState::set_started(PrintStep step)
{
    this->_started.insert(step);
}

void
PrintState::set_done(PrintStep step)
{
    this->_done.insert(step);
}

void
PrintState::invalidate(PrintStep step)
{
    this->_started.erase(step);
    this->_done.erase(step);
}

void
PrintState::invalidate_all()
{
    this->_started.clear();
    this->_done.clear();
}

#ifdef SLIC3RXS
REGISTER_CLASS(PrintState, "Print::State");
#endif

}
