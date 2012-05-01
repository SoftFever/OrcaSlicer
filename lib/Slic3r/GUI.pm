package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use FindBin;
use Slic3r::GUI::Dashboard;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;

use Wx 0.9901 qw(:sizer :frame wxID_EXIT wxID_ABOUT);
use Wx::Event qw(EVT_MENU);
use base 'Wx::App';

sub OnInit {
    my $self = shift;
    
    $self->SetAppName('Slic3r');
    
    my $frame = Wx::Frame->new( undef, -1, 'Slic3r', [-1, -1], Wx::wxDefaultSize,
         wxDEFAULT_FRAME_STYLE ^ (wxRESIZE_BORDER | wxMAXIMIZE_BOX) );
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    $frame->SetIcon(Wx::Icon->new("$FindBin::Bin/var/Slic3r_128px.png", &Wx::wxBITMAP_TYPE_PNG) );
    
    my $panel = Slic3r::GUI::SkeinPanel->new($frame);
    my $box = Wx::BoxSizer->new(wxVERTICAL);
    $box->Add($panel, 0);
    
    # menubar
    my $menubar = Wx::MenuBar->new;
    $frame->SetMenuBar($menubar);
    EVT_MENU($frame, wxID_EXIT, sub {$_[0]->Close(1)});
    EVT_MENU($frame, wxID_ABOUT, \&About);
    
    # File menu
    my $fileMenu = Wx::Menu->new;
    $fileMenu->Append(1, "Save Config…");
    $fileMenu->Append(2, "Open Config…");
    $fileMenu->AppendSeparator();
    $fileMenu->Append(3, "Slice…");
    $fileMenu->Append(4, "Reslice");
    $fileMenu->Append(5, "Slice and Save As…");
    $fileMenu->Append(6, "Export SVG…");
    $fileMenu->AppendSeparator();
    $fileMenu->Append(&Wx::wxID_EXIT, "&Quit");
    $menubar->Append($fileMenu, "&File");
    EVT_MENU($frame, 1, sub { $panel->save_config });
    EVT_MENU($frame, 2, sub { $panel->load_config });
    EVT_MENU($frame, 3, sub { $panel->do_slice });
    EVT_MENU($frame, 4, sub { $panel->do_slice(reslice => 1) });
    EVT_MENU($frame, 5, sub { $panel->do_slice(save_as => 1) });
    EVT_MENU($frame, 6, sub { $panel->do_slice(save_as => 1, export_svg => 1) });
    
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

sub catch_error {
    my ($self, $cb) = @_;
    if (my $err = $@) {
        $cb->() if $cb;
        Wx::MessageDialog->new($self, $err, 'Error', &Wx::wxOK | &Wx::wxICON_ERROR)->ShowModal;
        return 1;
    }
    return 0;
}

sub warning_catcher {
    my ($self) = @_;
    return sub {
        my $message = shift;
        Wx::MessageDialog->new($self, $message, 'Warning', &Wx::wxOK | &Wx::wxICON_WARNING)->ShowModal;
    };
}

1;
