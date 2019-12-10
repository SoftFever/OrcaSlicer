#include "RemovableDriveManager.hpp"
#include <iostream>
#include "boost/nowide/convert.hpp"

#if _WIN32
#include <windows.h>
#include <tchar.h>
#include <winioctl.h>
#include <shlwapi.h>

#include <Dbt.h>
GUID WceusbshGUID = { 0x25dbce51, 0x6c8f, 0x4a72,
					  0x8a,0x6d,0xb5,0x4c,0x2b,0x4f,0xc8,0x35 };

#else
//linux includes
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <libgen.h>
#include <pwd.h>
#endif

namespace Slic3r {
namespace GUI { 
//std::vector<DriveData>  RemovableDriveManager::m_current_drives;
//std::vector<std::function<void()>>  RemovableDriveManager::m_callbacks;


#if _WIN32
INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void RemovableDriveManager::search_for_drives()
{
	m_current_drives.clear();
	m_current_drives.reserve(26);
	DWORD drives_mask = GetLogicalDrives();
	for (size_t i = 0; i < 26; i++)
	{
		if(drives_mask & (1 << i))
		{
			std::string path (1,(char)('A' + i));
			path+=":";
			UINT drive_type = GetDriveTypeA(path.c_str());
			//std::cout << "found drive" << (char)('A' + i) << ": type:" <<driveType << "\n";
			if (drive_type ==  DRIVE_REMOVABLE)
			{
				// get name of drive
				std::wstring wpath = std::wstring(path.begin(), path.end());
				std::wstring volume_name;
				volume_name.resize(1024);
				std::wstring file_system_name;
				file_system_name.resize(1024);
				LPWSTR  lp_volume_name_buffer = new wchar_t;
				BOOL error = GetVolumeInformationW(wpath.c_str(), &volume_name[0], sizeof(volume_name), NULL, NULL, NULL, &file_system_name[0], sizeof(file_system_name));
				if(error != 0)
				{
					if (volume_name == L"")
					{
						volume_name = L"REMOVABLE DRIVE";
					}
					if (file_system_name != L"")
					{
						ULARGE_INTEGER free_space;
						GetDiskFreeSpaceExA(path.c_str(), &free_space, NULL, NULL);
						//std::cout << std::string(volumeName.begin(), volumeName.end()) << " " << std::string(fileSystemName.begin(), fileSystemName.end()) << " " << freeSpace.QuadPart << "\n";
						if (free_space.QuadPart > 0)
						{
							path += "\\";
							m_current_drives.push_back(DriveData(boost::nowide::narrow(volume_name), path));
						}
					}
				}
			}
		}
	}
	//std::cout << "found drives:" << m_current_drives.size() << "\n";
}
void RemovableDriveManager::eject_drive(const std::string &path)
{

	//if (!update() || !is_drive_mounted(path))
	if(m_current_drives.empty())
		return;
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if ((*it).path == path)
		{
			std::string mpath = "\\\\.\\" + path;
			mpath = mpath.substr(0, mpath.size() - 1);
			//std::cout << "Ejecting " << mpath << "\n";
			HANDLE handle = CreateFileA(mpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				std::cerr << "Ejecting " << mpath << " failed " << GetLastError() << " \n";
				return;
			}
			DWORD deviceControlRetVal(0);
			BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
			CloseHandle(handle);
			if (error == 0)
			{
				std::cerr << "Ejecting " << mpath << " failed " << deviceControlRetVal << " " << GetLastError() << " \n";
				return;
			}


			m_current_drives.erase(it);
			break;
		}
	}
}
bool RemovableDriveManager::is_path_on_removable_drive(const std::string &path)
{
	if (m_current_drives.empty())
		return false;
	int letter = PathGetDriveNumberA(path.c_str());
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		char drive = (*it).path[0];
		if (drive == ('A' + letter))
			return true;
	}
	return false;
}
std::string RemovableDriveManager::get_drive_from_path(const std::string& path)
{
	int letter = PathGetDriveNumberA(path.c_str());
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		char drive = (*it).path[0];
		if (drive == ('A' + letter))
			return (*it).path;
	}
	return "";
}
void RemovableDriveManager::register_window()
{
	std::cout << "Registering for device notification\n";
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wndClass.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
	wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(WinProcCallback);
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hIcon = LoadIcon(0, IDI_APPLICATION);
	wndClass.hbrBackground = CreateSolidBrush(RGB(192, 192, 192));
	wndClass.hCursor = LoadCursor(0, IDC_ARROW);
	wndClass.lpszClassName = L"PrusaSlicer_aux_class";
	wndClass.lpszMenuName = NULL;
	wndClass.hIconSm = wndClass.hIcon;
	if(!RegisterClassEx(&wndClass))
	{
		DWORD err = GetLastError();
		return;
	}

	HWND hWnd = CreateWindowEx(
		WS_EX_NOACTIVATE,
		L"PrusaSlicer_aux_class",
		L"PrusaSlicer_aux_wnd",
		WS_DISABLED, // style
		CW_USEDEFAULT, 0,
		640, 480,
		NULL, NULL,
		GetModuleHandle(NULL),
		NULL);
	if(hWnd == NULL)
	{
		DWORD err = GetLastError();
	}
	//ShowWindow(hWnd, SW_SHOWNORMAL);
	UpdateWindow(hWnd);
}

INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT lRet = 1;
	static HDEVNOTIFY hDeviceNotify;

	static HWND hEditWnd;
	static ULONGLONG msgCount = 0;

	switch (message)
	{
	case WM_CREATE:
		DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

		ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
		NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
		NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		NotificationFilter.dbcc_classguid = WceusbshGUID;

		hDeviceNotify = RegisterDeviceNotification(
			hWnd,                       // events recipient
			&NotificationFilter,        // type of device
			DEVICE_NOTIFY_WINDOW_HANDLE // type of recipient handle
		);
		break;
	case WM_DEVICECHANGE:
	{
		if(wParam == DBT_DEVICEREMOVECOMPLETE)
		{
			std::cout << "WM_DEVICECHANGE\n";
			RemovableDriveManager::get_instance().update();
		}
	}
	break;
	default:
		// Send all other messages on to the default windows handler.
		lRet = DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}
	return lRet;
}

#else
void RemovableDriveManager::search_for_drives()
{
 
#if __APPLE__
	if(m_rdmmm)
	{
		m_rdmmm->list_devices();
	}
#else
	m_current_drives.clear();
	m_current_drives.reserve(26);

    //search /media/* folder
	search_path("/media/*", "/media");

/*
	//search /Volumes/* folder (OSX)
	search_path("/Volumes/*", "/Volumes");
*/
    std::string path(std::getenv("USER"));
	std::string pp(path);
	//std::cout << "user: "<< path << "\n";
	//if program is run with sudo, we have to search for all users 
	// but do we want that?
	/*
	if(path == "root"){ 
		while (true) {
	        passwd* entry = getpwent();
	        if (!entry) {
	            break;
	        }
	        path = entry->pw_name;
	        pp = path;
	        //search /media/USERNAME/* folder
			pp = "/media/"+pp;
			path = "/media/" + path + "/*";
			search_path(path, pp);

			//search /run/media/USERNAME/* folder
			path = "/run" + path;
			pp = "/run"+pp;
			search_path(path, pp);
	    }
	    endpwent();
	}else
	*/
	{
		//search /media/USERNAME/* folder
		pp = "/media/"+pp;
		path = "/media/" + path + "/*";
		search_path(path, pp);

		//search /run/media/USERNAME/* folder
		path = "/run" + path;
		pp = "/run"+pp;
		search_path(path, pp);

	}
	
	//std::cout << "found drives:" <<m_current_drives.size() << "\n";
#endif
}
void RemovableDriveManager::search_path(const std::string &path,const std::string &parent_path)
{
    glob_t globbuf;
	globbuf.gl_offs = 2;
	int error = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
	if(error == 0) 
	{
		for(size_t i = 0; i < globbuf.gl_pathc; i++)
		{
			inspect_file(globbuf.gl_pathv[i], parent_path);
		}
	}else
	{
		//if error - path probably doesnt exists so function just exits
		//std::cout<<"glob error "<< error<< "\n";
	}
	
	globfree(&globbuf);
}
void RemovableDriveManager::inspect_file(const std::string &path, const std::string &parent_path)
{
	//if not same file system - could be removable drive
	if(!compare_filesystem_id(path, parent_path))
	{
		//user id
		struct stat buf;
		stat(path.c_str(), &buf);
		uid_t uid = buf.st_uid;
		std::string username(std::getenv("USER"));
		struct passwd *pw = getpwuid(uid);
		if(pw != 0)
		{
			if(pw->pw_name == username)
			{
				std::string name = basename(const_cast<char*>(path.c_str()));
	       		m_current_drives.push_back(DriveData(name,path));
			}
		}
	}
}
bool RemovableDriveManager::compare_filesystem_id(const std::string &path_a, const std::string &path_b)
{
	struct stat buf;
	stat(path_a.c_str() ,&buf);
	dev_t id_a = buf.st_dev;
	stat(path_b.c_str() ,&buf);
	dev_t id_b = buf.st_dev;
	return id_a == id_b;
}
void RemovableDriveManager::eject_drive(const std::string &path)
{
	if (m_current_drives.empty())
		return;

	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if((*it).path == path)
		{
            
            std::string correct_path(path);
            for (size_t i = 0; i < correct_path.size(); ++i)
            {
            	if(correct_path[i]==' ')
            	{
            		correct_path = correct_path.insert(i,1,'\\');
            		i++;
            	}
            }
            std::cout<<"Ejecting "<<(*it).name<<" from "<< correct_path<<"\n";

            std::string command = "";
#if __APPLE__
            command = "diskutil unmount ";
#else
            command = "umount ";
#endif
            command += correct_path;
            int err = system(command.c_str());
            if(err)
            {
                std::cerr<<"Ejecting failed\n";
                return;
            }
            m_current_drives.erase(it);
            		
            break;
		}

	}

}
bool RemovableDriveManager::is_path_on_removable_drive(const std::string &path)
{
	if (m_current_drives.empty())
		return false;
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if(compare_filesystem_id(path, (*it).path))
			return true;
	}
	return false;
}
std::string RemovableDriveManager::get_drive_from_path(const std::string& path)
{
	//check if same filesystem
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if (compare_filesystem_id(path, (*it).path))
			return (*it).path;
	}
	return "";
}
#endif
void RemovableDriveManager::init()
{
	add_callback([](void) { RemovableDriveManager::get_instance().print(); });
#if _WIN32
	register_window();
#elif __APPLE__
    register_window();
#endif
	update();
}
bool RemovableDriveManager::update(const long time)
{
	if(time != 0) //time = 0 is forced update
	{
		long diff = m_last_update - time;
		if(diff <= -2)
		{
			m_last_update = time;
		}else
		{
			return false; // return value shouldnt matter if update didnt run
		}
	}
	search_for_drives();
	check_and_notify();
	return !m_current_drives.empty();
}


