package Slic3r::GUI;
use strict;
use warnings;

use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;

use Wx qw(:sizer :frame wxID_EXIT wxID_ABOUT);
use Wx::Event qw(EVT_MENU);
use base 'Wx::App';

sub OnInit {
    my $self = shift;
    
    #$self->SetIcon(Wx::Icon->new("path/to/my/icon.gif", wxBITMAP_TYPE_GIF) );
    
    my $frame = Wx::Frame->new( undef, -1, 'Slic3r', [-1, -1], Wx::wxDefaultSize,
         wxDEFAULT_FRAME_STYLE ^ (wxRESIZE_BORDER | wxMAXIMIZE_BOX) );
    
    # menubar
    my $menubar = Wx::MenuBar->new;
    $frame->SetMenuBar($menubar);
    EVT_MENU($frame, wxID_EXIT, sub {$_[0]->Close(1)});
    EVT_MENU($frame, wxID_ABOUT, \&About);
    
    my $panel = Slic3r::GUI::SkeinPanel->new($frame);
    my $box = Wx::BoxSizer->new(wxVERTICAL);
    $box->Add($panel, 0);
    
    $box->SetSizeHints($frame);
    $frame->SetSizer($box);
    $frame->Show;
    $frame->Layout;
    
    return 1;
}

sub About {
    my $frame = shift;
    
    my $info = Wx::AboutDialogInfo->new;
    $info->SetName('Slic3r');
    $info->AddDeveloper('Alessandro Ranellucci');
    
    Wx::AboutBox($info);
}

1;
