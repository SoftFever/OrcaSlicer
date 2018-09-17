package Slic3r::GUI;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use FindBin;
use List::Util qw(first);
use Slic3r::GUI::2DBed;
use Slic3r::GUI::Controller;
use Slic3r::GUI::Controller::ManualControlDialog;
use Slic3r::GUI::Controller::PrinterPanel;
use Slic3r::GUI::MainFrame;
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
use Slic3r::GUI::ProgressStatusBar;
use Slic3r::GUI::OptionsGroup;
use Slic3r::GUI::OptionsGroup::Field;
use Slic3r::GUI::SystemInfo;

use Wx::Locale gettext => 'L';

our $have_OpenGL = eval "use Slic3r::GUI::3DScene; 1";

use Wx 0.9901 qw(:bitmap :dialog :icon :id :misc :systemsettings :toplevelwindow :filedialog :font);
use Wx::Event qw(EVT_IDLE EVT_COMMAND EVT_MENU);
use base 'Wx::App';

use constant FILE_WILDCARDS => {
    known   => 'Known files (*.stl, *.obj, *.amf, *.xml, *.3mf, *.prusa)|*.stl;*.STL;*.obj;*.OBJ;*.zip.amf;*.amf;*.AMF;*.xml;*.XML;*.3mf;*.3MF;*.prusa;*.PRUSA',
    stl     => 'STL files (*.stl)|*.stl;*.STL',
    obj     => 'OBJ files (*.obj)|*.obj;*.OBJ',
    amf     => 'AMF files (*.amf)|*.zip.amf;*.amf;*.AMF;*.xml;*.XML',
    threemf => '3MF files (*.3mf)|*.3mf;*.3MF',
    prusa   => 'Prusa Control files (*.prusa)|*.prusa;*.PRUSA',
    ini     => 'INI files *.ini|*.ini;*.INI',
    gcode   => 'G-code files (*.gcode, *.gco, *.g, *.ngc)|*.gcode;*.GCODE;*.gco;*.GCO;*.g;*.G;*.ngc;*.NGC',
    svg     => 'SVG files *.svg|*.svg;*.SVG',
};
use constant MODEL_WILDCARD => join '|', @{&FILE_WILDCARDS}{qw(known stl obj amf threemf prusa)};

# Datadir provided on the command line.
our $datadir;
# If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
our $no_plater;
our @cb;

our $small_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11) if &Wx::wxMAC;
our $small_bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$small_bold_font->SetPointSize(11) if &Wx::wxMAC;
$small_bold_font->SetWeight(wxFONTWEIGHT_BOLD);
our $medium_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
$medium_font->SetPointSize(12);
our $grey = Wx::Colour->new(200,200,200);

# Events to be sent from a C++ menu implementation:
# 1) To inform about a change of the application language.
our $LANGUAGE_CHANGE_EVENT  = Wx::NewEventType;
# 2) To inform about a change of Preferences.
our $PREFERENCES_EVENT      = Wx::NewEventType;
# To inform AppConfig about Slic3r version available online
our $VERSION_ONLINE_EVENT   = Wx::NewEventType;

