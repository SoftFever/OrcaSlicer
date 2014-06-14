package Slic3r::GUI::MainFrame;
use strict;
use warnings;
use utf8;

use List::Util qw(min);
use Wx qw(:frame :bitmap :id :misc :notebook :panel :sizer :menu :dialog :filedialog
    :font :icon);
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

our $last_input_file;
our $last_output_file;
our $last_config;

sub new {
    my ($class, %params) = @_;
    
    my $self = $class->SUPER::new(undef, -1, 'Slic3r', wxDefaultPosition, [760, 470], wxDEFAULT_FRAME_STYLE);
    $self->SetIcon(Wx::Icon->new("$Slic3r::var/Slic3r_128px.png", wxBITMAP_TYPE_PNG) );
    
    # store input params
    $self->{mode} = $params{mode};
    $self->{mode} = 'expert' if $self->{mode} !~ /^(?:simple|expert)$/;
    $self->{no_plater} = $params{no_plater};
    $self->{loaded} = 0;
    
    # initialize tabpanel and menubar
    $self->_init_tabpanel;
    $self->_init_menubar;
    
    # initialize status bar
    $self->{statusbar} = Slic3r::GUI::ProgressStatusBar->new($self, -1);
    $self->{statusbar}->SetStatusText("Version $Slic3r::VERSION - Remember to check for updates at http://slic3r.org/");
    $self->SetStatusBar($self->{statusbar});
    
    $self->{loaded} = 1;
    
    # declare events
    EVT_CLOSE($self, sub {
        my (undef, $event) = @_;
        if ($event->CanVeto && !$self->check_unsaved_changes) {
            $event->Veto;
            return;
        }
        $event->Skip;
    });
    
    # initialize layout
    {
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($self->{tabpanel}, 1, wxEXPAND);
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
        $self->Fit;
        $self->SetMinSize($self->GetSize);
        $self->Show;
        $self->Layout;
    }
    
    return $self;
}

sub _init_tabpanel {
    my ($self) = @_;
    
    $self->{tabpanel} = my $panel = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    
    $panel->AddPage($self->{plater} = Slic3r::GUI::Plater->new($panel), "Plater")
        unless $self->{no_plater};
    $self->{options_tabs} = {};
    
    my $simple_config;
    if ($self->{mode} eq 'simple') {
        $simple_config = Slic3r::Config->load("$Slic3r::GUI::datadir/simple.ini")
            if -e "$Slic3r::GUI::datadir/simple.ini";
    }
    
    my $class_prefix = $self->{mode} eq 'simple' ? "Slic3r::GUI::SimpleTab::" : "Slic3r::GUI::Tab::";
    for my $tab_name (qw(print filament printer)) {
        my $tab;
        $tab = $self->{options_tabs}{$tab_name} = ($class_prefix . ucfirst $tab_name)->new(
            $panel,
            on_value_change     => sub {
                $self->{plater}->on_config_change(@_) if $self->{plater}; # propagate config change events to the plater
                if ($self->{loaded}) {  # don't save while loading for the first time
                    if ($self->{mode} eq 'simple') {
                        # save config
                        $self->config->save("$Slic3r::GUI::datadir/simple.ini");
                        
                        # save a copy into each preset section
                        # so that user gets the config when switching to expert mode
                        $tab->config->save(sprintf "$Slic3r::GUI::datadir/%s/%s.ini", $tab->name, 'Simple Mode');
                        $Slic3r::GUI::Settings->{presets}{$tab->name} = 'Simple Mode.ini';
                        &Wx::wxTheApp->save_settings;
                    }
                    $self->config->save($Slic3r::GUI::autosave) if $Slic3r::GUI::autosave;
                }
            },
            on_presets_changed  => sub {
                $self->{plater}->update_presets($tab_name, @_) if $self->{plater};
            },
        );
        $panel->AddPage($tab, $tab->title);
        $tab->load_config($simple_config) if $simple_config;
    }
}

