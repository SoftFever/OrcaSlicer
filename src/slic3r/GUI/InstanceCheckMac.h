#import <Cocoa/Cocoa.h>

@interface OtherInstanceMessageHandlerMac : NSObject

-(instancetype) init;
-(void) add_observer;
-(void) message_update:(NSNotification *)note;
@end
