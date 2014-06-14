package Slic3r::GUI::MainFrame;
use strict;
use warnings;
use utf8;

use Wx qw(:frame :bitmap :id :misc :notebook :panel :sizer :menu);
use Wx::Event qw(EVT_CLOSE EVT_MENU);
use base 'Wx::Frame';

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

use constant MI_OBJECT_REMOVE       => &Wx::NewId;
use constant MI_OBJECT_MORE         => &Wx::NewId;
use constant MI_OBJECT_FEWER        => &Wx::NewId;
use constant MI_OBJECT_ROTATE_45CW  => &Wx::NewId;
use constant MI_OBJECT_ROTATE_45CCW => &Wx::NewId;
use constant MI_OBJECT_ROTATE       => &Wx::NewId;
use constant MI_OBJECT_SCALE        => &Wx::NewId;
use constant MI_OBJECT_SPLIT        => &Wx::NewId;
use constant MI_OBJECT_VIEWCUT      => &Wx::NewId;
use constant MI_OBJECT_SETTINGS     => &Wx::NewId;

use constant MI_TAB_PLATER    => &Wx::NewId;
use constant MI_TAB_PRINT     => &Wx::NewId;
use constant MI_TAB_FILAMENT  => &Wx::NewId;
use constant MI_TAB_PRINTER   => &Wx::NewId;

use constant MI_CONF_WIZARD   => &Wx::NewId;
use constant MI_WEBSITE       => &Wx::NewId;
use constant MI_VERSIONCHECK  => &Wx::NewId;
use constant MI_DOCUMENTATION => &Wx::NewId;

