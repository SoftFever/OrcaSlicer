package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use FindBin;
use List::Util qw(first);
use Slic3r::GUI::2DBed;
use Slic3r::GUI::AboutDialog;
use Slic3r::GUI::BedShapeDialog;
use Slic3r::GUI::BonjourBrowser;
use Slic3r::GUI::ConfigWizard;
use Slic3r::GUI::Controller;
use Slic3r::GUI::Controller::ManualControlDialog;
use Slic3r::GUI::Controller::PrinterPanel;
use Slic3r::GUI::MainFrame;
use Slic3r::GUI::Notifier;
use Slic3r::GUI::Plater;
use Slic3r::GUI::Plater::2D;
use Slic3r::GUI::Plater::2DToolpaths;
use Slic3r::GUI::Plater::3D;
use Slic3r::GUI::Plater::3DPreview;
use Slic3r::GUI::Plater::ObjectPartsPanel;
use Slic3r::GUI::Plater::ObjectCutDialog;
use Slic3r::GUI::Plater::ObjectSettingsDialog;
use Slic3r::GUI::Plater::LambdaObjectDialog;
use Slic3r::GUI::Plater::OverrideSettingsPanel;
use Slic3r::GUI::Preferences;
use Slic3r::GUI::ProgressStatusBar;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::OptionsGroup::Field;
use Slic3r::GUI::SystemInfo;
use Slic3r::GUI::Tab;

our $have_OpenGL = eval "use Slic3r::GUI::3DScene; 1";
our $have_LWP    = eval "use LWP::UserAgent; 1";

use Wx 0.9901 qw(:bitmap :dialog :icon :id :misc :systemsettings :toplevelwindow :filedialog :font);
use Wx::Event qw(EVT_IDLE EVT_COMMAND EVT_MENU);
use base 'Wx::App';

use constant FILE_WILDCARDS => {
    known   => 'Known files (*.stl, *.obj, *.amf, *.xml, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.amf;*.AMF;*.xml;*.XML;*.prusa;*.PRUSA',
    stl     => 'STL files (*.stl)|*.stl;*.STL',
    obj     => 'OBJ files (*.obj)|*.obj;*.OBJ',
    amf     => 'AMF files (*.amf)|*.amf;*.AMF;*.xml;*.XML',
    prusa   => 'Prusa Control files (*.prusa)|*.prusa;*.PRUSA',
    ini     => 'INI files *.ini|*.ini;*.INI',
    gcode   => 'G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC',
    svg     => 'SVG files *.svg|*.svg;*.SVG',
};
use constant MODEL_WILDCARD => join '|', @{&FILE_WILDCARDS}{qw(known stl obj amf prusa)};

# If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
our $no_controller;
our $no_plater;
our $autosave;
our @cb;

our $Settings = {
    _ => {
        version_check => 1,
        autocenter => 1,
        # Disable background processing by default as it is not stable.
        background_processing => 0,
        # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        # By default, Prusa has the controller hidden.
        no_controller => 1,
        # If set, the "- default -" selections of print/filament/printer are suppressed, if there is a valid preset available.
        no_defaults => 1,
    },
};

our $small_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11) if &Wx::wxMAC;
our $small_bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_bold_font->SetPointSize(11) if &Wx::wxMAC;
$small_bold_font->SetWeight(wxFONTWEIGHT_BOLD);
our $medium_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$medium_font->SetPointSize(12);
our $grey = Wx::Colour->new(200,200,200);

