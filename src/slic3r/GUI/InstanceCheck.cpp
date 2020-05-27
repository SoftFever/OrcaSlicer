#include "GUI_App.hpp"
#include "InstanceCheck.hpp"
#include "Plater.hpp"

#ifdef _WIN32
  #include "MainFrame.hpp"
#endif

#include "libslic3r/Utils.hpp"
#include "libslic3r/Config.hpp"

#include "boost/nowide/convert.hpp"
#include <boost/log/trivial.hpp>
#include <iostream>
#include <unordered_map>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
#include <strsafe.h>
#endif //WIN32

#if __linux__
#include <dbus/dbus.h> /* Pull in all of D-Bus headers. */
#endif //__linux__

namespace Slic3r {
namespace instance_check_internal
{
	struct CommandLineAnalysis
	{
		bool           should_send;
		std::string    cl_string;
	};
	static CommandLineAnalysis process_command_line(int argc, char** argv)
	{
		CommandLineAnalysis ret { false };
		if (argc < 2)
			return ret;
		ret.cl_string = escape_string_cstyle(argv[0]);
		for (size_t i = 1; i < argc; ++i) {
			const std::string token = argv[i];
			if (token == "--single-instance" || token == "--single-instance=1") {
				ret.should_send = true;
			} else {
				ret.cl_string += " : ";
				ret.cl_string += escape_string_cstyle(token);
			}
		} 
		BOOST_LOG_TRIVIAL(debug) << "single instance: "<< ret.should_send << ". other params: " << ret.cl_string;
		return ret;
	}

	

#ifdef _WIN32

	static HWND l_prusa_slicer_hwnd;
	static BOOL CALLBACK EnumWindowsProc(_In_ HWND   hwnd, _In_ LPARAM lParam)
	{
		//checks for other instances of prusaslicer, if found brings it to front and return false to stop enumeration and quit this instance
		//search is done by classname(wxWindowNR is wxwidgets thing, so probably not unique) and name in window upper panel
		//other option would be do a mutex and check for its existence
		//BOOST_LOG_TRIVIAL(error) << "ewp: version: " << l_version_wstring;
		TCHAR 		 wndText[1000];
		TCHAR 		 className[1000];
		GetClassName(hwnd, className, 1000);
		GetWindowText(hwnd, wndText, 1000);
		std::wstring classNameString(className);
		std::wstring wndTextString(wndText);
		if (wndTextString.find(L"PrusaSlicer") != std::wstring::npos && classNameString == L"wxWindowNR") {
			//check if other instances has same instance hash
			//if not it is not same version(binary) as this version 
			HANDLE       handle = GetProp(hwnd, L"Instance_Hash_Minor");
			size_t       other_instance_hash = PtrToUint(handle);
			size_t       other_instance_hash_major;
			handle = GetProp(hwnd, L"Instance_Hash_Major");
			other_instance_hash_major = PtrToUint(handle);
			other_instance_hash_major = other_instance_hash_major << 32;
			other_instance_hash += other_instance_hash_major;
			size_t my_instance_hash = GUI::wxGetApp().get_instance_hash_int();
			if(my_instance_hash == other_instance_hash)
			{
				BOOST_LOG_TRIVIAL(debug) << "win enum - found correct instance";
				l_prusa_slicer_hwnd = hwnd;
				ShowWindow(hwnd, SW_SHOWMAXIMIZED);
				SetForegroundWindow(hwnd);
				return false;
			}
			BOOST_LOG_TRIVIAL(debug) << "win enum - found wrong instance";
		}
		return true;
	}
	static bool send_message(const std::string& message, const std::string &version)
	{
		if (!EnumWindows(EnumWindowsProc, 0)) {
			std::wstring wstr = boost::nowide::widen(message);
			//LPWSTR command_line_args = wstr.c_str();//GetCommandLine();
			LPWSTR command_line_args = new wchar_t[wstr.size() + 1];
			copy(wstr.begin(), wstr.end(), command_line_args);
			command_line_args[wstr.size()] = 0; 
			//Create a COPYDATASTRUCT to send the information
			//cbData represents the size of the information we want to send.
			//lpData represents the information we want to send.
			//dwData is an ID defined by us(this is a type of ID different than WM_COPYDATA).
			COPYDATASTRUCT data_to_send = { 0 };
			data_to_send.dwData = 1;
			data_to_send.cbData = sizeof(TCHAR) * (wcslen(command_line_args) + 1);
			data_to_send.lpData = command_line_args;

			SendMessage(l_prusa_slicer_hwnd, WM_COPYDATA, 0, (LPARAM)&data_to_send);
			return true;  
		}
	    return false;
	}

#else 

