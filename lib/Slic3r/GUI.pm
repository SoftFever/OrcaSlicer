package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use FindBin;
use Slic3r::GUI::AboutDialog;
use Slic3r::GUI::ConfigWizard;
use Slic3r::GUI::MainFrame;
use Slic3r::GUI::Plater;
use Slic3r::GUI::Plater::2D;
use Slic3r::GUI::Plater::ObjectPartsPanel;
use Slic3r::GUI::Plater::ObjectCutDialog;
use Slic3r::GUI::Plater::ObjectPreviewDialog;
use Slic3r::GUI::Plater::ObjectSettingsDialog;
use Slic3r::GUI::Plater::OverrideSettingsPanel;
use Slic3r::GUI::Preferences;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::SkeinPanel;
use Slic3r::GUI::SimpleTab;
use Slic3r::GUI::Tab;

our $have_OpenGL = eval "use Slic3r::GUI::PreviewCanvas; 1";

use Wx 0.9901 qw(:bitmap :dialog :icon :id :misc :systemsettings :toplevelwindow
    :filedialog);
use Wx::Event qw(EVT_IDLE);
use base 'Wx::App';

our $datadir;
our $no_plater;
our $mode;
our $autosave;
our @cb;

our $Settings = {
    _ => {
        mode => 'simple',
        version_check => 1,
        autocenter => 1,
        background_processing => 1,
    },
};

our $have_button_icons = &Wx::wxVERSION_STRING =~ / 2\.9\.[1-9]/;
our $small_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11) if !&Wx::wxMSW;
our $medium_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$medium_font->SetPointSize(12);

sub OnInit {
    my $self = shift;
    
    $self->SetAppName('Slic3r');
    Slic3r::debugf "wxWidgets version %s, Wx version %s\n", &Wx::wxVERSION_STRING, $Wx::VERSION;
    
    $self->{notifier} = Slic3r::GUI::Notifier->new;
    
    # locate or create data directory
    $datadir ||= Wx::StandardPaths::Get->GetUserDataDir;
    $datadir = Slic3r::encode_path($datadir);
    Slic3r::debugf "Data directory: %s\n", $datadir;
    
    # just checking for existence of $datadir is not enough: it may be an empty directory
    # supplied as argument to --datadir; in that case we should still run the wizard
    my $run_wizard = (-d $datadir && -e "$datadir/slic3r.ini") ? 0 : 1;
    for ($datadir, "$datadir/print", "$datadir/filament", "$datadir/printer") {
        mkdir or $self->fatal_error("Slic3r was unable to create its data directory at $_ (errno: $!).")
            unless -d $_;
    }
    
    # load settings
    my $last_version;
    if (-f "$datadir/slic3r.ini") {
        my $ini = eval { Slic3r::Config->read_ini("$datadir/slic3r.ini") };
        $Settings = $ini if $ini;
        $last_version = $Settings->{_}{version};
        $Settings->{_}{mode} ||= 'expert';
        $Settings->{_}{autocenter} //= 1;
        $Settings->{_}{background_processing} //= 1;
    }
    $Settings->{_}{version} = $Slic3r::VERSION;
    Slic3r::GUI->save_settings;
    
    # application frame
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    my $frame = Slic3r::GUI::MainFrame->new(
        mode        => $mode // $Settings->{_}{mode},
        no_plater   => $no_plater,
    );
    $self->{skeinpanel} = $frame->{skeinpanel};
    $self->SetTopWindow($frame);
    
    if (!$run_wizard && (!defined $last_version || $last_version ne $Slic3r::VERSION)) {
        # user was running another Slic3r version on this computer
        if (!defined $last_version || $last_version =~ /^0\./) {
            show_info($self->{skeinpanel}, "Hello! Support material was improved since the "
                . "last version of Slic3r you used. It is strongly recommended to revert "
                . "your support material settings to the factory defaults and start from "
                . "those. Enjoy and provide feedback!", "Support Material");
        }
    }
    $self->{skeinpanel}->config_wizard if $run_wizard;
    
    Slic3r::GUI->check_version
        if Slic3r::GUI->have_version_check
            && ($Settings->{_}{version_check} // 1)
            && (!$Settings->{_}{last_version_check} || (time - $Settings->{_}{last_version_check}) >= 86400);
    
    EVT_IDLE($frame, sub {
        while (my $cb = shift @cb) {
            $cb->();
        }
    });
    
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
        $message_dialog
            ? $message_dialog->($err, 'Error', wxOK | wxICON_ERROR)
            : Slic3r::GUI::show_error($self, $err);
        return 1;
    }
    return 0;
}

