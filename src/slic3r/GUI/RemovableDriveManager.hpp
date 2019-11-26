#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <vector>
#include <string>

namespace Slic3r {
namespace GUI {
struct DriveData
{
	std::string name;
	std::string path;
	DriveData(std::string n, std::string p):name(n),path(p){}
};
class RemovableDriveManager
{
public:
	static RemovableDriveManager& getInstance()
	{
		static RemovableDriveManager instance; 
		return instance;
	}
	RemovableDriveManager(RemovableDriveManager const&) = delete;
	void operator=(RemovableDriveManager const&) = delete;
	
	//update() searches for removable devices, returns false if empty.
	static bool update(); 
	static bool isDriveMounted(std::string path);
	static void ejectDrive(std::string path);
	static std::string getLastDrivePath();
	static void getAllDrives(std::vector<DriveData>& drives);
private:
	RemovableDriveManager(){}
	static void searchForDrives(std::vector<DriveData>& newDrives);
	static void updateCurrentDrives(const std::vector<DriveData>& newDrives);
	static std::vector<DriveData> currentDrives;  
#if _WIN32
#else
	static void searchPath(std::vector<DriveData>& newDrives,const std::string path, const dev_t parentDevID);
#endif
};
}}
#endif