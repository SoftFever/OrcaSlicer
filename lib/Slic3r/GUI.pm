package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use FindBin;
use Slic3r::GUI::AboutDialog;
use Slic3r::GUI::ConfigWizard;
use Slic3r::GUI::Plater;
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

use Wx 0.9901 qw(:bitmap :dialog :frame :icon :id :misc :systemsettings :toplevelwindow
    :filedialog);
use Wx::Event qw(EVT_CLOSE EVT_MENU EVT_IDLE);
use base 'Wx::App';

use constant MI_LOAD_CONF     => &Wx::NewId;
use constant MI_LOAD_CONFBUNDLE => &Wx::NewId;
use constant MI_EXPORT_CONF   => &Wx::NewId;
use constant MI_EXPORT_CONFBUNDLE => &Wx::NewId;
use constant MI_QUICK_SLICE   => &Wx::NewId;
use constant MI_REPEAT_QUICK  => &Wx::NewId;
use constant MI_QUICK_SAVE_AS => &Wx::NewId;
use constant MI_SLICE_SVG     => &Wx::NewId;
use constant MI_REPAIR_STL    => &Wx::NewId;
use constant MI_COMBINE_STLS  => &Wx::NewId;

use constant MI_PLATER_EXPORT_GCODE => &Wx::NewId;
use constant MI_PLATER_EXPORT_STL   => &Wx::NewId;
use constant MI_PLATER_EXPORT_AMF   => &Wx::NewId;

use constant MI_TAB_PLATER    => &Wx::NewId;
use constant MI_TAB_PRINT     => &Wx::NewId;
use constant MI_TAB_FILAMENT  => &Wx::NewId;
use constant MI_TAB_PRINTER   => &Wx::NewId;

