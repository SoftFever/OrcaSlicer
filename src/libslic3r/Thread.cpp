#ifdef _WIN32
	#include <windows.h>
	#include <processthreadsapi.h>
	#include <boost/nowide/convert.hpp>
#else
	// any posix system
	#include <pthread.h>
#endif

#include <condition_variable>
#include <mutex>
#include <tbb/parallel_for.h>
#include <tbb/tbb_thread.h>

#define SLIC3R_THREAD_NAME_WIN32_MODERN

#include "Thread.hpp"

namespace Slic3r {

#ifdef _WIN32
#ifdef SLIC3R_THREAD_NAME_WIN32_MODERN

	static void WindowsSetThreadName(HANDLE hThread, const char *thread_name)
	{
		size_t len = strlen(thread_name);
		if (len < 1024) {
			// Allocate the temp string on stack.
			wchar_t buf[1024];
			::SetThreadDescription(hThread, boost::nowide::widen(buf, 1024, thread_name));
		} else {
			// Allocate dynamically.
			::SetThreadDescription(hThread, boost::nowide::widen(thread_name).c_str());
		}
	}

#else // SLIC3R_THREAD_NAME_WIN32_MODERN
	// use the old way by throwing an exception

	const DWORD MS_VC_EXCEPTION=0x406D1388;

	#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
	   DWORD dwType; // Must be 0x1000.
	   LPCSTR szName; // Pointer to name (in user addr space).
	   DWORD dwThreadID; // Thread ID (-1=caller thread).
	   DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
	#pragma pack(pop)
	static void WindowsSetThreadName(HANDLE hThread, const char *thread_name)
	{
	   THREADNAME_INFO info;
	   info.dwType = 0x1000;
	   info.szName = threadName;
	   info.dwThreadID = ::GetThreadId(hThread);
	   info.dwFlags = 0;

	   __try
	   {
	      RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	   }
	   __except(EXCEPTION_EXECUTE_HANDLER)
	   {
	   }
	}

#endif // SLIC3R_THREAD_NAME_WIN32_MODERN

// posix
void set_thread_name(std::thread &thread, const char *thread_name)
{
   	WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

void set_thread_name(boost::thread &thread, const char *thread_name)
{
   	WindowsSetThreadName(static_cast<HANDLE>(thread.native_handle()), thread_name);
}

void set_current_thread_name(const char *thread_name)
{
    WindowsSetThreadName(::GetCurrentThread(), thread_name);
}

std::string get_current_thread_name() 
{
	wchar_t *ptr = nullptr;
	::GetThreadDescription(::GetCurrentThread(), &ptr);
	return (ptr == nullptr) ? std::string() : boost::nowide::narrow(ptr);
}

#else // _WIN32

#ifdef __APPLE__

// Appe screwed the Posix norm.
void set_thread_name(std::thread &thread, const char *thread_name)
{
// not supported
//   	pthread_setname_np(thread.native_handle(), thread_name);
	throw CriticalException("Not supported");
}

void set_thread_name(boost::thread &thread, const char *thread_name)
{
// not supported	
//   	pthread_setname_np(thread.native_handle(), thread_name);
	throw CriticalException("Not supported");
}

void set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(thread_name);
}

std::string get_current_thread_name()
{
	char buf[16];
	return std::string(thread_getname_np(buf, 16) == 0 ? buf : "");
}

#else

// posix
void set_thread_name(std::thread &thread, const char *thread_name)
{
   	pthread_setname_np(thread.native_handle(), thread_name);
}

void set_thread_name(boost::thread &thread, const char *thread_name)
{
   	pthread_setname_np(thread.native_handle(), thread_name);
}

void set_current_thread_name(const char *thread_name)
{
	pthread_setname_np(pthread_self(), thread_name);
}

std::string get_current_thread_name()
{
	char buf[16];
	return std::string(pthread_getname_np(pthread_self(), buf, 16) == 0 ? buf : "");
}

#endif

#endif // _WIN32

// Spawn (n - 1) worker threads on Intel TBB thread pool and name them by an index and a system thread ID.
void name_tbb_thread_pool_threads()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	const size_t nthreads_hw = std::thread::hardware_concurrency();
	size_t 		 nthreads    = nthreads_hw;

#ifdef SLIC3R_PROFILE
	// Shiny profiler is not thread safe, thus disable parallelization.
	nthreads = 1;
#endif

	if (nthreads != nthreads_hw) 
		new tbb::task_scheduler_init(nthreads);

	std::atomic<size_t>		nthreads_running(0);
	std::condition_variable cv;
	std::mutex				cv_m;
	auto					master_thread_id = tbb::this_tbb_thread::get_id();
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, nthreads, 1),
        [&nthreads_running, nthreads, &master_thread_id, &cv, &cv_m](const tbb::blocked_range<size_t> &range) {
        	assert(range.begin() + 1 == range.end());
        	if (nthreads_running.fetch_add(1) + 1 == nthreads) {
        		// All threads are spinning.
        		// Wake them up.
    			cv.notify_all();
        	} else {
        		// Wait for the last thread to wake the others.
				std::unique_lock<std::mutex> lk(cv_m);
			    cv.wait(lk, [&nthreads_running, nthreads]{return nthreads_running == nthreads;});
        	}
        	auto thread_id = tbb::this_tbb_thread::get_id();
			if (thread_id == master_thread_id) {
				// The calling thread runs the 0'th task.
				assert(range.begin() == 0);
			} else {
				assert(range.begin() > 0);
				std::ostringstream name;
		        name << "slic3r_tbb_" << range.begin();
		        set_current_thread_name(name.str().c_str());
    		}
        });
}

}
