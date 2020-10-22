#ifndef GUI_THREAD_HPP
#define GUI_THREAD_HPP

#include <utility>
#include <string>
#include <thread>
#include <boost/thread.hpp>

namespace Slic3r {

// Set / get thread name.
// pthread_setname_np supports maximum 15 character thread names! (16th character is the null terminator)
// Methods taking the thread as an argument are not supported by OSX.
void set_thread_name(std::thread &thread, const char *thread_name);
inline void set_thread_name(std::thread &thread, const std::string &thread_name) { set_thread_name(thread, thread_name.c_str()); }
void set_thread_name(boost::thread &thread, const char *thread_name);
inline void set_thread_name(boost::thread &thread, const std::string &thread_name) { set_thread_name(thread, thread_name.c_str()); }
void set_current_thread_name(const char *thread_name);
inline void set_current_thread_name(const std::string &thread_name) { set_current_thread_name(thread_name.c_str()); }

// Not supported by OSX.
std::string get_current_thread_name();

// To be called somewhere before the TBB threads are spinned for the first time, to
// give them names recognizible in the debugger.
void name_tbb_thread_pool_threads();

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