use constant MI_CONF_WIZARD   => &Wx::NewId;
use constant MI_WEBSITE       => &Wx::NewId;
use constant MI_VERSIONCHECK  => &Wx::NewId;
use constant MI_DOCUMENTATION => &Wx::NewId;

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
    }
    $Settings->{_}{version} = $Slic3r::VERSION;
    Slic3r::GUI->save_settings;
    
    # application frame
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    my $frame = Wx::Frame->new(undef, -1, 'Slic3r', wxDefaultPosition, [760, 470], wxDEFAULT_FRAME_STYLE);
    $frame->SetIcon(Wx::Icon->new("$Slic3r::var/Slic3r_128px.png", wxBITMAP_TYPE_PNG) );
    $self->{skeinpanel} = Slic3r::GUI::SkeinPanel->new($frame,
        mode        => $mode // $Settings->{_}{mode},
        no_plater   => $no_plater,
    );
    $self->SetTopWindow($frame);
    
    # status bar
    $frame->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($frame, -1);
    $frame->{statusbar}->SetStatusText("Version $Slic3r::VERSION - Remember to check for updates at http://slic3r.org/");
    $frame->SetStatusBar($frame->{statusbar});
    
    # File menu
    my $fileMenu = Wx::Menu->new;
    {
        $fileMenu->Append(MI_LOAD_CONF, "&Load Config…\tCtrl+L", 'Load exported configuration file');
        $fileMenu->Append(MI_EXPORT_CONF, "&Export Config…\tCtrl+E", 'Export current configuration to file');
        $fileMenu->Append(MI_LOAD_CONFBUNDLE, "&Load Config Bundle…", 'Load presets from a bundle');
        $fileMenu->Append(MI_EXPORT_CONFBUNDLE, "&Export Config Bundle…", 'Export all presets to file');
        $fileMenu->AppendSeparator();
        $fileMenu->Append(MI_QUICK_SLICE, "Q&uick Slice…\tCtrl+U", 'Slice file');
        $fileMenu->Append(MI_QUICK_SAVE_AS, "Quick Slice and Save &As…\tCtrl+Alt+U", 'Slice file and save as');
        my $repeat = $fileMenu->Append(MI_REPEAT_QUICK, "&Repeat Last Quick Slice\tCtrl+Shift+U", 'Repeat last quick slice');
        $repeat->Enable(0);
        $fileMenu->AppendSeparator();
        $fileMenu->Append(MI_SLICE_SVG, "Slice to SV&G…\tCtrl+G", 'Slice file to SVG');
        $fileMenu->AppendSeparator();
        $fileMenu->Append(MI_REPAIR_STL, "Repair STL file…", 'Automatically repair an STL file');
        $fileMenu->Append(MI_COMBINE_STLS, "Combine multi-material STL files…", 'Combine multiple STL files into a single multi-material AMF file');
        $fileMenu->AppendSeparator();
        $fileMenu->Append(wxID_PREFERENCES, "Preferences…", 'Application preferences');
        $fileMenu->AppendSeparator();
        $fileMenu->Append(wxID_EXIT, "&Quit", 'Quit Slic3r');
        EVT_MENU($frame, MI_LOAD_CONF, sub { $self->{skeinpanel}->load_config_file });
        EVT_MENU($frame, MI_LOAD_CONFBUNDLE, sub { $self->{skeinpanel}->load_configbundle });
        EVT_MENU($frame, MI_EXPORT_CONF, sub { $self->{skeinpanel}->export_config });
        EVT_MENU($frame, MI_EXPORT_CONFBUNDLE, sub { $self->{skeinpanel}->export_configbundle });
        EVT_MENU($frame, MI_QUICK_SLICE, sub { $self->{skeinpanel}->quick_slice;
                                               $repeat->Enable(defined $Slic3r::GUI::SkeinPanel::last_input_file) });
        EVT_MENU($frame, MI_REPEAT_QUICK, sub { $self->{skeinpanel}->quick_slice(reslice => 1) });
        EVT_MENU($frame, MI_QUICK_SAVE_AS, sub { $self->{skeinpanel}->quick_slice(save_as => 1);
                                                 $repeat->Enable(defined $Slic3r::GUI::SkeinPanel::last_input_file) });
        EVT_MENU($frame, MI_SLICE_SVG, sub { $self->{skeinpanel}->quick_slice(save_as => 1, export_svg => 1) });
        EVT_MENU($frame, MI_REPAIR_STL, sub { $self->{skeinpanel}->repair_stl });
        EVT_MENU($frame, MI_COMBINE_STLS, sub { $self->{skeinpanel}->combine_stls });
        EVT_MENU($frame, wxID_PREFERENCES, sub { Slic3r::GUI::Preferences->new($frame)->ShowModal });
        EVT_MENU($frame, wxID_EXIT, sub {$_[0]->Close(0)});
    }
    
    # Plater menu
    my $platerMenu;
    unless ($no_plater) {
        $platerMenu = Wx::Menu->new;
        $platerMenu->Append(MI_PLATER_EXPORT_GCODE, "Export G-code...", 'Export current plate as G-code');
        $platerMenu->Append(MI_PLATER_EXPORT_STL, "Export STL...", 'Export current plate as STL');
        $platerMenu->Append(MI_PLATER_EXPORT_AMF, "Export AMF...", 'Export current plate as AMF');
        EVT_MENU($frame, MI_PLATER_EXPORT_GCODE, sub { $self->{skeinpanel}{plater}->export_gcode });
        EVT_MENU($frame, MI_PLATER_EXPORT_STL, sub { $self->{skeinpanel}{plater}->export_stl });
        EVT_MENU($frame, MI_PLATER_EXPORT_AMF, sub { $self->{skeinpanel}{plater}->export_amf });
    }
    
    # Window menu
    my $windowMenu = Wx::Menu->new;
    {
        my $tab_count = $no_plater ? 3 : 4;
        $windowMenu->Append(MI_TAB_PLATER, "Select &Plater Tab\tCtrl+1", 'Show the plater') unless $no_plater;
        $windowMenu->Append(MI_TAB_PRINT, "Select P&rint Settings Tab\tCtrl+2", 'Show the print settings');
        $windowMenu->Append(MI_TAB_FILAMENT, "Select &Filament Settings Tab\tCtrl+3", 'Show the filament settings');
        $windowMenu->Append(MI_TAB_PRINTER, "Select Print&er Settings Tab\tCtrl+4", 'Show the printer settings');
        EVT_MENU($frame, MI_TAB_PLATER, sub { $self->{skeinpanel}->select_tab(0) }) unless $no_plater;
        EVT_MENU($frame, MI_TAB_PRINT, sub { $self->{skeinpanel}->select_tab($tab_count-3) });
        EVT_MENU($frame, MI_TAB_FILAMENT, sub { $self->{skeinpanel}->select_tab($tab_count-2) });
        EVT_MENU($frame, MI_TAB_PRINTER, sub { $self->{skeinpanel}->select_tab($tab_count-1) });
    }
    
    # Help menu
    my $helpMenu = Wx::Menu->new;
    {
        $helpMenu->Append(MI_CONF_WIZARD, "&Configuration $Slic3r::GUI::ConfigWizard::wizard…", "Run Configuration $Slic3r::GUI::ConfigWizard::wizard");
        $helpMenu->AppendSeparator();
        $helpMenu->Append(MI_WEBSITE, "Slic3r &Website", 'Open the Slic3r website in your browser');
        my $versioncheck = $helpMenu->Append(MI_VERSIONCHECK, "Check for &Updates...", 'Check for new Slic3r versions');
        $versioncheck->Enable(Slic3r::GUI->have_version_check);
        $helpMenu->Append(MI_DOCUMENTATION, "Slic3r &Manual", 'Open the Slic3r manual in your browser');
        $helpMenu->AppendSeparator();
        $helpMenu->Append(wxID_ABOUT, "&About Slic3r", 'Show about dialog');
        EVT_MENU($frame, MI_CONF_WIZARD, sub { $self->{skeinpanel}->config_wizard });
        EVT_MENU($frame, MI_WEBSITE, sub { Wx::LaunchDefaultBrowser('http://slic3r.org/') });
        EVT_MENU($frame, MI_VERSIONCHECK, sub { Slic3r::GUI->check_version(manual => 1) });
        EVT_MENU($frame, MI_DOCUMENTATION, sub { Wx::LaunchDefaultBrowser('http://manual.slic3r.org/') });
        EVT_MENU($frame, wxID_ABOUT, \&about);
    }
    
    # menubar
    # assign menubar to frame after appending items, otherwise special items
    # will not be handled correctly
    {
        my $menubar = Wx::MenuBar->new;
        $menubar->Append($fileMenu, "&File");
        $menubar->Append($platerMenu, "&Plater") if $platerMenu;
        $menubar->Append($windowMenu, "&Window");
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
    
    $frame->Fit;
    $frame->SetMinSize($frame->GetSize);
    $frame->Show;
    $frame->Layout;
    
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
