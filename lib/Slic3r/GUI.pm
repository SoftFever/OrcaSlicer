package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use FindBin;
use Slic3r::GUI::AboutDialog;
use Slic3r::GUI::ConfigWizard;
use Slic3r::GUI::Plater;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;
use Slic3r::GUI::Tab;

use Wx 0.9901 qw(:bitmap :dialog :frame :icon :id :misc :systemsettings);
use Wx::Event qw(EVT_CLOSE EVT_MENU);
use base 'Wx::App';

use constant MI_LOAD_CONF     => 1;
use constant MI_EXPORT_CONF   => 2;
use constant MI_QUICK_SLICE   => 3;
use constant MI_REPEAT_QUICK  => 4;
use constant MI_QUICK_SAVE_AS => 5;
use constant MI_SLICE_SVG     => 6;
use constant MI_CONF_WIZARD   => 7;
use constant MI_WEBSITE       => 8;

our $datadir;

our $small_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11) if !&Wx::wxMSW;
our $medium_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$medium_font->SetPointSize(12);

sub OnInit {
    my $self = shift;
    
    $self->SetAppName('Slic3r');
    Slic3r::debugf "wxWidgets version %s\n", &Wx::wxVERSION_STRING;
    
    $self->{notifier} = Slic3r::GUI::Notifier->new;
    
    # locate or create data directory
    $datadir = Wx::StandardPaths::Get->GetUserDataDir;
    Slic3r::debugf "Data directory: %s\n", $datadir;
    my $run_wizard = (-d $datadir) ? 0 : 1;
    for ($datadir, "$datadir/print", "$datadir/filament", "$datadir/printer") {
        mkdir or $self->fatal_error("Slic3r was unable to create its data directory at $_ (errno: $!).")
            unless -d $_;
    }
    
    # load settings
    if (-f "$datadir/slic3r.ini") {
        my $ini = eval { Slic3r::Config->read_ini("$datadir/slic3r.ini") };
        $Slic3r::Settings = $ini if $ini;
    }
    
    # application frame
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    my $frame = Wx::Frame->new(undef, -1, 'Slic3r', wxDefaultPosition, [760, 470], wxDEFAULT_FRAME_STYLE);
    $frame->SetIcon(Wx::Icon->new("$Slic3r::var/Slic3r_128px.png", wxBITMAP_TYPE_PNG) );
    $self->{skeinpanel} = Slic3r::GUI::SkeinPanel->new($frame);
    $self->SetTopWindow($frame);
    
    # status bar
    $frame->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($frame, -1);
    $frame->{statusbar}->SetStatusText("Version $Slic3r::VERSION - Remember to check for updates at http://slic3r.org/");
    $frame->SetStatusBar($frame->{statusbar});
    
    # File menu
    my $fileMenu = Wx::Menu->new;
    {
        $fileMenu->Append(MI_LOAD_CONF, "&Load Config…\tCtrl+L");
        $fileMenu->Append(MI_EXPORT_CONF, "&Export Config…\tCtrl+E");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(MI_QUICK_SLICE, "Q&uick Slice…\tCtrl+U");
        $fileMenu->Append(MI_REPEAT_QUICK, "&Repeat Last Quick Slice\tCtrl+Shift+U");
        $fileMenu->Append(MI_QUICK_SAVE_AS, "Quick Slice and Save &As…\tCtrl+Alt+U");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(MI_SLICE_SVG, "Slice to SV&G…\tCtrl+G");
        $fileMenu->AppendSeparator();
        $fileMenu->Append(wxID_EXIT, "&Quit");
        EVT_MENU($frame, MI_LOAD_CONF, sub { $self->{skeinpanel}->load_config });
        EVT_MENU($frame, MI_EXPORT_CONF, sub { $self->{skeinpanel}->save_config });
        EVT_MENU($frame, MI_QUICK_SLICE, sub { $self->{skeinpanel}->do_slice });
        EVT_MENU($frame, MI_REPEAT_QUICK, sub { $self->{skeinpanel}->do_slice(reslice => 1) });
        EVT_MENU($frame, MI_QUICK_SAVE_AS, sub { $self->{skeinpanel}->do_slice(save_as => 1) });
        EVT_MENU($frame, MI_SLICE_SVG, sub { $self->{skeinpanel}->do_slice(save_as => 1, export_svg => 1) });
        EVT_MENU($frame, wxID_EXIT, sub {$_[0]->Close(0)});
    }
    
    # Help menu
    my $helpMenu = Wx::Menu->new;
    {
        $helpMenu->Append(MI_CONF_WIZARD, "&Configuration $Slic3r::GUI::ConfigWizard::wizard…");
        $helpMenu->Append(MI_WEBSITE, "Slic3r &Website");
        $helpMenu->Append(wxID_ABOUT, "&About Slic3r");
        EVT_MENU($frame, MI_CONF_WIZARD, sub { $self->{skeinpanel}->config_wizard });
        EVT_MENU($frame, MI_WEBSITE, sub { Wx::LaunchDefaultBrowser('http://slic3r.org/') });
        EVT_MENU($frame, wxID_ABOUT, \&about);
    }
    
    # menubar
    # assign menubar to frame after appending items, otherwise special items
    # will not be handled correctly
    {
        my $menubar = Wx::MenuBar->new;
        $menubar->Append($fileMenu, "&File");
        $menubar->Append($helpMenu, "&Help");
        $frame->SetMenuBar($menubar);
    }
    
    EVT_CLOSE($frame, sub {
        my (undef, $event) = @_;
        if ($event->CanVeto && !$self->{skeinpanel}->check_unsaved_changes) {
            $event->Veto;
            return;
        }
        $event->Skip;
    });
    
    $frame->SetMinSize($frame->GetSize);
    $frame->Show;
    $frame->Layout;
    
    $self->{skeinpanel}->config_wizard if $run_wizard;
    
    return 1;
}