sub show_error {
    my $self = shift;
    my ($message) = @_;
    Wx::MessageDialog->new($self, $message, 'Error', wxOK | wxICON_ERROR)->ShowModal;
}

sub show_info {
    my $self = shift;
    my ($message, $title) = @_;
    Wx::MessageDialog->new($self, $message, $title || 'Notice', wxOK | wxICON_INFORMATION)->ShowModal;
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
        return if $message =~ /GLUquadricObjPtr|Attempt to free unreferenced scalar/;
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
    # try harder to attract user attention on OS X
    $frame->RequestUserAttention(&Wx::wxMAC ? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO)
        unless ($frame->IsActive);

    $self->{notifier}->notify($message);
}

sub save_settings {
    my $class = shift;
    
    Slic3r::Config->write_ini("$datadir/slic3r.ini", $Settings);
}

sub presets {
    my ($class, $section) = @_;
    
    my %presets = ();
    opendir my $dh, "$Slic3r::GUI::datadir/$section" or die "Failed to read directory $Slic3r::GUI::datadir/$section (errno: $!)\n";
    foreach my $file (grep /\.ini$/i, readdir $dh) {
        my $name = basename($file);
        $name =~ s/\.ini$//;
        $presets{$name} = "$Slic3r::GUI::datadir/$section/$file";
    }
    closedir $dh;
    
    return %presets;
}

sub have_version_check {
    my $class = shift;
    
    # return an explicit 0
    return ($Slic3r::have_threads && $Slic3r::build && eval "use LWP::UserAgent; 1") || 0;
}

sub check_version {
    my $class = shift;
    my %p = @_;
    Slic3r::debugf "Checking for updates...\n";
    
    @_ = ();
    threads->create(sub {
        my $ua = LWP::UserAgent->new;
        $ua->timeout(10);
        my $response = $ua->get('http://slic3r.org/updatecheck');
        if ($response->is_success) {
            if ($response->decoded_content =~ /^obsolete ?= ?([a-z0-9.-]+,)*\Q$Slic3r::VERSION\E(?:,|$)/) {
                my $res = Wx::MessageDialog->new(undef, "A new version is available. Do you want to open the Slic3r website now?",
                    'Update', wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_INFORMATION | wxICON_ERROR)->ShowModal;
                Wx::LaunchDefaultBrowser('http://slic3r.org/') if $res == wxID_YES;
            } else {
                Slic3r::GUI::show_info(undef, "You're using the latest version. No updates are available.") if $p{manual};
            }
            $Settings->{_}{last_version_check} = time();
            Slic3r::GUI->save_settings;
        } else {
            Slic3r::GUI::show_error(undef, "Failed to check for updates. Try later.") if $p{manual};
        }
        Slic3r::thread_cleanup();
    })->detach;
}

sub output_path {
    my $class = shift;
    my ($dir) = @_;
    
    return ($Settings->{_}{last_output_path} && $Settings->{_}{remember_output_path})
        ? $Settings->{_}{last_output_path}
        : $dir;
}

sub open_model {
    my ($self) = @_;
    
    my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory}
           || $Slic3r::GUI::Settings->{recent}{config_directory}
           || '';
    
    my $dialog = Wx::FileDialog->new($self, 'Choose one or more files (STL/OBJ/AMF):', $dir, "",
        &Slic3r::GUI::SkeinPanel::MODEL_WILDCARD, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if ($dialog->ShowModal != wxID_OK) {
        $dialog->Destroy;
        return;
    }
    my @input_files = $dialog->GetPaths;
    $dialog->Destroy;
    
    return @input_files;
}

sub CallAfter {
    my $class = shift;
    my ($cb) = @_;
    push @cb, $cb;
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
    # Net::DBus is broken in multithreaded environment
    if (0 && eval 'use Net::DBus; 1') {
        eval {
            my $session = Net::DBus->session;
            my $serv = $session->get_service('org.freedesktop.Notifications');
            my $notifier = $serv->get_object('/org/freedesktop/Notifications',
                                             'org.freedesktop.Notifications');
            $notifier->Notify('Slic3r', 0, $self->{icon}, $title, $message, [], {}, -1);
            undef $Net::DBus::bus_session;
        };
    }
}

1;
