#include "RemovableDriveManager.hpp"
<<<<<<< HEAD
<<<<<<< HEAD
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

#if _WIN32
INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void RemovableDriveManager::search_for_drives()
{
	m_current_drives.clear();
	//get logical drives flags by letter in alphabetical order
	DWORD drives_mask = GetLogicalDrives();
	for (size_t i = 0; i < 26; i++)
	{
		if(drives_mask & (1 << i))
		{
			std::string path (1,(char)('A' + i));
			path+=":";
			UINT drive_type = GetDriveTypeA(path.c_str());
			// DRIVE_REMOVABLE on W are sd cards and usb thumbnails (not usb harddrives)
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
					/*
					if (volume_name == L"")
					{
						volume_name = L"REMOVABLE DRIVE";
					}
					*/
					if (file_system_name != L"")
					{
						ULARGE_INTEGER free_space;
						GetDiskFreeSpaceExA(path.c_str(), &free_space, NULL, NULL);
						if (free_space.QuadPart > 0)
						{
							path += "\\";
							m_current_drives.push_back(DriveData(boost::nowide::narrow(volume_name), path));
=======



=======
>>>>>>> refactoring
#include <iostream>
#include <stdio.h>
#include "boost/nowide/convert.hpp"

#if _WIN32
#include <windows.h>
#include <tchar.h>
#include <winioctl.h>
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE,
	0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);
#else
//linux includes
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <glob.h>
#include <libgen.h>
#endif

namespace Slic3r {
namespace GUI {

std::vector<DriveData>  RemovableDriveManager::m_current_drives;

#if _WIN32
void RemovableDriveManager::search_for_drives()
{
	m_current_drives.clear();
	m_current_drives.reserve(26);
	DWORD drivesMask = GetLogicalDrives();
	for (size_t i = 0; i < 26; i++)
	{
		if(drivesMask & (1 << i))
		{
			std::string path (1,(char)('A' + i));
			path+=":";
			UINT driveType = GetDriveTypeA(path.c_str());
			//std::cout << "found drive" << (char)('A' + i) << ": type:" <<driveType << "\n";
			if (driveType ==  DRIVE_REMOVABLE)
			{
				// get name of drive
				std::wstring wpath = std::wstring(path.begin(), path.end());
				std::wstring volumeName;
				volumeName.resize(1024);
				std::wstring fileSystemName;
				fileSystemName.resize(1024);
				LPWSTR  lpVolumeNameBuffer = new wchar_t;
				BOOL error = GetVolumeInformationW(wpath.c_str(), &volumeName[0], sizeof(volumeName), NULL, NULL, NULL, &fileSystemName[0], sizeof(fileSystemName));
				if(error != 0)
				{
					if (volumeName == L"")
					{
						volumeName = L"REMOVABLE DRIVE";
					}
					if (fileSystemName != L"")
					{
						ULARGE_INTEGER freeSpace;
						GetDiskFreeSpaceExA(path.c_str(), &freeSpace, NULL, NULL);
						//std::cout << std::string(volumeName.begin(), volumeName.end()) << " " << std::string(fileSystemName.begin(), fileSystemName.end()) << " " << freeSpace.QuadPart << "\n";
						if (freeSpace.QuadPart > 0)
						{
<<<<<<< HEAD
							newDrives.push_back(DriveData(volumeName, path));
>>>>>>> removable drive manager - Windows part
=======
							m_current_drives.push_back(DriveData(boost::nowide::narrow(volumeName), path));
>>>>>>> refactoring
						}
					}
				}
			}
<<<<<<< HEAD
		}
	}
}
void RemovableDriveManager::eject_drive(const std::string &path)
{
	if(m_current_drives.empty())
		return;
	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if ((*it).path == path)
		{
			// get handle to device
			std::string mpath = "\\\\.\\" + path;
			mpath = mpath.substr(0, mpath.size() - 1);
			HANDLE handle = CreateFileA(mpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				std::cerr << "Ejecting " << mpath << " failed " << GetLastError() << " \n";
				return;
			}
			DWORD deviceControlRetVal(0);
			//these 3 commands should eject device safely but they dont, the device does disappear from file explorer but the "device was safely remove" notification doesnt trigger.
			//sd cards does  trigger WM_DEVICECHANGE messege, usb drives dont
			
			DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
			DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
			// some implemenatations also calls IOCTL_STORAGE_MEDIA_REMOVAL here but it returns error to me
			BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
			if (error == 0)
			{
				CloseHandle(handle);
				std::cerr << "Ejecting " << mpath << " failed " << deviceControlRetVal << " " << GetLastError() << " \n";
				return;
			}
			CloseHandle(handle);

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
	//creates new unvisible window that is recieving callbacks from system
	// structure to register 
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wndClass.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
	wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(WinProcCallback);//this is callback
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
	// here we need to catch messeges about device removal
	// problem is that when ejecting usb (how is it implemented above) there is no messege dispached. Only after physical removal of the device.
	//uncomment register_window() in init() to register and comment update() in GUI_App.cpp (only for windows!) to stop recieving periodical updates 
	LRESULT lRet = 1;
	static HDEVNOTIFY hDeviceNotify;

	switch (message)
	{
	case WM_CREATE:
		DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

		ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
		NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
		NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		NotificationFilter.dbcc_classguid = WceusbshGUID;

		hDeviceNotify = RegisterDeviceNotification(hWnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
		break;
	
	case WM_DEVICECHANGE:
	{
		// here is the important
		if(wParam == DBT_DEVICEREMOVECOMPLETE)
		{
-			RemovableDriveManager::get_instance().update(0, true);
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
    
    m_current_drives.clear();
    
#if __APPLE__
	// if on macos obj-c class will enumerate
	if(m_rdmmm)
	{
		m_rdmmm->list_devices();
	}
#else


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
	//confirms if the file is removable drive and adds it to vector

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
// there is no usable command in c++ so terminal command is used instead
// but neither triggers "succesful safe removal messege"
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

RemovableDriveManager::RemovableDriveManager():
    m_drives_count(0),
    m_last_update(0),
    m_last_save_path("")
#if __APPLE__
	, m_rdmmm(new RDMMMWrapper())
#endif
{}

void RemovableDriveManager::init()
{
	//add_callback([](void) { RemovableDriveManager::get_instance().print(); });
#if _WIN32
	//register_window();
#elif __APPLE__
    m_rdmmm->register_window();
#endif
	update();
}
bool RemovableDriveManager::update(const long time,const bool check)
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
	if(check)check_and_notify();
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
std::string RemovableDriveManager::get_drive_path()
{
	if (m_current_drives.size() == 0)
	{
		reset_last_save_path();
		return "";
	}
	if (m_last_save_path != "")
		return m_last_save_path;
	return m_current_drives.back().path;
}
std::string RemovableDriveManager::get_last_save_path()
{
	return m_last_save_path;
}
std::vector<DriveData> RemovableDriveManager::get_all_drives()
{
	return m_current_drives;
}
void RemovableDriveManager::check_and_notify()
{
	if(m_drives_count != m_current_drives.size())
	{
		if(m_callbacks.size() != 0 && m_drives_count > m_current_drives.size() && m_last_save_path != "" && !is_drive_mounted(m_last_save_path))
		{
			for (auto it = m_callbacks.begin(); it != m_callbacks.end(); ++it)
			{
				(*it)();
			}
		}
		m_drives_count = m_current_drives.size();
	}
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
	//std::cout<<"is last: "<<m_last_save_path;
	m_drives_count = m_current_drives.size();
	if(m_last_save_path == "")
	{
		//std::cout<<"\n";
		return true;
	}
	bool r = !is_drive_mounted(m_last_save_path);
	if (r) reset_last_save_path();
	//std::cout<<" "<< r <<"\n";
	return r;
}
bool RemovableDriveManager::is_last_drive_removed_with_update(const long time)
{
	update(time, false);
	return is_last_drive_removed();
}
void RemovableDriveManager::reset_last_save_path()
{
	m_last_save_path = "";
}
}}//namespace Slicer::Gui
=======
			else if(driveType == 3)//disks and usb drives
			{
			}
		}
	}
	
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
			HANDLE handle = CreateFileA(mpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				std::cerr << "Ejecting " << mpath << " failed " << GetLastError() << " \n";
				return;
			}
			DWORD deviceControlRetVal(0);
			BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &deviceControlRetVal, nullptr);
			CloseHandle(handle);
			if (error != 0)
				std::cout << "Ejected " << mpath << "\n";
			else
				std::cerr << "Ejecting " << mpath << " failed " << deviceControlRetVal << " " << GetLastError() << " \n";


			m_current_drives.erase(it);
			break;
		}
	}
}
#else
void RemovableDriveManager::search_for_drives()
{
    struct stat buf;
    std::string path(std::getenv("USER"));
	std::string pp(path);

	m_current_drives.clear();
	m_current_Drives.reserve(26);

    //search /media/* folder
    stat("/media/",&buf);
    std::cout << "/media ID: " <<buf.st_dev << "\n";
	search_path("/media/*", buf.st_dev);

	//search /media/USERNAME/* folder
	pp = "/media/"+pp;
	path = "/media/" + path + "/*";

	stat(pp.c_str() ,&buf);
    std::cout << pp <<" ID: " <<buf.st_dev << "\n";
	searchPath(path, buf.st_dev);

	//search /run/media/USERNAME/* folder
	path = "/run" + path;
	pp = "/run"+pp;
	stat(pp.c_str() ,&buf);
    std::cout << pp <<" ID: " <<buf.st_dev << "\n";
	searchPath(path, buf.st_dev);

	std::cout << "found drives:" <<newDrives.size() << "\n";
}
void RemovableDriveManager::search_path(const std::string &path,const dev_t &parentDevID)
{
    glob_t globbuf;
	globbuf.gl_offs = 2;
    std::cout<<"searching "<<path<<"\n";
	int error = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
	if(error)
	{
		std::cerr<<"glob error "<< error<< "\n";
	}
	for(size_t i = 0; i < globbuf.gl_pathc; i++)
	{
		std::cout<<globbuf.gl_pathv[i]<<"\n";
		//TODO check if mounted
		std::string name = basename(globbuf.gl_pathv[i]);
        std::cout<<name<<"\n";
        struct stat buf;
		stat(globbuf.gl_pathv[i],&buf);
		std::cout << buf.st_dev << "\n";
		if(buf.st_dev != parentDevID)// not same file system
		{
            m_current_drives.push_back(DriveData(name,globbuf.gl_pathv[i]));
		}
	}
	globfree(&globbuf);
}
void RemovableDriveManager::eject_drive(const std::string &path)
{
	if (m_current_drives.empty())
		return;

	for (auto it = m_current_drives.begin(); it != m_current_drives.end(); ++it)
	{
		if((*it).path == path)
		{
            std::cout<<"Ejecting "<<(*it).name<<" from "<< (*it).path<<"\n";
            int error = umount2(path.c_str(),MNT_DETACH);
            if(error)
            {
                int errsv = errno;
                std::cerr<<"Ejecting failed Error "<< errsv<<"\n";
            }
            m_current_drives.erase(it);
            break;
		}

	}

}
#endif
bool RemovableDriveManager::update()
{
	search_for_drives();
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
		return m_current_drives.back().path;
	}
	return "";
}
std::vector<DriveData> RemovableDriveManager::get_all_drives()
{
	return m_current_drives;
}
}}
>>>>>>> removable drive manager - Windows part
