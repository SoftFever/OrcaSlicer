
#import <wx/osx/cocoa/dataview.h>
#import "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

void dataview_remove_insets(wxDataViewCtrl* dv) {
    NSScrollView* scrollview = (NSScrollView*) ((wxCocoaDataViewControl*)dv->GetDataViewPeer())->GetWXWidget();
    NSOutlineView* outlineview = scrollview.documentView;
    [outlineview setIntercellSpacing: NSMakeSize(0.0, 1.0)];
    if (@available(macOS 11, *)) {
        [outlineview setStyle:NSTableViewStylePlain];
    }
}

void staticbox_remove_margin(wxStaticBox* sb) {
    NSBox* nativeBox = (NSBox*)sb->GetHandle();
    [nativeBox setBoxType:NSBoxCustom];
    [nativeBox setBorderWidth:0];
}

}
}

