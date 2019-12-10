#import <Cocoa/Cocoa.h>

@interface RemovableDriveManagerMM : NSObject

-(instancetype) init;
-(void) add_unmount_observer;
-(void) on_device_unmount: (NSNotification*) notification;
-(void) list_dev;
@end
