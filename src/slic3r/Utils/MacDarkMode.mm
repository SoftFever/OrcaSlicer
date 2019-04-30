#import "MacDarkMode.hpp"

#import <Foundation/Foundation.h>


@implementation MacDarkMode

namespace Slic3r {
namespace GUI {

bool mac_dark_mode()
{
    NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style isEqualToString:@"Dark"];

}


}
}

@end