sub new {
    my ($class, %params) = @_;
    
    my $self = Wx::Frame->new(undef, -1, 'Slic3r', wxDefaultPosition, [760, 470], wxDEFAULT_FRAME_STYLE);
    $self->SetIcon(Wx::Icon->new("$Slic3r::var/Slic3r_128px.png", wxBITMAP_TYPE_PNG) );
    $self->{skeinpanel} = Slic3r::GUI::SkeinPanel->new($self,
        mode        => $params{mode},
        no_plater   => $params{no_plater},
    );
    
    # status bar
    $self->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($self, -1);
    $self->{statusbar}->SetStatusText("Version $Slic3r::VERSION - Remember to check for updates at http://slic3r.org/");
    $self->SetStatusBar($self->{statusbar});
    
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
        EVT_MENU($self, MI_LOAD_CONF, sub { $self->{skeinpanel}->load_config_file });
        EVT_MENU($self, MI_LOAD_CONFBUNDLE, sub { $self->{skeinpanel}->load_configbundle });
        EVT_MENU($self, MI_EXPORT_CONF, sub { $self->{skeinpanel}->export_config });
        EVT_MENU($self, MI_EXPORT_CONFBUNDLE, sub { $self->{skeinpanel}->export_configbundle });
        EVT_MENU($self, MI_QUICK_SLICE, sub { $self->{skeinpanel}->quick_slice;
                                               $repeat->Enable(defined $Slic3r::GUI::SkeinPanel::last_input_file) });
        EVT_MENU($self, MI_REPEAT_QUICK, sub { $self->{skeinpanel}->quick_slice(reslice => 1) });
        EVT_MENU($self, MI_QUICK_SAVE_AS, sub { $self->{skeinpanel}->quick_slice(save_as => 1);
                                                 $repeat->Enable(defined $Slic3r::GUI::SkeinPanel::last_input_file) });
        EVT_MENU($self, MI_SLICE_SVG, sub { $self->{skeinpanel}->quick_slice(save_as => 1, export_svg => 1) });
        EVT_MENU($self, MI_REPAIR_STL, sub { $self->{skeinpanel}->repair_stl });
        EVT_MENU($self, MI_COMBINE_STLS, sub { $self->{skeinpanel}->combine_stls });
        EVT_MENU($self, wxID_PREFERENCES, sub { Slic3r::GUI::Preferences->new($self)->ShowModal });
        EVT_MENU($self, wxID_EXIT, sub {$_[0]->Close(0)});
    }
    
    # Plater menu
    unless ($params{no_plater}) {
        my $plater = $self->{skeinpanel}{plater};
        
        $self->{plater_menu} = Wx::Menu->new;
        $self->{plater_menu}->Append(MI_PLATER_EXPORT_GCODE, "Export G-code...", 'Export current plate as G-code');
        $self->{plater_menu}->Append(MI_PLATER_EXPORT_STL, "Export STL...", 'Export current plate as STL');
        $self->{plater_menu}->Append(MI_PLATER_EXPORT_AMF, "Export AMF...", 'Export current plate as AMF');
        EVT_MENU($self, MI_PLATER_EXPORT_GCODE, sub { $plater->export_gcode });
        EVT_MENU($self, MI_PLATER_EXPORT_STL, sub { $plater->export_stl });
        EVT_MENU($self, MI_PLATER_EXPORT_AMF, sub { $plater->export_amf });
        
        $self->{object_menu} = Wx::Menu->new;
        $self->{object_menu}->Append(MI_OBJECT_REMOVE, "Delete\tCtrl+Del", 'Remove the selected object');
        $self->{object_menu}->Append(MI_OBJECT_MORE, "Increase copies\tCtrl++", 'Place one more copy of the selected object');
        $self->{object_menu}->Append(MI_OBJECT_FEWER, "Decrease copies\tCtrl+-", 'Remove one copy of the selected object');
        $self->{object_menu}->AppendSeparator();
        $self->{object_menu}->Append(MI_OBJECT_ROTATE_45CW, "Rotate 45° clockwise", 'Rotate the selected object by 45° clockwise');
        $self->{object_menu}->Append(MI_OBJECT_ROTATE_45CCW, "Rotate 45° counter-clockwise", 'Rotate the selected object by 45° counter-clockwise');
        $self->{object_menu}->Append(MI_OBJECT_ROTATE, "Rotate…", 'Rotate the selected object by an arbitrary angle around Z axis');
        $self->{object_menu}->Append(MI_OBJECT_SCALE, "Scale…", 'Scale the selected object by an arbitrary factor');
        $self->{object_menu}->Append(MI_OBJECT_SPLIT, "Split", 'Split the selected object into individual parts');
        $self->{object_menu}->Append(MI_OBJECT_VIEWCUT, "View/Cut…", 'Open the 3D cutting tool');
        $self->{object_menu}->AppendSeparator();
        $self->{object_menu}->Append(MI_OBJECT_SETTINGS, "Settings…", 'Open the object editor dialog');
        EVT_MENU($self, MI_OBJECT_REMOVE, sub { $plater->remove });
        EVT_MENU($self, MI_OBJECT_MORE, sub { $plater->increase });
        EVT_MENU($self, MI_OBJECT_FEWER, sub { $plater->decrease });
        EVT_MENU($self, MI_OBJECT_ROTATE_45CW, sub { $plater->rotate(-45) });
        EVT_MENU($self, MI_OBJECT_ROTATE_45CCW, sub { $plater->rotate(45) });
        EVT_MENU($self, MI_OBJECT_ROTATE, sub { $plater->rotate(undef) });
        EVT_MENU($self, MI_OBJECT_SCALE, sub { $plater->changescale });
        EVT_MENU($self, MI_OBJECT_SPLIT, sub { $plater->split_object });
        EVT_MENU($self, MI_OBJECT_VIEWCUT, sub { $plater->object_cut_dialog });
        EVT_MENU($self, MI_OBJECT_SETTINGS, sub { $plater->object_settings_dialog });
        $self->on_plater_selection_changed(0);
    }
    
    # Window menu
    my $windowMenu = Wx::Menu->new;
    {
        my $tab_count = $params{no_plater} ? 3 : 4;
        $windowMenu->Append(MI_TAB_PLATER, "Select &Plater Tab\tCtrl+1", 'Show the plater') unless $params{no_plater};
        $windowMenu->Append(MI_TAB_PRINT, "Select P&rint Settings Tab\tCtrl+2", 'Show the print settings');
        $windowMenu->Append(MI_TAB_FILAMENT, "Select &Filament Settings Tab\tCtrl+3", 'Show the filament settings');
        $windowMenu->Append(MI_TAB_PRINTER, "Select Print&er Settings Tab\tCtrl+4", 'Show the printer settings');
        EVT_MENU($self, MI_TAB_PLATER, sub { $self->{skeinpanel}->select_tab(0) }) unless $params{no_plater};
        EVT_MENU($self, MI_TAB_PRINT, sub { $self->{skeinpanel}->select_tab($tab_count-3) });
        EVT_MENU($self, MI_TAB_FILAMENT, sub { $self->{skeinpanel}->select_tab($tab_count-2) });
        EVT_MENU($self, MI_TAB_PRINTER, sub { $self->{skeinpanel}->select_tab($tab_count-1) });
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
        EVT_MENU($self, MI_CONF_WIZARD, sub { $self->{skeinpanel}->config_wizard });
        EVT_MENU($self, MI_WEBSITE, sub { Wx::LaunchDefaultBrowser('http://slic3r.org/') });
        EVT_MENU($self, MI_VERSIONCHECK, sub { Slic3r::GUI->check_version(manual => 1) });
        EVT_MENU($self, MI_DOCUMENTATION, sub { Wx::LaunchDefaultBrowser('http://manual.slic3r.org/') });
        EVT_MENU($self, wxID_ABOUT, \&about);
    }
    
    # menubar
    # assign menubar to frame after appending items, otherwise special items
    # will not be handled correctly
    {
        my $menubar = Wx::MenuBar->new;
        $menubar->Append($fileMenu, "&File");
        $menubar->Append($self->{plater_menu}, "&Plater") if $self->{plater_menu};
        $menubar->Append($self->{object_menu}, "&Object") if $self->{object_menu};
        $menubar->Append($windowMenu, "&Window");
        $menubar->Append($helpMenu, "&Help");
        $self->SetMenuBar($menubar);
    }
    
    EVT_CLOSE($self, sub {
        my (undef, $event) = @_;
        if ($event->CanVeto && !$self->{skeinpanel}->check_unsaved_changes) {
            $event->Veto;
            return;
        }
        $event->Skip;
    });
    
    $self->Fit;
    $self->SetMinSize($self->GetSize);
    $self->Show;
    $self->Layout;
    
    return $self;
}

sub on_plater_selection_changed {
    my ($self, $have_selection) = @_;
    
    return if !defined $self->{object_menu};
    $self->{object_menu}->Enable($_->GetId, $have_selection)
        for $self->{object_menu}->GetMenuItems;
}

1;
