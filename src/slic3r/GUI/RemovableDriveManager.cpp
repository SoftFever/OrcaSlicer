#include "RemovableDriveManager.hpp"

#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <stdio.h>

//#include <boost/log/trivial.hpp>
//#include "libslic3r/Utils.hpp"

DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE,
	0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

namespace Slic3r {
namespace GUI {

std::vector<DriveData> RemovableDriveManager::currentDrives;

bool RemovableDriveManager::update()
{
	searchForDrives(currentDrives);
	return !currentDrives.empty();
}
void RemovableDriveManager::searchForDrives(std::vector<DriveData>& newDrives)
{
	newDrives.clear();
	newDrives.reserve(26);
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
							newDrives.push_back(DriveData(volumeName, path));
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

void RemovableDriveManager::updateCurrentDrives(const std::vector<DriveData>& newDrives)
{
	currentDrives.clear();
	currentDrives.reserve(26);
	for (auto it = newDrives.begin(); it != newDrives.end(); ++it)
	{
		currentDrives.push_back(*it);
	}
}
void RemovableDriveManager::printDrivesToLog()
{
	//std::cout<<"current drives:"<< currentDrives.size() <<"\n";
	for (auto it = currentDrives.begin(); it != currentDrives.end(); ++it)
	{
		//BOOST_LOG_TRIVIAL(trace) << boost::format("found disk  %1%:") % ('A' + i);
		//std::cout << /*std::string((*it).name.begin(), (*it).name.end()) << "(" << */(*it).path << ":/, ";
	}
	//std::cout << "\n";
}
bool  RemovableDriveManager::isDriveMounted(std::string path)
{
	for (auto it = currentDrives.begin(); it != currentDrives.end(); ++it)
	{
		if ((*it).path == path)
		{
			return true;
		}
	}
	return false;
}
void RemovableDriveManager::ejectDrive(std::string path)
{
	if (!update() || !isDriveMounted(path))
		return;

	path = "\\\\.\\"+path;
	HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if(handle == INVALID_HANDLE_VALUE)
	{
		std::cerr << "Ejecting " << path << " failed " << GetLastError() << " \n";
		return;
	}
	DWORD deviceControlRetVal(0);
	BOOL error = DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0,nullptr , 0, &deviceControlRetVal, nullptr);
	CloseHandle(handle);
	if(error != 0)
		std::cout << "Ejected " << path << "\n";
	else
		std::cerr << "Ejecting " << path << " failed "<< deviceControlRetVal << " " << GetLastError() <<" \n";

	for (auto it = currentDrives.begin(); it != currentDrives.end(); ++it)
	{
		if ((*it).path == path)
		{
			currentDrives.erase(it);
			break;
		}
	}
}
std::string RemovableDriveManager::getLastDrivePath()
{
	if (!currentDrives.empty())
	{
		return currentDrives.back().path;
	}
	return "";
}
void RemovableDriveManager::getAllDrives(std::vector<DriveData>& drives)
{
	drives.clear();
	drives.reserve(26);
	for (auto it = currentDrives.begin(); it != currentDrives.end(); ++it)
	{
		drives.push_back(*it);
	}
}
}}