#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <vector>
#include <string>

namespace Slic3r {
namespace GUI {
class RDMMMWrapper;
#if __APPLE__

    
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
	void init();
	bool update(const long time = 0);  //time = 0 is forced update, time expects wxGetLocalTime()
	bool is_drive_mounted(const std::string &path);
	void eject_drive(const std::string &path);
	std::string get_last_drive_path();
	std::vector<DriveData> get_all_drives();
	bool is_path_on_removable_drive(const std::string &path);
	void add_callback(std::function<void()> callback); // callback will notify only if device with last save path was removed
	void erase_callbacks(); // erases all callbacks added by add_callback()
	void set_last_save_path(const std::string &path);
	bool is_last_drive_removed(); //if we dont need info about this drive, call reset_last_save_path();
	bool is_last_drive_removed_with_update(const long time = 0); // param as update()
	void reset_last_save_path();
	void print();
private:
#if __APPLE__
	RemovableDriveManager():m_drives_count(0),m_last_update(0),m_last_save_path(""),m_rdmmm(new RemovableDriveManagerMM()){}
#else
	RemovableDriveManager() : m_drives_count(0), m_last_update(0), m_last_save_path(""){}
#endif
	void search_for_drives();
	void check_and_notify();
	std::string get_drive_from_path(const std::string& path);//returns drive path (same as path in DriveData) if exists otherwise empty string ""
	std::vector<DriveData> m_current_drives;
	std::vector<std::function<void()>> m_callbacks;
	size_t m_drives_count;
	long m_last_update;
	std::string m_last_save_path;
#if _WIN32
	void register_window();
	//INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
#else
#if __APPLE__
	RDMMMWrapper * m_rdmmm;
 #endif
    void search_path(const std::string &path, const std::string &parent_path);
    void inspect_file(const std::string &path, const std::string &parent_path);
    bool compare_filesystem_id(const std::string &path_a, const std::string &path_b);
#endif
};
    class RDMMMWrapper
    {
    public:
        RDMMMWrapper();
        ~RDMMMWrapper();
        void register_window();
        void list_devices();
    private:
        void *m_imp;
        friend void RemovableDriveManager::inspect_file(const std::string &path, const std::string &parent_path);
    };
#endif
}}
#endif
