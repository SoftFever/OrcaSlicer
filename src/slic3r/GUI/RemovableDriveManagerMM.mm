#import "RemovableDriveManager.hpp"
#import "RemovableDriveManagerMM.h"
#import <AppKit/AppKit.h> 
#import <DiskArbitration/DiskArbitration.h>

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
    Slic3r::GUI::RemovableDriveManager::get_instance().update(0,true);
}
-(void) add_unmount_observer
{
    NSLog(@"add unmount observer");
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidUnmountNotification object:nil];
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector: @selector(on_device_unmount:) name:NSWorkspaceDidMountNotification object:nil];
}
-(NSArray*) list_dev
{
    // DEPRICATED:
    //NSArray* devices = [[NSWorkspace sharedWorkspace] mountedRemovableMedia];
	//return devices;
    
    NSArray *mountedRemovableMedia = [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:nil options:NSVolumeEnumerationSkipHiddenVolumes];
    NSMutableArray *result = [NSMutableArray array];
    for(NSURL *volURL in mountedRemovableMedia)
    {
        int                 err = 0;
        DADiskRef           disk;
        DASessionRef        session;
        CFDictionaryRef     descDict;
        session = DASessionCreate(NULL);
        if (session == NULL) {
            err = EINVAL;
        }
        if (err == 0) {
            disk = DADiskCreateFromVolumePath(NULL,session,(CFURLRef)volURL);
            if (session == NULL) {
                err = EINVAL;
            }
        }
        if (err == 0) {
            descDict = DADiskCopyDescription(disk);
            if (descDict == NULL) {
                err = EINVAL;
            }
        }
        if (err == 0) {
            CFTypeRef mediaEjectableKey = CFDictionaryGetValue(descDict,kDADiskDescriptionMediaEjectableKey);
            BOOL ejectable = [mediaEjectableKey boolValue];
            CFTypeRef deviceProtocolName = CFDictionaryGetValue(descDict,kDADiskDescriptionDeviceProtocolKey);
            CFTypeRef deviceModelKey = CFDictionaryGetValue(descDict, kDADiskDescriptionDeviceModelKey);
            if (mediaEjectableKey != NULL)
            {
                BOOL op = ejectable && (CFEqual(deviceProtocolName, CFSTR("USB")) || CFEqual(deviceModelKey, CFSTR("SD Card Reader")));
                //!CFEqual(deviceModelKey, CFSTR("Disk Image"));
                //
                if (op) {
                    [result addObject:volURL.path];
                }
            }
        }
        if (descDict != NULL) {
            CFRelease(descDict);
        }
        
        
    }
    return result;
}
-(void)eject_drive:(NSString *)path
{
    DADiskRef disk;
    DASessionRef session;
    NSURL *url = [[NSURL alloc] initFileURLWithPath:path];
    int err = 0;
    session = DASessionCreate(NULL);
    if (session == NULL) {
        err = EINVAL;
    }
    if (err == 0) {
        disk = DADiskCreateFromVolumePath(NULL,session,(CFURLRef)url);
    }
    if( err == 0)
    {
        DADiskUnmount(disk, kDADiskUnmountOptionDefault,
                      NULL, NULL);
    }
    if (disk != NULL) {
        CFRelease(disk);
    }
    if (session != NULL) {
        CFRelease(session);
    }
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
void RDMMMWrapper::log(const std::string &msg)
{
    NSLog(@"%s", msg.c_str());
}
void RDMMMWrapper::eject_device(const std::string &path)
{
    if(m_imp)
    {
        NSString * pth = [NSString stringWithCString:path.c_str()
                                            encoding:[NSString defaultCStringEncoding]];
        [m_imp eject_drive:pth];
    }
}
}}//namespace Slicer::GUI

/*

*/

@end