sub _init_menubar {
    my ($self) = @_;
    
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
        EVT_MENU($self, MI_LOAD_CONF, sub { $self->load_config_file });
        EVT_MENU($self, MI_LOAD_CONFBUNDLE, sub { $self->load_configbundle });
        EVT_MENU($self, MI_EXPORT_CONF, sub { $self->export_config });
        EVT_MENU($self, MI_EXPORT_CONFBUNDLE, sub { $self->export_configbundle });
        EVT_MENU($self, MI_QUICK_SLICE, sub { $self->quick_slice;
                                               $repeat->Enable(defined $Slic3r::GUI::MainFrame::last_input_file) });
        EVT_MENU($self, MI_REPEAT_QUICK, sub { $self->quick_slice(reslice => 1) });
        EVT_MENU($self, MI_QUICK_SAVE_AS, sub { $self->quick_slice(save_as => 1);
                                                 $repeat->Enable(defined $Slic3r::GUI::MainFrame::last_input_file) });
        EVT_MENU($self, MI_SLICE_SVG, sub { $self->quick_slice(save_as => 1, export_svg => 1) });
        EVT_MENU($self, MI_REPAIR_STL, sub { $self->repair_stl });
        EVT_MENU($self, MI_COMBINE_STLS, sub { $self->combine_stls });
        EVT_MENU($self, wxID_PREFERENCES, sub { Slic3r::GUI::Preferences->new($self)->ShowModal });
        EVT_MENU($self, wxID_EXIT, sub {$_[0]->Close(0)});
    }
    
    # Plater menu
    unless ($self->{no_plater}) {
        my $plater = $self->{plater};
        
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
        my $tab_count = $self->{no_plater} ? 3 : 4;
        $windowMenu->Append(MI_TAB_PLATER, "Select &Plater Tab\tCtrl+1", 'Show the plater') unless $self->{no_plater};
        $windowMenu->Append(MI_TAB_PRINT, "Select P&rint Settings Tab\tCtrl+2", 'Show the print settings');
        $windowMenu->Append(MI_TAB_FILAMENT, "Select &Filament Settings Tab\tCtrl+3", 'Show the filament settings');
        $windowMenu->Append(MI_TAB_PRINTER, "Select Print&er Settings Tab\tCtrl+4", 'Show the printer settings');
        EVT_MENU($self, MI_TAB_PLATER, sub { $self->select_tab(0) }) unless $self->{no_plater};
        EVT_MENU($self, MI_TAB_PRINT, sub { $self->select_tab($tab_count-3) });
        EVT_MENU($self, MI_TAB_FILAMENT, sub { $self->select_tab($tab_count-2) });
        EVT_MENU($self, MI_TAB_PRINTER, sub { $self->select_tab($tab_count-1) });
    }
    
    # Help menu
    my $helpMenu = Wx::Menu->new;
    {
        $helpMenu->Append(MI_CONF_WIZARD, "&Configuration $Slic3r::GUI::ConfigWizard::wizard…", "Run Configuration $Slic3r::GUI::ConfigWizard::wizard");
        $helpMenu->AppendSeparator();
        $helpMenu->Append(MI_WEBSITE, "Slic3r &Website", 'Open the Slic3r website in your browser');
        my $versioncheck = $helpMenu->Append(MI_VERSIONCHECK, "Check for &Updates...", 'Check for new Slic3r versions');
        $versioncheck->Enable(&Wx::wxTheApp->have_version_check);
        $helpMenu->Append(MI_DOCUMENTATION, "Slic3r &Manual", 'Open the Slic3r manual in your browser');
        $helpMenu->AppendSeparator();
        $helpMenu->Append(wxID_ABOUT, "&About Slic3r", 'Show about dialog');
        EVT_MENU($self, MI_CONF_WIZARD, sub { $self->config_wizard });
        EVT_MENU($self, MI_WEBSITE, sub { Wx::LaunchDefaultBrowser('http://slic3r.org/') });
        EVT_MENU($self, MI_VERSIONCHECK, sub { &Wx::wxTheApp->check_version(manual => 1) });
        EVT_MENU($self, MI_DOCUMENTATION, sub { Wx::LaunchDefaultBrowser('http://manual.slic3r.org/') });
        EVT_MENU($self, wxID_ABOUT, sub { &Wx::wxTheApp->about });
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
}

sub is_loaded {
    my ($self) = @_;
    return $self->{loaded};
}

sub on_plater_selection_changed {
    my ($self, $have_selection) = @_;
    
    return if !defined $self->{object_menu};
    $self->{object_menu}->Enable($_->GetId, $have_selection)
        for $self->{object_menu}->GetMenuItems;
}

