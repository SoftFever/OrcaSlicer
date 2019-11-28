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
	static bool update(long time = 0);  //time = 0 is forced update
	static bool is_drive_mounted(const std::string &path);
	static void eject_drive(const std::string &path);
	static std::string get_last_drive_path();
	static std::vector<DriveData> get_all_drives();
	static bool is_path_on_removable_drive(const std::string &path);
	static void add_callback(std::function<void()> callback);
	static void print();
private:
	RemovableDriveManager(){}
	static void search_for_drives();
	static void check_and_notify();
	static std::vector<DriveData> m_current_drives;
	static std::vector<std::function<void()>> m_callbacks;
#if _WIN32
	static void register_window();
#else
	static void search_path(const std::string &path, const dev_t &parentDevID);
#endif
};
}}
#endif