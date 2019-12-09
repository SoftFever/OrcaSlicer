#import "RemovableDriveManager.hpp"

@implementation RemovableDriveManager

namespace Slic3r {
namespace GUI {

void RemovableDriveManager::register_window()
{
	//[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(volumesChanged:) name:NSWorkspaceDidMountNotification object: nil];
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidUnmountNotification object:nil];
}

-(void) on_device_unmount: (NSNotification*) notification
{
    NSLog(@"on device change");
    RemovableDriveManager::get_instance().update();
}

-(void) RemovableDriveManager::list_devices()
{
	NSLog(@"---");
	NSArray* devices = [[NSWorkspace sharedWorkspace] mountedRemovableMedia];
	for (NSString* volumePath in listOfMedia)
	{
		 NSLog(@"@", volumePath);
	}
	NSLog(@"--");
	//removable here means CD not USB :/
	NSArray* listOfMedia = [[NSWorkspace sharedWorkspace] mountedLocalVolumePaths];
	NSLog(@"%@", listOfMedia);

	for (NSString* volumePath in listOfMedia)
	{
	    BOOL isRemovable = NO;
	    BOOL isWritable  = NO;
	    BOOL isUnmountable = NO;
	    NSString* description = [NSString string];
	    NSString* type = [NSString string];

	    BOOL result = [[NSWorkspace sharedWorkspace] getFileSystemInfoForPath:volumePath 
	                                                             isRemovable:&isRemovable 
	                                                              isWritable:&isWritable 
	                                                              isUnmountable:&isUnmountable 
	                                                                description:&description
	                                                                       type:&type];
	    NSLog(@"Result:%i Volume: %@, Removable:%i, W:%i, Unmountable:%i, Desc:%@, type:%@", result, volumePath, isRemovable, isWritable, isUnmountable, description, type);
	}
}

}}//namespace Slicer::GUI