sub OnInit {
    my ($self) = @_;
    
    $self->SetAppName('Slic3r');
    $self->SetAppDisplayName('Slic3r Prusa Edition');
    Slic3r::debugf "wxWidgets version %s, Wx version %s\n", &Wx::wxVERSION_STRING, $Wx::VERSION;
    
    $self->{notifier} = Slic3r::GUI::Notifier->new;
    $self->{preset_bundle} = Slic3r::GUI::PresetBundle->new;
    
    # just checking for existence of Slic3r::data_dir is not enough: it may be an empty directory
    # supplied as argument to --datadir; in that case we should still run the wizard
    my $enc_datadir = Slic3r::encode_path(Slic3r::data_dir);
    Slic3r::debugf "Data directory: %s\n", $enc_datadir;
    my $run_wizard = (-d $enc_datadir && -e "$enc_datadir/slic3r.ini") ? 0 : 1;
    foreach my $dir ($enc_datadir, "$enc_datadir/print", "$enc_datadir/filament", "$enc_datadir/printer") {
        next if -d $dir;
        if (!mkdir $dir) {
            my $error = "Slic3r was unable to create its data directory at $dir ($!).";
            warn "$error\n";
            fatal_error(undef, $error);
        }
    }
    
    # load settings
    my $last_version;
    if (-f "$enc_datadir/slic3r.ini") {
        my $ini = eval { Slic3r::Config->read_ini(Slic3r::data_dir . "/slic3r.ini") };
        $Settings = $ini if $ini;
        $last_version = $Settings->{_}{version};
        $Settings->{_}{autocenter} //= 1;
        $Settings->{_}{background_processing} //= 1;
        # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        $Settings->{_}{no_controller} //= 1;
        # If set, the "- default -" selections of print/filament/printer are suppressed, if there is a valid preset available.
        $Settings->{_}{no_defaults} //= 1;
    }
    $Settings->{_}{version} = $Slic3r::VERSION;
    $self->save_settings;

    # Suppress the '- default -' presets.
    $self->{preset_bundle}->set_default_suppressed($Slic3r::GUI::Settings->{_}{no_defaults} ? 1 : 0);
    eval { $self->{preset_bundle}->load_presets(Slic3r::data_dir) };
    
    # application frame
    Wx::Image::AddHandler(Wx::PNGHandler->new);
    $self->{mainframe} = my $frame = Slic3r::GUI::MainFrame->new(
        # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        no_controller   => $no_controller // $Settings->{_}{no_controller},
        no_plater       => $no_plater,
    );
    $self->SetTopWindow($frame);
    
    # load init bundle
    #FIXME this is undocumented and the use case is unclear.
#    {
#        my @dirs = ($FindBin::Bin);
#        if (&Wx::wxMAC) {
#            push @dirs, qw();
#        } elsif (&Wx::wxMSW) {
#            push @dirs, qw();
#        }
#        my $init_bundle = first { -e $_ } map "$_/.init_bundle.ini", @dirs;
#        if ($init_bundle) {
#            Slic3r::debugf "Loading config bundle from %s\n", $init_bundle;
#            $self->{mainframe}->load_configbundle($init_bundle, 1);
#            $run_wizard = 0;
#        }
#    }
    
    if (!$run_wizard && (!defined $last_version || $last_version ne $Slic3r::VERSION)) {
        # user was running another Slic3r version on this computer
        if (!defined $last_version || $last_version =~ /^0\./) {
            show_info($self->{mainframe}, "Hello! Support material was improved since the "
                . "last version of Slic3r you used. It is strongly recommended to revert "
                . "your support material settings to the factory defaults and start from "
                . "those. Enjoy and provide feedback!", "Support Material");
        }
        if (!defined $last_version || $last_version =~ /^(?:0|1\.[01])\./) {
            show_info($self->{mainframe}, "Hello! In this version a new Bed Shape option was "
                . "added. If the bed coordinates in the plater preview screen look wrong, go "
                . "to Print Settings and click the \"Set\" button next to \"Bed Shape\".", "Bed Shape");
        }
    }
    if ($run_wizard) {
        $self->{mainframe}->config_wizard;
        eval { $self->{preset_bundle}->load_presets(Slic3r::data_dir) };
    }

    EVT_IDLE($frame, sub {
        while (my $cb = shift @cb) {
            $cb->();
        }
    });
    
    return 1;
}

sub about {
    my ($self) = @_;
    my $about = Slic3r::GUI::AboutDialog->new(undef);
    $about->ShowModal;
    $about->Destroy;
}

sub system_info {
    my ($self) = @_;
    my $slic3r_info = Slic3r::slic3r_info(format => 'html');
    my $copyright_info = Slic3r::copyright_info(format => 'html');
    my $system_info = Slic3r::system_info(format => 'html');
    my $opengl_info;
    my $opengl_info_txt = '';
    if (defined($self->{mainframe}) && defined($self->{mainframe}->{plater}) &&
        defined($self->{mainframe}->{plater}->{canvas3D})) {
        $opengl_info = $self->{mainframe}->{plater}->{canvas3D}->opengl_info(format => 'html');
        $opengl_info_txt = $self->{mainframe}->{plater}->{canvas3D}->opengl_info;
    }
    my $about = Slic3r::GUI::SystemInfo->new(
        parent      => undef, 
        slic3r_info => $slic3r_info,
#        copyright_info => $copyright_info,
        system_info => $system_info, 
        opengl_info => $opengl_info,
        text_info => Slic3r::slic3r_info . Slic3r::system_info . $opengl_info_txt,
    );
    $about->ShowModal;
    $about->Destroy;
}

# static method accepting a wxWindow object as first parameter
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

# static method accepting a wxWindow object as first parameter
sub show_error {
    my ($parent, $message) = @_;
    Wx::MessageDialog->new($parent, $message, 'Error', wxOK | wxICON_ERROR)->ShowModal;
}

# static method accepting a wxWindow object as first parameter
sub show_info {
    my ($parent, $message, $title) = @_;
    Wx::MessageDialog->new($parent, $message, $title || 'Notice', wxOK | wxICON_INFORMATION)->ShowModal;
}

