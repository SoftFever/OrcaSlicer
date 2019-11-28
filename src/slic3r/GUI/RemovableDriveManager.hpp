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
	static RemovableDriveManager& get_instance()
	{
		static RemovableDriveManager instance; 
		return instance;
	}
	RemovableDriveManager(RemovableDriveManager const&) = delete;
	void operator=(RemovableDriveManager const&) = delete;
	
	//update() searches for removable devices, returns false if empty.
	bool update(long time = 0);  //time = 0 is forced update
	bool is_drive_mounted(const std::string &path);
	void eject_drive(const std::string &path);
	std::string get_last_drive_path();
	std::vector<DriveData> get_all_drives();
	bool is_path_on_removable_drive(const std::string &path);
	void add_callback(std::function<void()> callback);
	void print();
private:
	RemovableDriveManager(){}
	void search_for_drives();
	void check_and_notify();
	std::vector<DriveData> m_current_drives;
	std::vector<std::function<void()>> m_callbacks;
#if _WIN32
	void register_window();
#else
	void search_path(const std::string &path, const std::string &parent_path);
	bool compare_filesystem_id(const std::string &path_a, const std::string &path_b);
#endif
};
}}
#endif