	static bool get_lock(const std::string& name, const std::string& path)
	{
		std::string dest_dir = path + name;
		BOOST_LOG_TRIVIAL(debug) <<"full lock path: "<< dest_dir;
		struct      flock fl;
		int         fdlock;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		fl.l_start = 0;
		fl.l_len = 1;
		if ((fdlock = open(dest_dir.c_str(), O_WRONLY | O_CREAT, 0666)) == -1)
			return true;

		if (fcntl(fdlock, F_SETLK, &fl) == -1)
			return true;

		return false;
	}

#endif //WIN32
#if defined(__APPLE__)

	static bool send_message(const std::string &message_text, const std::string &version)
	{
		//std::string v(version);
		//std::replace(v.begin(), v.end(), '.', '-');
		//if (!instance_check_internal::get_lock(v)) 
		{
			send_message_mac(message_text, version);
			return true;
		}
		return false;
	}

#elif defined(__linux__)

	static bool  send_message(const std::string &message_text, const std::string &version)
	{
		/*std::string v(version);
		std::replace(v.begin(), v.end(), '.', '-');
		if (!instance_check_internal::get_lock(v))*/
		/*auto checker = new wxSingleInstanceChecker;
		if ( !checker->IsAnotherRunning() ) */
		{
			DBusMessage* msg;
			DBusMessageIter args;
			DBusConnection* conn;
			DBusError 		err;
			dbus_uint32_t 	serial = 0;
			const char* sigval = message_text.c_str();
			//std::string		interface_name = "com.prusa3d.prusaslicer.InstanceCheck";
			std::string		interface_name = "com.prusa3d.prusaslicer.InstanceCheck.Object" + version;
			std::string   	method_name = "AnotherInstace";
			//std::string		object_name = "/com/prusa3d/prusaslicer/InstanceCheck";
			std::string		object_name = "/com/prusa3d/prusaslicer/InstanceCheck/Object" + version;


			// initialise the error value
			dbus_error_init(&err);

			// connect to bus, and check for errors (use SESSION bus everywhere!)
			conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
			if (dbus_error_is_set(&err)) {
				BOOST_LOG_TRIVIAL(error) << "DBus Connection Error. Message to another instance wont be send.";
				BOOST_LOG_TRIVIAL(error) << "DBus Connection Error: " << err.message;
				dbus_error_free(&err);
				return true;
			}
			if (NULL == conn) {
				BOOST_LOG_TRIVIAL(error) << "DBus Connection is NULL. Message to another instance wont be send.";
				return true;
			}

			//some sources do request interface ownership before constructing msg but i think its wrong.

			//create new method call message
			msg = dbus_message_new_method_call(interface_name.c_str(), object_name.c_str(), interface_name.c_str(), method_name.c_str());
			if (NULL == msg) {
				BOOST_LOG_TRIVIAL(error) << "DBus Message is NULL. Message to another instance wont be send.";
				dbus_connection_unref(conn);
				return true;
			}
			//the AnotherInstace method is not sending reply.
			dbus_message_set_no_reply(msg, TRUE);

			//append arguments to message
			if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &sigval, DBUS_TYPE_INVALID)) {
				BOOST_LOG_TRIVIAL(error) << "Ran out of memory while constructing args for DBus message. Message to another instance wont be send.";
				dbus_message_unref(msg);
				dbus_connection_unref(conn);
				return true;
			}

			// send the message and flush the connection
			if (!dbus_connection_send(conn, msg, &serial)) {
				BOOST_LOG_TRIVIAL(error) << "Ran out of memory while sending DBus message.";
				dbus_message_unref(msg);
				dbus_connection_unref(conn);
				return true;
			}
			dbus_connection_flush(conn);

			BOOST_LOG_TRIVIAL(trace) << "DBus message sent.";

			// free the message and close the connection
			dbus_message_unref(msg);
			dbus_connection_unref(conn);
			return true;
		}
		return false;
	}