sub OnInit {
    my ($self) = @_;
    
    $self->SetAppName('Slic3rPE');
    $self->SetAppDisplayName('Slic3r Prusa Edition');
    Slic3r::debugf "wxWidgets version %s, Wx version %s\n", &Wx::wxVERSION_STRING, $Wx::VERSION;

    # Set the Slic3r data directory at the Slic3r XS module.
    # Unix: ~/.Slic3r
    # Windows: "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
    # Mac: "~/Library/Application Support/Slic3r"
    Slic3r::set_data_dir($datadir || Wx::StandardPaths::Get->GetUserDataDir);
    Slic3r::GUI::set_wxapp($self);

    $self->{app_config} = Slic3r::GUI::AppConfig->new;
    Slic3r::GUI::set_app_config($self->{app_config});
    $self->{preset_bundle} = Slic3r::GUI::PresetBundle->new;
    Slic3r::GUI::set_preset_bundle($self->{preset_bundle});

    # just checking for existence of Slic3r::data_dir is not enough: it may be an empty directory
    # supplied as argument to --datadir; in that case we should still run the wizard
    eval { $self->{preset_bundle}->setup_directories() };
    if ($@) {
        warn $@ . "\n";
        fatal_error(undef, $@);
    }
    my $app_conf_exists = $self->{app_config}->exists;
    # load settings
    $self->{app_config}->load if $app_conf_exists;
    $self->{app_config}->set('version', $Slic3r::VERSION);
    $self->{app_config}->save;

    $self->{preset_updater} = Slic3r::PresetUpdater->new($VERSION_ONLINE_EVENT);
    Slic3r::GUI::set_preset_updater($self->{preset_updater});

    Slic3r::GUI::load_language();

    # Suppress the '- default -' presets.
    $self->{preset_bundle}->set_default_suppressed($self->{app_config}->get('no_defaults') ? 1 : 0);
    eval { $self->{preset_bundle}->load_presets($self->{app_config}); };
    if ($@) {
        warn $@ . "\n";
        show_error(undef, $@);
    }

    # application frame
    print STDERR "Creating main frame...\n";
    Wx::Image::FindHandlerType(wxBITMAP_TYPE_PNG) || Wx::Image::AddHandler(Wx::PNGHandler->new);
    $self->{mainframe} = my $frame = Slic3r::GUI::MainFrame->new(
        # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        no_controller   => $self->{app_config}->get('no_controller'),
        no_plater       => $no_plater,
        lang_ch_event   => $LANGUAGE_CHANGE_EVENT,
        preferences_event => $PREFERENCES_EVENT,
    );
    $self->SetTopWindow($frame);

    # This makes CallAfter() work
    EVT_IDLE($self->{mainframe}, sub {
        while (my $cb = shift @cb) {
            $cb->();
        }
        $self->{app_config}->save if $self->{app_config}->dirty;
    });

    # On OS X the UI tends to freeze in weird ways if modal dialogs (config wizard, update notifications, ...)
    # are shown before or in the same event callback with the main frame creation.
    # Therefore we schedule them for later using CallAfter.
    $self->CallAfter(sub {
        eval {
            if (! $self->{preset_updater}->config_update()) {
                $self->{mainframe}->Close;
            }
        };
        if ($@) {
            show_error(undef, $@);
            $self->{mainframe}->Close;
        }
    });

    $self->CallAfter(sub {
        if (! Slic3r::GUI::config_wizard_startup($app_conf_exists)) {
            # Only notify if there was not wizard so as not to bother too much ...
            $self->{preset_updater}->slic3r_update_notify();
        }
        $self->{preset_updater}->sync($self->{preset_bundle});
    });

    # The following event is emited by the C++ menu implementation of application language change.
    EVT_COMMAND($self, -1, $LANGUAGE_CHANGE_EVENT, sub{
        print STDERR "LANGUAGE_CHANGE_EVENT\n";
        $self->recreate_GUI;
    });

    # The following event is emited by the C++ menu implementation of preferences change.
    EVT_COMMAND($self, -1, $PREFERENCES_EVENT, sub{
        $self->update_ui_from_settings;
    });
    
    # The following event is emited by PresetUpdater (C++) to inform about
    # the newer Slic3r application version avaiable online.
    EVT_COMMAND($self, -1, $VERSION_ONLINE_EVENT, sub {
        my ($self, $event) = @_;
        my $version = $event->GetString;
        $self->{app_config}->set('version_online', $version);
        $self->{app_config}->save;
    });

    return 1;
}

sub recreate_GUI{
    print STDERR "recreate_GUI\n";
    my ($self) = @_;
    my $topwindow = $self->GetTopWindow();
    $self->{mainframe} = my $frame = Slic3r::GUI::MainFrame->new(
        # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        no_controller   => $self->{app_config}->get('no_controller'),
        no_plater       => $no_plater,
        lang_ch_event   => $LANGUAGE_CHANGE_EVENT,
        preferences_event => $PREFERENCES_EVENT,
    );

    if($topwindow)
    {
        $self->SetTopWindow($frame);
        $topwindow->Destroy;
    }

    EVT_IDLE($self->{mainframe}, sub {
        while (my $cb = shift @cb) {
            $cb->();
        }
        $self->{app_config}->save if $self->{app_config}->dirty;
    });

    # On OSX the UI was not initialized correctly if the wizard was called
    # before the UI was up and running.
    $self->CallAfter(sub {
        # Run the config wizard, don't offer the "reset user profile" checkbox.
        Slic3r::GUI::config_wizard_startup(1);
    });
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
        $opengl_info = Slic3r::GUI::_3DScene::get_gl_info(1, 1);
        $opengl_info_txt = Slic3r::GUI::_3DScene::get_gl_info(0, 1);
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
    Slic3r::GUI::show_error_id($parent ? $parent->GetId() : 0, $message);
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

    # There used to be notifier using a Growl application for OSX, but Growl is dead.
    # The notifier also supported the Linux X D-bus notifications, but that support was broken.
    #TODO use wxNotificationMessage?
}

# Called after the Preferences dialog is closed and the program settings are saved.
# Update the UI based on the current preferences.
sub update_ui_from_settings {
    my ($self) = @_;
    $self->{mainframe}->update_ui_from_settings;
}

sub open_model {
    my ($self, $window) = @_;

    my $dlg_title = L('Choose one or more files (STL/OBJ/AMF/3MF/PRUSA):');   
    my $dialog = Wx::FileDialog->new($window // $self->GetTopWindow, $dlg_title, 
        $self->{app_config}->get_last_dir, "",
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

1;
