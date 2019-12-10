#import "RemovableDriveManager.hpp"

#import <AppKit/AppKit.h> 

@implementation RemovableDriveManagerMM

namespace Slic3r {
namespace GUI {

-(instancetype) init
{
	self = [super init];
	if(self)
	{
		[self add_unmount_observer]
	}
	return self;
}
-(void) on_device_unmount: (NSNotification*) notification
{
    NSLog(@"on device change");
    RemovableDriveManager::get_instance().update();
}
-(void) add_unmount_observer
{
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidUnmountNotification object:nil];
}

void RemovableDriveManager::register_window()
{
	m_rdmmm = nullptr;
	m_rdmmm = [[RemovableDriveManagerMM alloc] init];
}


/*
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
*/
}}//namespace Slicer::GUI