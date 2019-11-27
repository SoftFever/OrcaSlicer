#include "RemovableDriveManager.hpp"
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
							m_current_drives.push_back(DriveData(boost::nowide::narrow(volumeName), path));
						}
					}
				}
			}
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