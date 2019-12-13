#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <vector>
#include <string>

namespace Slic3r {
namespace GUI {
#if __APPLE__
class RDMMMWrapper;
#endif
    
struct DriveData
{
	std::string name;
	std::string path;
	DriveData(std::string n, std::string p):name(n),path(p){}
};
class RemovableDriveManager
{
#if __APPLE__
friend class RDMMMWrapper;
#endif
public:
	static RemovableDriveManager& get_instance()
	{
		static RemovableDriveManager instance; 
		return instance;
	}
	RemovableDriveManager(RemovableDriveManager const&) = delete;
	void operator=(RemovableDriveManager const&) = delete;
	//call only once. on apple register for unmnount callbacks. on windows register for device notification is prepared but not called (eject usb drive on widnows doesnt trigger the callback, sdc ard does), also enumerates devices for first time so init shoud be called on linux too.
	void init();
	//update() searches for removable devices, returns false if empty. /time = 0 is forced update, time expects wxGetLocalTime()
	bool update(const long time = 0,const bool check = false);  
	bool is_drive_mounted(const std::string &path);
	void eject_drive(const std::string &path);
	//returns path to last drive which was used, if none was used, returns device that was enumerated last
	std::string get_last_save_path();
	std::string get_last_save_name();
	//returns path to last drive which was used, if none was used, returns empty string
	std::string get_drive_path();
	std::vector<DriveData> get_all_drives();
	bool is_path_on_removable_drive(const std::string &path);
	// callback will notify only if device with last save path was removed
	void add_callback(std::function<void()> callback);
	// erases all callbacks added by add_callback()
	void erase_callbacks(); 
	// marks one of the eveices in vector as last used
	void set_last_save_path(const std::string &path);
	bool is_last_drive_removed();
	// param as update()
	bool is_last_drive_removed_with_update(const long time = 0);
	void set_is_writing(const bool b);
	bool get_is_writing();
	std::string get_drive_name(const std::string& path);
private:
    RemovableDriveManager();
	void search_for_drives();
	//triggers callbacks if last used drive was removed
	void check_and_notify();
	//returns drive path (same as path in DriveData) if exists otherwise empty string ""
	std::string get_drive_from_path(const std::string& path);
	void reset_last_save_path();

	std::vector<DriveData> m_current_drives;
	std::vector<std::function<void()>> m_callbacks;
	size_t m_drives_count;
	long m_last_update;
	std::string m_last_save_path;
	std::string m_last_save_name;
	bool m_is_writing;//on device

#if _WIN32
	//registers for notifications by creating invisible window
	void register_window();
#else
#if __APPLE__
	RDMMMWrapper * m_rdmmm;
 #endif
    void search_path(const std::string &path, const std::string &parent_path);
    bool compare_filesystem_id(const std::string &path_a, const std::string &path_b);
    void inspect_file(const std::string &path, const std::string &parent_path);
#endif
};
// apple wrapper for RemovableDriveManagerMM which searches for drives and/or ejects them    
#if __APPLE__
class RDMMMWrapper
{
public:
    RDMMMWrapper();
    ~RDMMMWrapper();
    void register_window();
    void list_devices();
    void log(const std::string &msg);
protected:
    void *m_imp;
    //friend void RemovableDriveManager::inspect_file(const std::string &path, const std::string &parent_path);
};
#endif
}}
#endif