#endif //__APPLE__/__linux__
} //namespace instance_check_internal

bool instance_check(int argc, char** argv, bool app_config_single_instance)
{	
	std::size_t hashed_path = std::hash<std::string>{}(boost::filesystem::system_complete(argv[0]).string());
	std::string lock_name 	= std::to_string(hashed_path);
	GUI::wxGetApp().set_instance_hash(hashed_path);
	BOOST_LOG_TRIVIAL(debug) <<"full path: "<< lock_name;
	instance_check_internal::CommandLineAnalysis cla = instance_check_internal::process_command_line(argc, argv);
#ifdef _WIN32
	GUI::wxGetApp().init_single_instance_checker(lock_name + ".lock", data_dir() + "/cache/");
	if ((cla.should_send || app_config_single_instance) && GUI::wxGetApp().single_instance_checker()->IsAnotherRunning()) {
#else // mac & linx
	if (instance_check_internal::get_lock(lock_name + ".lock", data_dir() + "/cache/") && (cla.should_send || app_config_single_instance)) {
#endif
		instance_check_internal::send_message(cla.cl_string, lock_name);
		BOOST_LOG_TRIVIAL(info) << "instance check: Another instance found. This instance will terminate.";
		return true;
	}
	BOOST_LOG_TRIVIAL(info) << "instance check: Another instance not found or single-instance not set.";
	return false;
}

namespace GUI {

wxDEFINE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDEFINE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);

void OtherInstanceMessageHandler::init(wxEvtHandler* callback_evt_handler)
{
	assert(!m_initialized);
	assert(m_callback_evt_handler == nullptr);
	if (m_initialized) 
		return;

	m_initialized = true;
	m_callback_evt_handler = callback_evt_handler;

#if defined(__APPLE__)
	this->register_for_messages(wxGetApp().get_instance_hash_string());
#endif //__APPLE__

#ifdef BACKGROUND_MESSAGE_LISTENER
	m_thread = boost::thread((boost::bind(&OtherInstanceMessageHandler::listen, this)));
#endif //BACKGROUND_MESSAGE_LISTENER
}
void OtherInstanceMessageHandler::shutdown(MainFrame* main_frame)
{
	BOOST_LOG_TRIVIAL(debug) << "message handler shutdown().";
	assert(m_initialized);
	if (m_initialized) {
#ifdef _WIN32
		HWND hwnd = main_frame->GetHandle();
		RemoveProp(hwnd, L"Instance_Hash_Minor");
		RemoveProp(hwnd, L"Instance_Hash_Major");
#endif //_WIN32
#if __APPLE__
		//delete macos implementation
		this->unregister_for_messages();
#endif //__APPLE__
#ifdef BACKGROUND_MESSAGE_LISTENER
		if (m_thread.joinable()) {
			// Stop the worker thread, if running.
			{
				// Notify the worker thread to cancel wait on detection polling.
				std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
				m_stop = true;
			}
			m_thread_stop_condition.notify_all();
			// Wait for the worker thread to stop.
			m_thread.join();
			m_stop = false;
		}
#endif //BACKGROUND_MESSAGE_LISTENER
		m_callback_evt_handler = nullptr;
		m_initialized = false;
	}
}

#ifdef _WIN32 
void OtherInstanceMessageHandler::init_windows_properties(MainFrame* main_frame, size_t instance_hash)
{
	size_t       minor_hash = instance_hash & 0xFFFFFFFF;
	size_t       major_hash = (instance_hash & 0xFFFFFFFF00000000) >> 32;
	HWND         hwnd = main_frame->GetHandle();
	HANDLE       handle_minor = UIntToPtr(minor_hash);
	HANDLE       handle_major = UIntToPtr(major_hash);
	SetProp(hwnd, L"Instance_Hash_Minor", handle_minor);
	SetProp(hwnd, L"Instance_Hash_Major", handle_major);
	//BOOST_LOG_TRIVIAL(debug) << "window properties initialized " << instance_hash << " (" << minor_hash << " & "<< major_hash;
}

#if 0

void OtherInstanceMessageHandler::print_window_info(HWND hwnd)
{
	std::wstring instance_hash = boost::nowide::widen(wxGetApp().get_instance_hash_string());
	TCHAR 		 wndText[1000];
	TCHAR 		 className[1000];
	GetClassName(hwnd, className, 1000);
	GetWindowText(hwnd, wndText, 1000);
	std::wstring classNameString(className);
	std::wstring wndTextString(wndText);
	HANDLE       handle = GetProp(hwnd, L"Instance_Hash_Minor");
	size_t       result = PtrToUint(handle);
	handle = GetProp(hwnd, L"Instance_Hash_Major");
	size_t       r2 = PtrToUint(handle);
	r2 = (r2 << 32);
	result += r2;
	BOOST_LOG_TRIVIAL(info) << "window info: " << result;
}
#endif //0
#endif  //WIN32
namespace MessageHandlerInternal
{
   // returns ::path to possible model or empty ::path if input string is not existing path
	static boost::filesystem::path get_path(std::string possible_path)
	{
		BOOST_LOG_TRIVIAL(debug) << "message part:" << possible_path;

		if (possible_path.empty() || possible_path.size() < 3) {
			BOOST_LOG_TRIVIAL(debug) << "empty";
			return boost::filesystem::path();
		}
		if (boost::filesystem::exists(possible_path)) {
			BOOST_LOG_TRIVIAL(debug) << "is path";
			return boost::filesystem::path(possible_path);
		} else if (possible_path[0] == '\"') {
			if(boost::filesystem::exists(possible_path.substr(1, possible_path.size() - 2))) {
				BOOST_LOG_TRIVIAL(debug) << "is path in quotes";
				return boost::filesystem::path(possible_path.substr(1, possible_path.size() - 2));
			}
		}
		BOOST_LOG_TRIVIAL(debug) << "is NOT path";
		return boost::filesystem::path();
	}
} //namespace MessageHandlerInternal

void OtherInstanceMessageHandler::handle_message(const std::string& message) {
	std::vector<boost::filesystem::path> paths;
	auto                                 next_space = message.find(" : ");
	size_t                               last_space = 0;
	int                                  counter    = 0;

	BOOST_LOG_TRIVIAL(info) << "message from other instance: " << message;

	while (next_space != std::string::npos)
	{	
		if (counter != 0) {
			std::string possible_path = message.substr(last_space, next_space - last_space);
			boost::filesystem::path p = MessageHandlerInternal::get_path(std::move(possible_path));
			if(!p.string().empty())
				paths.emplace_back(p);
		}
		last_space = next_space + 3;
		next_space = message.find(" : ", last_space);
		counter++;
	}
	if (counter != 0 ) {
		boost::filesystem::path p = MessageHandlerInternal::get_path(message.substr(last_space));
		if (!p.string().empty())
			paths.emplace_back(p);
	}
	if (!paths.empty()) {
		//wxEvtHandler* evt_handler = wxGetApp().plater(); //assert here?
		//if (evt_handler) {
			wxPostEvent(m_callback_evt_handler, LoadFromOtherInstanceEvent(GUI::EVT_LOAD_MODEL_OTHER_INSTANCE, std::vector<boost::filesystem::path>(std::move(paths))));
		//}
	}
}

#ifdef BACKGROUND_MESSAGE_LISTENER

namespace MessageHandlerDBusInternal
{
	//reply to introspect makes our DBus object visible for other programs like D-Feet
	static void respond_to_introspect(DBusConnection *connection, DBusMessage *request) 
	{
    	DBusMessage *reply;
	    const char  *introspection_data =
	        " <!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
	        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
	        " <!-- dbus-sharp 0.8.1 -->"
	        " <node>"
	        "   <interface name=\"org.freedesktop.DBus.Introspectable\">"
	        "     <method name=\"Introspect\">"
	        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
	        "     </method>"
	        "   </interface>"
	        "   <interface name=\"com.prusa3d.prusaslicer.InstanceCheck\">"
	        "     <method name=\"AnotherInstace\">"
	        "       <arg name=\"data\" direction=\"in\" type=\"s\" />"
	        "     </method>"
	        "   </interface>"
	        " </node>";
	     
	    reply = dbus_message_new_method_return(request);
	    dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
	    dbus_connection_send(connection, reply, NULL);
	    dbus_message_unref(reply);
	}
	//method AnotherInstance receives message from another PrusaSlicer instance 
	static void handle_method_another_instance(DBusConnection *connection, DBusMessage *request)
	{
	    DBusError     err;
	    char*         text= "";
		wxEvtHandler* evt_handler;

	    dbus_error_init(&err);
	    dbus_message_get_args(request, &err, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
	    if (dbus_error_is_set(&err)) {
	    	BOOST_LOG_TRIVIAL(trace) << "Dbus method AnotherInstance received with wrong arguments.";
	    	dbus_error_free(&err);
	        return;
	    }
	    wxGetApp().other_instance_message_handler()->handle_message(text);

		evt_handler = wxGetApp().plater();
		if (evt_handler) {
			wxPostEvent(evt_handler, InstanceGoToFrontEvent(EVT_INSTANCE_GO_TO_FRONT));
		}
	}
	//every dbus message received comes here
	static DBusHandlerResult handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data)
	{
		const char* interface_name = dbus_message_get_interface(message);
	    const char* member_name    = dbus_message_get_member(message);
	    std::string our_interface  = "com.prusa3d.prusaslicer.InstanceCheck.Object" + wxGetApp().get_instance_hash_string();
	    BOOST_LOG_TRIVIAL(trace) << "DBus message received: interface: " << interface_name << ", member: " << member_name;
	    if (0 == strcmp("org.freedesktop.DBus.Introspectable", interface_name) && 0 == strcmp("Introspect", member_name)) {		
	        respond_to_introspect(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("AnotherInstace", member_name)) {
	        handle_method_another_instance(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } 
	    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
} //namespace MessageHandlerDBusInternal

void OtherInstanceMessageHandler::listen()
{
    DBusConnection* 	 conn;
    DBusError 			 err;
    int 				 name_req_val;
    DBusObjectPathVTable vtable;
    std::string 		 instance_hash  = wxGetApp().get_instance_hash_string();
	std::string			 interface_name = "com.prusa3d.prusaslicer.InstanceCheck.Object" + instance_hash;
    std::string			 object_name 	= "/com/prusa3d/prusaslicer/InstanceCheck/Object" + instance_hash;

    //BOOST_LOG_TRIVIAL(debug) << "init dbus listen " << interface_name << " " << object_name;
    dbus_error_init(&err);

    // connect to the bus and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) { 
	    BOOST_LOG_TRIVIAL(error) << "DBus Connection Error: "<< err.message;
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
        dbus_error_free(&err); 
        return;
    }
    if (NULL == conn) { 
		BOOST_LOG_TRIVIAL(error) << "DBus Connection is NULL. Dbus Messages listening terminating.";
        return;
    }

	// request our name on the bus and check for errors
	name_req_val = dbus_bus_request_name(conn, interface_name.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
	if (dbus_error_is_set(&err)) {
	    BOOST_LOG_TRIVIAL(error) << "DBus Request name Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_error_free(&err); 
	    dbus_connection_unref(conn);
	    return;
	}
	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != name_req_val) {
		BOOST_LOG_TRIVIAL(error) << "Not primary owner of DBus name - probably another PrusaSlicer instance is running.";
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
	    return;
	}

	// Set callbacks. Unregister function should not be nessary.
	vtable.message_function = MessageHandlerDBusInternal::handle_dbus_object_message;
    vtable.unregister_function = NULL;

    // register new object - this is our access to DBus
    dbus_connection_try_register_object_path(conn, object_name.c_str(), &vtable, NULL, &err);
   	if ( dbus_error_is_set(&err) ) {
   		BOOST_LOG_TRIVIAL(error) << "DBus Register object Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
		dbus_error_free(&err);
		return;
	}

	BOOST_LOG_TRIVIAL(trace) << "Dbus object "<< object_name <<" registered. Starting listening for messages.";

	for (;;) {
		// Wait for 1 second 
		// Cancellable.
		{
			std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
			m_thread_stop_condition.wait_for(lck, std::chrono::seconds(1), [this] { return m_stop; });
		}
		if (m_stop)
			// Stop the worker thread.

			break;
		//dispatch should do all the work with incoming messages
		//second parameter is blocking time that funciton waits for new messages
		//that is handled here with our own event loop above
		dbus_connection_read_write_dispatch(conn, 0);
     }
     
   	 dbus_connection_unref(conn);
}
#endif //BACKGROUND_MESSAGE_LISTENER
} // namespace GUI
} // namespace Slic3r
