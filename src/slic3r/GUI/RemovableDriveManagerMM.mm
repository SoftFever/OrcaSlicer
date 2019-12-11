#import "RemovableDriveManager.hpp"
#import "RemovableDriveManagerMM.h"
#import <AppKit/AppKit.h> 

@implementation RemovableDriveManagerMM



-(instancetype) init
{
	self = [super init];
	if(self)
	{        
	}
	return self;
}
-(void) on_device_unmount: (NSNotification*) notification
{
    NSLog(@"on device change");
    Slic3r::GUI::RemovableDriveManager::get_instance().update();
}
-(void) add_unmount_observer
{
    NSLog(@"add unmount observer");
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidUnmountNotification object:nil];
}
-(NSArray*) list_dev
{
    NSArray* devices = [[NSWorkspace sharedWorkspace] mountedRemovableMedia];
	return devices;
    
}
namespace Slic3r {
namespace GUI {
RDMMMWrapper::RDMMMWrapper():m_imp(nullptr){
	m_imp = [[RemovableDriveManagerMM alloc] init];
}
RDMMMWrapper::~RDMMMWrapper()
{
	if(m_imp)
	{
		[m_imp release];
	}
}
void  RDMMMWrapper::register_window()
{
	if(m_imp)
	{
		[m_imp add_unmount_observer];
	}
}
void  RDMMMWrapper::list_devices()
{
    if(m_imp)
    {
    	NSArray* devices = [m_imp list_dev];
    	for (NSString* volumePath in devices)
    	{
        	NSLog(@"%@", volumePath);
        	Slic3r::GUI::RemovableDriveManager::get_instance().inspect_file(std::string([volumePath UTF8String]), "/Volumes");
		}
    }
}
}}//namespace Slicer::GUI

/*

*/

@end