sub about {
    my $frame = shift;
    
    my $about = Slic3r::GUI::AboutDialog->new($frame);
    $about->ShowModal;
    $about->Destroy;
}

sub catch_error {
    my ($self, $cb, $message_dialog) = @_;
    if (my $err = $@) {
        $cb->() if $cb;
        my @params = ($err, 'Error', wxOK | wxICON_ERROR);
        $message_dialog
            ? $message_dialog->(@params)
            : Wx::MessageDialog->new($self, @params)->ShowModal;
        return 1;
    }
    return 0;
}

sub show_error {
    my $self = shift;
    my ($message) = @_;
    Wx::MessageDialog->new($self, $message, 'Error', wxOK | wxICON_ERROR)->ShowModal;
}

sub fatal_error {
    my $self = shift;
    $self->show_error(@_);
    exit 1;
}

sub warning_catcher {
    my ($self, $message_dialog) = @_;
    return sub {
        my $message = shift;
        my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
        $message_dialog
            ? $message_dialog->(@params)
            : Wx::MessageDialog->new($self, @params)->ShowModal;
    };
}

sub notify {
    my $self = shift;
    my ($message) = @_;

    my $frame = $self->GetTopWindow;
    $frame->RequestUserAttention unless ($frame->IsActive);

    $self->{notifier}->notify($message);
}

package Slic3r::GUI::ProgressStatusBar;
use Wx qw(:gauge :misc);
use base 'Wx::StatusBar';

sub new {
    my $class = shift;
    my $self = $class->SUPER::new(@_);
    
    $self->{busy} = 0;
    $self->{timer} = Wx::Timer->new($self);
    $self->{prog} = Wx::Gauge->new($self, wxGA_HORIZONTAL, 100, wxDefaultPosition, wxDefaultSize);
    $self->{prog}->Hide;
    $self->{cancelbutton} = Wx::Button->new($self, -1, "Cancel", wxDefaultPosition, wxDefaultSize);
    $self->{cancelbutton}->Hide;
    
    $self->SetFieldsCount(3);
    $self->SetStatusWidths(-1, 150, 155);
    
    Wx::Event::EVT_TIMER($self, \&OnTimer, $self->{timer});
    Wx::Event::EVT_SIZE($self, \&OnSize);
    Wx::Event::EVT_BUTTON($self, $self->{cancelbutton}, sub {
        $self->{cancel_cb}->();
        $self->{cancelbutton}->Hide;
    });
    
    return $self;
}

sub DESTROY {
    my $self = shift;    
    $self->{timer}->Stop if $self->{timer} && $self->{timer}->IsRunning;
}

sub OnSize {
    my ($self, $event) = @_;
    
    my %fields = (
        # 0 is reserved for status text
        1 => $self->{cancelbutton},
        2 => $self->{prog},
    );

    foreach (keys %fields) {
        my $rect = $self->GetFieldRect($_);
        my $offset = &Wx::wxGTK ? 1 : 0; # add a cosmetic 1 pixel offset on wxGTK
        my $pos = [$rect->GetX + $offset, $rect->GetY + $offset];
        $fields{$_}->Move($pos);
        $fields{$_}->SetSize($rect->GetWidth - $offset, $rect->GetHeight);
    }

    $event->Skip;
}

sub OnTimer {
    my ($self, $event) = @_;
    
    if ($self->{prog}->IsShown) {
        $self->{timer}->Stop;
    }
    $self->{prog}->Pulse if $self->{_busy};
}

sub SetCancelCallback {
    my $self = shift;
    my ($cb) = @_;
    $self->{cancel_cb} = $cb;
    $cb ? $self->{cancelbutton}->Show : $self->{cancelbutton}->Hide;
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
    
    $self->{prog}->Show($show);
    $self->{prog}->Pulse;
}

sub StartBusy {
    my $self = shift;
    my $rate = shift || 100;
    
    $self->{_busy} = 1;
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

package Slic3r::GUI::Notifier;

sub new {
    my $class = shift;
    my $self;

    $self->{icon} = "$Slic3r::var/Slic3r.png";

    if (eval 'use Growl::GNTP; 1') {
        # register with growl
        eval {
            $self->{growler} = Growl::GNTP->new(AppName => 'Slic3r', AppIcon => $self->{icon});
            $self->{growler}->register([{Name => 'SKEIN_DONE', DisplayName => 'Slicing Done'}]);
        };
    }

    bless $self, $class;

    return $self;
}

sub notify {
    my ($self, $message) = @_;
    my $title = 'Slicing Done!';

    eval {
        $self->{growler}->notify(Event => 'SKEIN_DONE', Title => $title, Message => $message)
            if $self->{growler};
    };
    if (eval 'use Net::DBus; 1') {
        eval {
            my $session = Net::DBus->session;
            my $serv = $session->get_service('org.freedesktop.Notifications');
            my $notifier = $serv->get_object('/org/freedesktop/Notifications',
                                             'org.freedesktop.Notifications');
            $notifier->Notify('Slic3r', 0, $self->{icon}, $title, $message, [], {}, 1500);
        }
    };
}

1;