sub quick_slice {
    my $self = shift;
    my %params = @_;
    
    my $progress_dialog;
    eval {
        # validate configuration
        my $config = $self->config;
        $config->validate;
        
        # select input file
        my $input_file;
        my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory} || $Slic3r::GUI::Settings->{recent}{config_directory} || '';
        if (!$params{reslice}) {
            my $dialog = Wx::FileDialog->new($self, 'Choose a file to slice (STL/OBJ/AMF):', $dir, "", &Slic3r::GUI::MODEL_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if ($dialog->ShowModal != wxID_OK) {
                $dialog->Destroy;
                return;
            }
            $input_file = $dialog->GetPaths;
            $dialog->Destroy;
            $last_input_file = $input_file unless $params{export_svg};
        } else {
            if (!defined $last_input_file) {
                Wx::MessageDialog->new($self, "No previously sliced file.",
                                       'Error', wxICON_ERROR | wxOK)->ShowModal();
                return;
            }
            if (! -e $last_input_file) {
                Wx::MessageDialog->new($self, "Previously sliced file ($last_input_file) not found.",
                                       'File Not Found', wxICON_ERROR | wxOK)->ShowModal();
                return;
            }
            $input_file = $last_input_file;
        }
        my $input_file_basename = basename($input_file);
        $Slic3r::GUI::Settings->{recent}{skein_directory} = dirname($input_file);
        &Wx::wxTheApp->save_settings;
        
        my $sprint = Slic3r::Print::Simple->new(
            status_cb       => sub {
                my ($percent, $message) = @_;
                return if &Wx::wxVERSION_STRING !~ / 2\.(8\.|9\.[2-9])/;
                $progress_dialog->Update($percent, "$message…");
            },
        );
        
        $sprint->apply_config($config);
        $sprint->set_model(Slic3r::Model->read_from_file($input_file));
        
        {
            my $extra = $self->extra_variables;
            $sprint->placeholder_parser->set($_, $extra->{$_}) for keys %$extra;
        }
        
        # select output file
        my $output_file;
        if ($params{reslice}) {
            $output_file = $last_output_file if defined $last_output_file;
        } elsif ($params{save_as}) {
            $output_file = $sprint->expanded_output_filepath;
            $output_file =~ s/\.gcode$/.svg/i if $params{export_svg};
            my $dlg = Wx::FileDialog->new($self, 'Save ' . ($params{export_svg} ? 'SVG' : 'G-code') . ' file as:',
                &Wx::wxTheApp->output_path(dirname($output_file)),
                basename($output_file), $params{export_svg} ? &Slic3r::GUI::FILE_WILDCARDS->{svg} : &Slic3r::GUI::FILE_WILDCARDS->{gcode}, wxFD_SAVE);
            if ($dlg->ShowModal != wxID_OK) {
                $dlg->Destroy;
                return;
            }
            $output_file = $dlg->GetPath;
            $last_output_file = $output_file unless $params{export_svg};
            $Slic3r::GUI::Settings->{_}{last_output_path} = dirname($output_file);
            &Wx::wxTheApp->save_settings;
            $dlg->Destroy;
        }
        
        # show processbar dialog
        $progress_dialog = Wx::ProgressDialog->new('Slicing…', "Processing $input_file_basename…", 
            100, $self, 0);
        $progress_dialog->Pulse;
        
        {
            my @warnings = ();
            local $SIG{__WARN__} = sub { push @warnings, $_[0] };
            
            $sprint->output_file($output_file);
            if ($params{export_svg}) {
                $sprint->export_svg;
            } else {
                $sprint->export_gcode;
            }
            $sprint->status_cb(undef);
            Slic3r::GUI::warning_catcher($self)->($_) for @warnings;
        }
        $progress_dialog->Destroy;
        undef $progress_dialog;
        
        my $message = "$input_file_basename was successfully sliced.";
        &Wx::wxTheApp->notify($message);
        Wx::MessageDialog->new($self, $message, 'Slicing Done!', 
            wxOK | wxICON_INFORMATION)->ShowModal;
    };
    Slic3r::GUI::catch_error($self, sub { $progress_dialog->Destroy if $progress_dialog });
}

