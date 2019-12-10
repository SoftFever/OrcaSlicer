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
    for (NSString* volumePath in devices)
    {
        NSLog(@"%@", volumePath);
	}
	return devices;
    
}
namespace Slic3r {
namespace GUI {
struct RemovableDriveManagerMMImpl{
	RemovableDriveManagerMM * wrap;
}
RemovableDriveManagerMM():impl(new RemovableDriveManagerMMImpl){
	impl->wrap = [[RemovableDriveManagerMM alloc] init];
}
RemovableDriveManagerMM::~RemovableDriveManagerMM()
{
	if(impl)
	{
		[impl->wrap release];
	}
}
void  RDMMMWrapper::register_window()
{
	if(impl->wrap)
	{
		[impl->wrap add_unmount_observer];
	}
}
void  RDMMMWrapper::list_devices()
{
    if(impl->wrap)
    {
    	NSArray* devices = [impl->wrap list_dev];
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
