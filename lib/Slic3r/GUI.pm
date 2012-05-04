package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use FindBin;
use Slic3r::GUI::Plater;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;

use Wx 0.9901 qw(:sizer :frame wxID_EXIT wxID_ABOUT);
use Wx::Event qw(EVT_MENU);
use base 'Wx::App';

sub OnInit {
    my $self = shift;
    
    $self->SetAppName('Slic3r');
    Slic3r::debugf "wxWidgets version %s\n", &Wx::wxVERSION_STRING;
    
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
    
    # status bar
    $frame->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($frame, -1);
    $frame->SetStatusBar($frame->{statusbar});
    
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

package Slic3r::GUI::ProgressStatusBar;
use base 'Wx::StatusBar';

sub new {
    my $class = shift;
    my $self = $class->SUPER::new(@_);
    
    $self->{_changed} = 0;
    $self->{busy} = 0;
    $self->{timer} = Wx::Timer->new($self);
    $self->{prog} = Wx::Gauge->new($self, &Wx::wxGA_HORIZONTAL, 100, [-1,-1], [-1,-1]);
    $self->{prog}->Hide;
    
    $self->SetFieldsCount(2);
    $self->SetStatusWidths(-1, 155);
    
    Wx::Event::EVT_IDLE($self, sub { $self->_Reposition });
    Wx::Event::EVT_TIMER($self, \&OnTimer, $self->{timer});
    Wx::Event::EVT_SIZE($self, \&OnSize);
    
    return $self;
}

sub DESTROY {
    my $self = shift;    
    $self->{timer}->Stop if $self->{timer} && $self->{timer}->IsRunning;
}

sub _Reposition {
    my $self = shift;
    
    ##if ($self->{_changed}) {
        my $rect = $self->GetFieldRect($self->GetFieldsCount - 1);
        my $prog_pos = [$rect->GetX + 2, $rect->GetY + 2];
        $self->{prog}->Move($prog_pos);
        $self->{prog}->SetSize($rect->GetWidth - 8, $rect->GetHeight - 4);
    ##}
    $self->{_changed} = 0;
}

sub OnSize {
    my ($self, $event) = @_;
    
    $self->{_changed} = 1;
    $self->_Reposition;
    $event->Skip;
}

sub OnTimer {
    my ($self, $event) = @_;
    
    if ($self->{prog}->IsShown) {
        $self->{timer}->Stop;
    }
    $self->{prog}->Pulse if $self->{_busy};
}

sub Run {
    my $self = shift;
    my $rate = shift || 100;
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub GetProgress {
    my $self = shift;
    return $self->{prog}->GetValue;
}

sub SetProgress {
    my $self = shift;
    my ($val) = @_;
    if (!$self->{prog}->IsShown) {
        $self->ShowProgress(1);
    }
    if ($val == $self->{prog}->GetRange) {
        $self->{prog}->SetValue(0);
        $self->ShowProgress(0);
    } else {
        $self->{prog}->SetValue($val);
    }
}

sub SetRange {
    my $self = shift;
    my ($val) = @_;
    
    if ($val != $self->{prog}->GetRange) {
        $self->{prog}->SetRange($val);
    }
}

sub ShowProgress {
    my $self = shift;
    my ($show) = @_;
    
    $self->_Reposition;
    $self->{prog}->Show($show);
    $self->{prog}->Pulse;
}

sub StartBusy {
    my $self = shift;
    my $rate = shift || 100;
    
    $self->{_busy} = 1;
    $self->_Reposition;
    $self->ShowProgress(1);
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub StopBusy {
    my $self = shift;
    
    $self->{timer}->Stop;
    $self->ShowProgress(0);
    $self->{prog}->SetValue(0);
    $self->{_busy} = 0;
}

sub IsBusy {
    my $self = shift;
    return $self->{_busy};
}

1;