bool  RemovableDriveManager::is_drive_mounted(const std::string &path)
{
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if ((*it).path == path)
		{
			return true;
		}
	}
	return false;
}

std::string RemovableDriveManager::get_last_drive_path()
{
	if (!m_current_drives.empty())
	{
		if (m_last_save_path != "")
			return m_last_save_path;
		return m_current_drives.back().path;
	}
	return "";
}
std::vector<DriveData> RemovableDriveManager::get_all_drives()
{
	return m_current_drives;
}
void RemovableDriveManager::check_and_notify()
{
	//std::cout<<"drives count: "<<m_drives_count;
	if(m_drives_count != m_current_drives.size())
	{
		//std::cout<<" vs "<< m_current_drives.size();
		if(m_drives_count > m_current_drives.size() && m_last_save_path != "" && !is_drive_mounted(m_last_save_path))
		{
			for (auto it = m_callbacks.begin(); it != m_callbacks.end(); ++it)
			{
				(*it)();
			}
		}
		m_drives_count = m_current_drives.size();
	}
	//std::cout<<"\n";
}
void RemovableDriveManager::add_callback(std::function<void()> callback)
{
	m_callbacks.push_back(callback);
}
void  RemovableDriveManager::erase_callbacks()
{
	m_callbacks.clear();
}
void RemovableDriveManager::set_last_save_path(const std::string& path)
{
	std::string last_drive = get_drive_from_path(path);
	if(last_drive != "")
	{
		m_last_save_path = last_drive;
	}
}
bool RemovableDriveManager::is_last_drive_removed()
{
	if(m_last_save_path == "")
	{
		return true;
	}
	return !is_drive_mounted(m_last_save_path);
}
bool RemovableDriveManager::is_last_drive_removed_with_update(const long time)
{
	update(time);
	return is_last_drive_removed();
}
void RemovableDriveManager::reset_last_save_path()
{
	m_last_save_path = "";
}

void RemovableDriveManager::print()
{
	//std::cout << "Removed Device: "<<(int)is_last_drive_removed()<<"\n";
	std::cout << "notified\n";
}
}}//namespace Slicer::Gui::
