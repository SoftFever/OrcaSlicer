#ifndef GUI_THREAD_HPP
#define GUI_THREAD_HPP

#include <utility>
#include <boost/thread.hpp>

namespace Slic3r {

template<class Fn>
inline boost::thread create_thread(boost::thread::attributes &attrs, Fn &&fn)
{
    // Duplicating the stack allocation size of Thread Building Block worker
    // threads of the thread pool: allocate 4MB on a 64bit system, allocate 2MB
    // on a 32bit system by default.
    
    attrs.set_stack_size((sizeof(void*) == 4) ? (2048 * 1024) : (4096 * 1024));
    return boost::thread{attrs, std::forward<Fn>(fn)};
}

template<class Fn> inline boost::thread create_thread(Fn &&fn)
{
    boost::thread::attributes attrs;
    return create_thread(attrs, std::forward<Fn>(fn));    
}

}

#endif // GUI_THREAD_HPP