# static method accepting a wxWindow object as first parameter
sub fatal_error {
    show_error(@_);
    exit 1;
}

# static method accepting a wxWindow object as first parameter
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
    my ($self, $message) = @_;

    my $frame = $self->GetTopWindow;
    # try harder to attract user attention on OS X
    $frame->RequestUserAttention(&Wx::wxMAC ? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO)
        unless ($frame->IsActive);

    $self->{notifier}->notify($message);
}

sub save_settings {
    my ($self) = @_;
    Slic3r::Config->write_ini(Slic3r::data_dir . "/slic3r.ini", $Settings);
}

# Called after the Preferences dialog is closed and the program settings are saved.
# Update the UI based on the current preferences.
sub update_ui_from_settings {
    my ($self) = @_;
    $self->{mainframe}->update_ui_from_settings;
}

sub output_path {
    my ($self, $dir) = @_;
    
    return ($Settings->{_}{last_output_path} && $Settings->{_}{remember_output_path})
        ? $Settings->{_}{last_output_path}
        : $dir;
}

sub open_model {
    my ($self, $window) = @_;
    
    my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory}
           || $Slic3r::GUI::Settings->{recent}{config_directory}
           || '';
    
    my $dialog = Wx::FileDialog->new($window // $self->GetTopWindow, 'Choose one or more files (STL/OBJ/AMF/PRUSA):', $dir, "",
        MODEL_WILDCARD, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if ($dialog->ShowModal != wxID_OK) {
        $dialog->Destroy;
        return;
    }
    my @input_files = $dialog->GetPaths;
    $dialog->Destroy;
    return @input_files;
}

sub CallAfter {
    my ($self, $cb) = @_;
    push @cb, $cb;
}

sub scan_serial_ports {
    my ($self) = @_;
    
    my @ports = ();
    
    if ($^O eq 'MSWin32') {
        # Windows
        if (eval "use Win32::TieRegistry; 1") {
            my $ts = Win32::TieRegistry->new("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM",
                { Access => 'KEY_READ' });
            if ($ts) {
                # when no serial ports are available, the registry key doesn't exist and 
                # TieRegistry->new returns undef
                $ts->Tie(\my %reg);
                push @ports, sort values %reg;
            }
        }
    } else {
        # UNIX and OS X
        push @ports, glob '/dev/{ttyUSB,ttyACM,tty.,cu.,rfcomm}*';
    }
    
    return grep !/Bluetooth|FireFly/, @ports;
}

sub append_menu_item {
    my ($self, $menu, $string, $description, $cb, $id, $icon, $kind) = @_;
    
    $id //= &Wx::NewId();
    my $item = Wx::MenuItem->new($menu, $id, $string, $description // '', $kind // 0);
    $self->set_menu_item_icon($item, $icon);
    $menu->Append($item);
    
    EVT_MENU($self, $id, $cb);
    return $item;
}

sub append_submenu {
    my ($self, $menu, $string, $description, $submenu, $id, $icon) = @_;
    
    $id //= &Wx::NewId();
    my $item = Wx::MenuItem->new($menu, $id, $string, $description // '');
    $self->set_menu_item_icon($item, $icon);
    $item->SetSubMenu($submenu);
    $menu->Append($item);
    
    return $item;
}

sub set_menu_item_icon {
    my ($self, $menuItem, $icon) = @_;
    
    # SetBitmap was not available on OS X before Wx 0.9927
    if ($icon && $menuItem->can('SetBitmap')) {
        $menuItem->SetBitmap(Wx::Bitmap->new(Slic3r::var($icon), wxBITMAP_TYPE_PNG));
    }
}

sub save_window_pos {
    my ($self, $window, $name) = @_;
    
    $Settings->{_}{"${name}_pos"}  = join ',', $window->GetScreenPositionXY;
    $Settings->{_}{"${name}_size"} = join ',', $window->GetSizeWH;
    $Settings->{_}{"${name}_maximized"}      = $window->IsMaximized;
    $self->save_settings;
}

sub restore_window_pos {
    my ($self, $window, $name) = @_;
    
    if (defined $Settings->{_}{"${name}_pos"}) {
        my $size = [ split ',', $Settings->{_}{"${name}_size"}, 2 ];
        $window->SetSize($size);
        
        my $display = Wx::Display->new->GetClientArea();
        my $pos = [ split ',', $Settings->{_}{"${name}_pos"}, 2 ];
        if (($pos->[0] + $size->[0]/2) < $display->GetRight && ($pos->[1] + $size->[1]/2) < $display->GetBottom) {
            $window->Move($pos);
        }
        $window->Maximize(1) if $Settings->{_}{"${name}_maximized"};
    }
}

1;