sub repair_stl {
    my $self = shift;
    
    my $input_file;
    {
        my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory} || $Slic3r::GUI::Settings->{recent}{config_directory} || '';
        my $dialog = Wx::FileDialog->new($self, 'Select the STL file to repair:', $dir, "", &Slic3r::GUI::FILE_WILDCARDS->{stl}, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if ($dialog->ShowModal != wxID_OK) {
            $dialog->Destroy;
            return;
        }
        $input_file = $dialog->GetPaths;
        $dialog->Destroy;
    }
    
    my $output_file = $input_file;
    {
        $output_file =~ s/\.stl$/_fixed.obj/i;
        my $dlg = Wx::FileDialog->new($self, "Save OBJ file (less prone to coordinate errors than STL) as:", dirname($output_file),
            basename($output_file), &Slic3r::GUI::FILE_WILDCARDS->{obj}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return undef;
        }
        $output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    my $tmesh = Slic3r::TriangleMesh->new;
    $tmesh->ReadSTLFile(Slic3r::encode_path($input_file));
    $tmesh->repair;
    $tmesh->WriteOBJFile(Slic3r::encode_path($output_file));
    Slic3r::GUI::show_info($self, "Your file was repaired.", "Repair");
}

sub extra_variables {
    my $self = shift;
    
    my %extra_variables = ();
    if ($self->{mode} eq 'expert') {
        $extra_variables{"${_}_preset"} = $self->{options_tabs}{$_}->current_preset->{name}
            for qw(print filament printer);
    }
    return { %extra_variables };
}

sub export_config {
    my $self = shift;
    
    my $config = $self->config;
    eval {
        # validate configuration
        $config->validate;
    };
    Slic3r::GUI::catch_error($self) and return;
    
    my $dir = $last_config ? dirname($last_config) : $Slic3r::GUI::Settings->{recent}{config_directory} || $Slic3r::GUI::Settings->{recent}{skein_directory} || '';
    my $filename = $last_config ? basename($last_config) : "config.ini";
    my $dlg = Wx::FileDialog->new($self, 'Save configuration as:', $dir, $filename, 
        &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if ($dlg->ShowModal == wxID_OK) {
        my $file = $dlg->GetPath;
        $Slic3r::GUI::Settings->{recent}{config_directory} = dirname($file);
        &Wx::wxTheApp->save_settings;
        $last_config = $file;
        $config->save($file);
    }
    $dlg->Destroy;
}

sub load_config_file {
    my $self = shift;
    my ($file) = @_;
    
    if (!$file) {
        return unless $self->check_unsaved_changes;
        my $dir = $last_config ? dirname($last_config) : $Slic3r::GUI::Settings->{recent}{config_directory} || $Slic3r::GUI::Settings->{recent}{skein_directory} || '';
        my $dlg = Wx::FileDialog->new($self, 'Select configuration to load:', $dir, "config.ini", 
                &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        return unless $dlg->ShowModal == wxID_OK;
        ($file) = $dlg->GetPaths;
        $dlg->Destroy;
    }
    $Slic3r::GUI::Settings->{recent}{config_directory} = dirname($file);
    &Wx::wxTheApp->save_settings;
    $last_config = $file;
    for my $tab (values %{$self->{options_tabs}}) {
        $tab->load_config_file($file);
    }
}

sub export_configbundle {
    my $self = shift;
    
    eval {
        # validate current configuration in case it's dirty
        $self->config->validate;
    };
    Slic3r::GUI::catch_error($self) and return;
    
    my $dir = $last_config ? dirname($last_config) : $Slic3r::GUI::Settings->{recent}{config_directory} || $Slic3r::GUI::Settings->{recent}{skein_directory} || '';
    my $filename = "Slic3r_config_bundle.ini";
    my $dlg = Wx::FileDialog->new($self, 'Save presets bundle as:', $dir, $filename, 
        &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if ($dlg->ShowModal == wxID_OK) {
        my $file = $dlg->GetPath;
        $Slic3r::GUI::Settings->{recent}{config_directory} = dirname($file);
        &Wx::wxTheApp->save_settings;
        
        # leave default category empty to prevent the bundle from being parsed as a normal config file
        my $ini = { _ => {} };
        $ini->{settings}{$_} = $Slic3r::GUI::Settings->{_}{$_} for qw(autocenter mode);
        $ini->{presets} = $Slic3r::GUI::Settings->{presets};
        if (-e "$Slic3r::GUI::datadir/simple.ini") {
            my $config = Slic3r::Config->load("$Slic3r::GUI::datadir/simple.ini");
            $ini->{simple} = $config->as_ini->{_};
        }
        
        foreach my $section (qw(print filament printer)) {
            my %presets = &Wx::wxTheApp->presets($section);
            foreach my $preset_name (keys %presets) {
                my $config = Slic3r::Config->load($presets{$preset_name});
                $ini->{"$section:$preset_name"} = $config->as_ini->{_};
            }
        }
        
        Slic3r::Config->write_ini($file, $ini);
    }
    $dlg->Destroy;
}

sub load_configbundle {
    my $self = shift;
    
    my $dir = $last_config ? dirname($last_config) : $Slic3r::GUI::Settings->{recent}{config_directory} || $Slic3r::GUI::Settings->{recent}{skein_directory} || '';
    my $dlg = Wx::FileDialog->new($self, 'Select configuration to load:', $dir, "config.ini", 
            &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    return unless $dlg->ShowModal == wxID_OK;
    my ($file) = $dlg->GetPaths;
    $dlg->Destroy;
    
    $Slic3r::GUI::Settings->{recent}{config_directory} = dirname($file);
    &Wx::wxTheApp->save_settings;
    
    # load .ini file
    my $ini = Slic3r::Config->read_ini($file);
    
    if ($ini->{settings}) {
        $Slic3r::GUI::Settings->{_}{$_} = $ini->{settings}{$_} for keys %{$ini->{settings}};
        &Wx::wxTheApp->save_settings;
    }
    if ($ini->{presets}) {
        $Slic3r::GUI::Settings->{presets} = $ini->{presets};
        &Wx::wxTheApp->save_settings;
    }
    if ($ini->{simple}) {
        my $config = Slic3r::Config->load_ini_hash($ini->{simple});
        $config->save("$Slic3r::GUI::datadir/simple.ini");
        if ($self->{mode} eq 'simple') {
            foreach my $tab (values %{$self->{options_tabs}}) {
                $tab->load_config($config) for values %{$self->{options_tabs}};
            }
        }
    }
    my $imported = 0;
    foreach my $ini_category (sort keys %$ini) {
        next unless $ini_category =~ /^(print|filament|printer):(.+)$/;
        my ($section, $preset_name) = ($1, $2);
        my $config = Slic3r::Config->load_ini_hash($ini->{$ini_category});
        $config->save(sprintf "$Slic3r::GUI::datadir/%s/%s.ini", $section, $preset_name);
        $imported++;
    }
    if ($self->{mode} eq 'expert') {
        foreach my $tab (values %{$self->{options_tabs}}) {
            $tab->load_presets;
        }
    }
    my $message = sprintf "%d presets successfully imported.", $imported;
    if ($self->{mode} eq 'simple' && $Slic3r::GUI::Settings->{_}{mode} eq 'expert') {
        Slic3r::GUI::show_info($self, "$message You need to restart Slic3r to make the changes effective.");
    } else {
        Slic3r::GUI::show_info($self, $message);
    }
}

sub load_config {
    my $self = shift;
    my ($config) = @_;
    
    foreach my $tab (values %{$self->{options_tabs}}) {
        $tab->set_value($_, $config->$_) for @{$config->get_keys};
    }
}

sub config_wizard {
    my $self = shift;

    return unless $self->check_unsaved_changes;
    if (my $config = Slic3r::GUI::ConfigWizard->new($self)->run) {
        if ($self->{mode} eq 'expert') {
            for my $tab (values %{$self->{options_tabs}}) {
                $tab->select_default_preset;
            }
        }
        $self->load_config($config);
        if ($self->{mode} eq 'expert') {
            for my $tab (values %{$self->{options_tabs}}) {
                $tab->save_preset('My Settings');
            }
        }
    }
}

sub combine_stls {
    my $self = shift;
    
    # get input files
    my @input_files = ();
    my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory} || '';
    {
        my $dlg_message = 'Choose one or more files to combine (STL/OBJ)';
        while (1) {
            my $dialog = Wx::FileDialog->new($self, "$dlg_message:", $dir, "", &Slic3r::GUI::MODEL_WILDCARD, 
                wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
            if ($dialog->ShowModal != wxID_OK) {
                $dialog->Destroy;
                last;
            }
            push @input_files, $dialog->GetPaths;
            $dialog->Destroy;
            $dlg_message .= " or hit Cancel if you have finished";
            $dir = dirname($input_files[0]);
        }
        return if !@input_files;
    }
    
    # get output file
    my $output_file = $input_files[0];
    {
        $output_file =~ s/\.(?:stl|obj)$/.amf.xml/i;
        my $dlg = Wx::FileDialog->new($self, 'Save multi-material AMF file as:', dirname($output_file),
            basename($output_file), &Slic3r::GUI::FILE_WILDCARDS->{amf}, wxFD_SAVE);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return;
        }
        $output_file = $dlg->GetPath;
    }
    
    my @models = eval { map Slic3r::Model->read_from_file($_), @input_files };
    Slic3r::GUI::show_error($self, $@) if $@;
    
    my $new_model = Slic3r::Model->new;
    my $new_object = $new_model->add_object;
    for my $m (0 .. $#models) {
        my $model = $models[$m];
        
        my $material_name = basename($input_files[$m]);
        $material_name =~ s/\.(stl|obj)$//i;
        
        $new_model->set_material($m, { Name => $material_name });
        $new_object->add_volume(
            material_id => $m,
            mesh        => $model->objects->[0]->volumes->[0]->mesh,
        );
    }
    
    Slic3r::Format::AMF->write_file($output_file, $new_model);
}

=head2 config

This method collects all config values from the tabs and merges them into a single config object.

=cut

sub config {
    my $self = shift;
    
    # retrieve filament presets and build a single config object for them
    my $filament_config;
    if (!$self->{plater} || $self->{plater}->filament_presets == 1 || $self->{mode} eq 'simple') {
        $filament_config = $self->{options_tabs}{filament}->config;
    } else {
        # TODO: handle dirty presets.
        # perhaps plater shouldn't expose dirty presets at all in multi-extruder environments.
        my $i = -1;
        foreach my $preset_idx ($self->{plater}->filament_presets) {
            $i++;
            my $preset = $self->{options_tabs}{filament}->get_preset($preset_idx);
            my $config = $self->{options_tabs}{filament}->get_preset_config($preset);
            if (!$filament_config) {
                $filament_config = $config->clone;
                next;
            }
            foreach my $opt_key (@{$config->get_keys}) {
                my $value = $filament_config->get($opt_key);
                next unless ref $value eq 'ARRAY';
                $value->[$i] = $config->get($opt_key)->[0];
                $filament_config->set($opt_key, $value);
            }
        }
    }
    
    my $config = Slic3r::Config->merge(
        Slic3r::Config->new_from_defaults,
        $self->{options_tabs}{print}->config,
        $self->{options_tabs}{printer}->config,
        $filament_config,
    );
    
    if ($self->{mode} eq 'simple') {
        # set some sensible defaults
        $config->set('first_layer_height', $config->nozzle_diameter->[0]);
        $config->set('avoid_crossing_perimeters', 1);
        $config->set('infill_every_layers', 10);
    } else {
        my $extruders_count = $self->{options_tabs}{printer}{extruders_count};
        $config->set("${_}_extruder", min($config->get("${_}_extruder"), $extruders_count))
            for qw(perimeter infill support_material support_material_interface);
    }
    
    return $config;
}

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    my $changed = 0;
    foreach my $tab (values %{$self->{options_tabs}}) {
        $changed = 1 if $tab->set_value($opt_key, $value);
    }
    return $changed;
}

sub check_unsaved_changes {
    my $self = shift;
    
    my @dirty = map $_->title, grep $_->is_dirty, values %{$self->{options_tabs}};
    if (@dirty) {
        my $titles = join ', ', @dirty;
        my $confirm = Wx::MessageDialog->new($self, "You have unsaved changes ($titles). Discard changes and continue anyway?",
                                             'Unsaved Presets', wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
        return ($confirm->ShowModal == wxID_YES);
    }
    
    return 1;
}

sub select_tab {
    my ($self, $tab) = @_;
    $self->{tabpanel}->ChangeSelection($tab);
}

1;
