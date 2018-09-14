# The main frame, the parent of all.

package Slic3r::GUI::MainFrame;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use FindBin;
use List::Util qw(min first);
use Slic3r::Geometry qw(X Y);
use Wx qw(:frame :bitmap :id :misc :notebook :panel :sizer :menu :dialog :filedialog :dirdialog
    :font :icon wxTheApp);
use Wx::Event qw(EVT_CLOSE EVT_COMMAND EVT_MENU EVT_NOTEBOOK_PAGE_CHANGED);
use base 'Wx::Frame';

use Wx::Locale gettext => 'L';

our $qs_last_input_file;
our $qs_last_output_file;
our $last_config;
our $appController;

# Events to be sent from a C++ Tab implementation:
# 1) To inform about a change of a configuration value.
our $VALUE_CHANGE_EVENT    = Wx::NewEventType;
# 2) To inform about a preset selection change or a "modified" status change.
our $PRESETS_CHANGED_EVENT = Wx::NewEventType;
# 3) To update the status bar with the progress information.
our $PROGRESS_BAR_EVENT    = Wx::NewEventType;
# 4) To display an error dialog box from a thread on the UI thread.
our $ERROR_EVENT             = Wx::NewEventType;
# 5) To inform about a change of object selection
our $OBJECT_SELECTION_CHANGED_EVENT = Wx::NewEventType;
# 6) To inform about a change of object settings
our $OBJECT_SETTINGS_CHANGED_EVENT = Wx::NewEventType;
# 7) To inform about a remove of object 
our $OBJECT_REMOVE_EVENT = Wx::NewEventType;
# 8) To inform about a update of the scene 
our $UPDATE_SCENE_EVENT = Wx::NewEventType;

sub new {
    my ($class, %params) = @_;
        
    my $self = $class->SUPER::new(undef, -1, $Slic3r::FORK_NAME . ' - ' . $Slic3r::VERSION, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);
        Slic3r::GUI::set_main_frame($self);
        
    $appController = Slic3r::AppController->new();

    if ($^O eq 'MSWin32') {
        # Load the icon either from the exe, or from the ico file.
        my $iconfile = Slic3r::decode_path($FindBin::Bin) . '\slic3r.exe';
        $iconfile = Slic3r::var("Slic3r.ico") unless -f $iconfile;
        $self->SetIcon(Wx::Icon->new($iconfile, wxBITMAP_TYPE_ICO));
    } else {
        $self->SetIcon(Wx::Icon->new(Slic3r::var("Slic3r_128px.png"), wxBITMAP_TYPE_PNG));        
    }
        
    # store input params
    # If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
    $self->{no_controller} = $params{no_controller};
    $self->{no_plater} = $params{no_plater};
    $self->{loaded} = 0;
    $self->{lang_ch_event} = $params{lang_ch_event};
    $self->{preferences_event} = $params{preferences_event};

    # initialize tabpanel and menubar
    $self->_init_tabpanel;
    $self->_init_menubar;
    
    # set default tooltip timer in msec
    # SetAutoPop supposedly accepts long integers but some bug doesn't allow for larger values
    # (SetAutoPop is not available on GTK.)
    eval { Wx::ToolTip::SetAutoPop(32767) };
    
    # initialize status bar
    $self->{statusbar} = Slic3r::GUI::ProgressStatusBar->new();
    $self->{statusbar}->Embed;
    $self->{statusbar}->SetStatusText(L("Version ").$Slic3r::VERSION.L(" - Remember to check for updates at http://github.com/prusa3d/slic3r/releases"));
    # Make the global status bar and its progress indicator available in C++
    Slic3r::GUI::set_progress_status_bar($self->{statusbar});
    $appController->set_global_progress_indicator($self->{statusbar});

    $appController->set_model($self->{plater}->{model});
    $appController->set_print($self->{plater}->{print});

    $self->{plater}->{appController} = $appController;

    $self->{loaded} = 1;
        
    # initialize layout
    {
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($self->{tabpanel}, 1, wxEXPAND);
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
        $self->Fit;
        $self->SetMinSize([760, 490]);
        $self->SetSize($self->GetMinSize);
        wxTheApp->restore_window_pos($self, "main_frame");
        $self->Show;
        $self->Layout;
    }
    
    # declare events
    EVT_CLOSE($self, sub {
        my (undef, $event) = @_;
        if ($event->CanVeto && !Slic3r::GUI::check_unsaved_changes) {
            $event->Veto;
            return;
        }
        # save window size
        wxTheApp->save_window_pos($self, "main_frame");
        # Save the slic3r.ini. Usually the ini file is saved from "on idle" callback,
        # but in rare cases it may not have been called yet.
        wxTheApp->{app_config}->save;
        $self->{statusbar}->ResetCancelCallback();
        $self->{plater}->{print} = undef if($self->{plater});
        Slic3r::GUI::_3DScene::remove_all_canvases();
        Slic3r::GUI::deregister_on_request_update_callback();
        # propagate event
        $event->Skip;
    });

    $self->update_ui_from_settings;

    Slic3r::GUI::update_mode();

    return $self;
}

sub _init_tabpanel {
    my ($self) = @_;
    
    $self->{tabpanel} = my $panel = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    Slic3r::GUI::set_tab_panel($panel);

    EVT_NOTEBOOK_PAGE_CHANGED($self, $self->{tabpanel}, sub {
        my $panel = $self->{tabpanel}->GetCurrentPage;
        $panel->OnActivate if $panel->can('OnActivate');

        for my $tab_name (qw(print filament printer)) {
            Slic3r::GUI::get_preset_tab("$tab_name")->OnActivate if ("$tab_name" eq $panel->GetName);
        }
    });
    
    if (!$self->{no_plater}) {
        $panel->AddPage($self->{plater} = Slic3r::GUI::Plater->new($panel,
            event_object_selection_changed   => $OBJECT_SELECTION_CHANGED_EVENT,
            event_object_settings_changed    => $OBJECT_SETTINGS_CHANGED_EVENT,
            event_remove_object              => $OBJECT_REMOVE_EVENT,
            event_update_scene               => $UPDATE_SCENE_EVENT,
            ), L("Plater"));
    }
    
    #TODO this is an example of a Slic3r XS interface call to add a new preset editor page to the main view.
    # The following event is emited by the C++ Tab implementation on config value change.
    EVT_COMMAND($self, -1, $VALUE_CHANGE_EVENT, sub {
        my ($self, $event) = @_;
        my $str = $event->GetString;
        my ($opt_key, $name) = ($str =~ /(.*) (.*)/);
        #print "VALUE_CHANGE_EVENT: ", $opt_key, "\n";
        my $tab = Slic3r::GUI::get_preset_tab($name);
        my $config = $tab->get_config;
        if ($self->{plater}) {
            $self->{plater}->on_config_change($config); # propagate config change events to the plater
            if ($opt_key eq 'extruders_count'){
                my $value = $event->GetInt();
                $self->{plater}->on_extruders_change($value);
            }
            if ($opt_key eq 'printer_technology'){
                my $value = $event->GetInt();# 0 ~ "ptFFF"; 1 ~ "ptSLA"
                $self->{plater}->show_preset_comboboxes($value);
            }
        }
        # don't save while loading for the first time
        $self->config->save($Slic3r::GUI::autosave) if $Slic3r::GUI::autosave && $self->{loaded};        
    });
    # The following event is emited by the C++ Tab implementation on preset selection,
    # or when the preset's "modified" status changes.
    EVT_COMMAND($self, -1, $PRESETS_CHANGED_EVENT, sub {
        my ($self, $event) = @_;
        my $tab_name = $event->GetString;

        my $tab = Slic3r::GUI::get_preset_tab($tab_name);
        if ($self->{plater}) {
            # Update preset combo boxes (Print settings, Filament, Material, Printer) from their respective tabs.
            my $presets = $tab->get_presets;
            if (defined $presets){
                my $reload_dependent_tabs = $tab->get_dependent_tabs;
                $self->{plater}->update_presets($tab_name, $reload_dependent_tabs, $presets);
                $self->{plater}->{"selected_item_$tab_name"} = $tab->get_selected_preset_item;
                if ($tab_name eq 'printer') {
                    # Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
                    for my $tab_name_other (qw(print filament sla_material)) {
                        # If the printer tells us that the print or filament preset has been switched or invalidated,
                        # refresh the print or filament tab page. Otherwise just refresh the combo box.
                        my $update_action = ($reload_dependent_tabs && (first { $_ eq $tab_name_other } (@{$reload_dependent_tabs}))) 
                            ? 'load_current_preset' : 'update_tab_ui';
                        $self->{options_tabs}{$tab_name_other}->$update_action;
                    }
                    # Update the controller printers.
                    $self->{controller}->update_presets($presets) if $self->{controller};
                }
                $self->{plater}->on_config_change($tab->get_config);
            }
        }
    });

    # The following event is emited by the C++ Tab implementation on object selection change.
    EVT_COMMAND($self, -1, $OBJECT_SELECTION_CHANGED_EVENT, sub {
        my ($self, $event) = @_;
        my $obj_idx = $event->GetId;
        my $child = $event->GetInt == 1 ? 1 : undef;

        $self->{plater}->select_object($obj_idx < 0 ? undef: $obj_idx, $child);
        $self->{plater}->item_changed_selection($obj_idx);
    });

    # The following event is emited by the C++ GUI implementation on object settings change.
    EVT_COMMAND($self, -1, $OBJECT_SETTINGS_CHANGED_EVENT, sub {
        my ($self, $event) = @_;

        my $line = $event->GetString;
        my ($obj_idx, $parts_changed, $part_settings_changed) = split('',$line);

        $self->{plater}->changed_object_settings($obj_idx, $parts_changed, $part_settings_changed);
    });

    # The following event is emited by the C++ GUI implementation on object remove.
    EVT_COMMAND($self, -1, $OBJECT_REMOVE_EVENT, sub {
        my ($self, $event) = @_;
        $self->{plater}->remove();
    });

    # The following event is emited by the C++ GUI implementation on extruder change for object.
    EVT_COMMAND($self, -1, $UPDATE_SCENE_EVENT, sub {
        my ($self, $event) = @_;
        $self->{plater}->update();
    });
        

    Slic3r::GUI::create_preset_tabs($self->{no_controller}, $VALUE_CHANGE_EVENT, $PRESETS_CHANGED_EVENT);
    $self->{options_tabs} = {};
    for my $tab_name (qw(print filament sla_material printer)) {
        $self->{options_tabs}{$tab_name} = Slic3r::GUI::get_preset_tab("$tab_name");
    }

    # Update progress bar with an event sent by the slicing core.
    EVT_COMMAND($self, -1, $PROGRESS_BAR_EVENT, sub {
        my ($self, $event) = @_;
        if (defined $self->{progress_dialog}) {
            # If a progress dialog is open, update it.
            $self->{progress_dialog}->Update($event->GetInt, $event->GetString . "…");
        } else {
            # Otherwise update the main window status bar.
            $self->{statusbar}->SetProgress($event->GetInt);
            $self->{statusbar}->SetStatusText($event->GetString . "…");
        }
    });

    EVT_COMMAND($self, -1, $ERROR_EVENT, sub {
        my ($self, $event) = @_;
        Slic3r::GUI::show_error($self, $event->GetString);
    });
    
    if ($self->{plater}) {
        $self->{plater}->on_select_preset(sub {
            my ($group, $name) = @_;
            $self->{options_tabs}{$group}->select_preset($name);
        });
        # load initial config
        my $full_config = wxTheApp->{preset_bundle}->full_config;
        $self->{plater}->on_config_change($full_config);

        # Show a correct number of filament fields.
        if (defined $full_config->nozzle_diameter){ # nozzle_diameter is undefined when SLA printer is selected
            $self->{plater}->on_extruders_change(int(@{$full_config->nozzle_diameter}));
        }

        # Show correct preset comboboxes according to the printer_technology
        $self->{plater}->show_preset_comboboxes(($full_config->printer_technology eq "FFF") ? 0 : 1);
    }
}

sub _init_menubar {
    my ($self) = @_;
    
    # File menu
    my $fileMenu = Wx::Menu->new;
    {
        wxTheApp->append_menu_item($fileMenu, L("Open STL/OBJ/AMF/3MF…\tCtrl+O"), L('Open a model'), sub {
            $self->{plater}->add if $self->{plater};
        }, undef, undef); #'brick_add.png');
        $self->_append_menu_item($fileMenu, L("&Load Config…\tCtrl+L"), L('Load exported configuration file'), sub {
            $self->load_config_file;
        }, undef, 'plugin_add.png');
        $self->_append_menu_item($fileMenu, L("&Export Config…\tCtrl+E"), L('Export current configuration to file'), sub {
            $self->export_config;
        }, undef, 'plugin_go.png');
        $self->_append_menu_item($fileMenu, L("&Load Config Bundle…"), L('Load presets from a bundle'), sub {
            $self->load_configbundle;
        }, undef, 'lorry_add.png');
        $self->_append_menu_item($fileMenu, L("&Export Config Bundle…"), L('Export all presets to file'), sub {
            $self->export_configbundle;
        }, undef, 'lorry_go.png');
        $fileMenu->AppendSeparator();
        $self->_append_menu_item($fileMenu, L("Slice to PNG…"), L('Slice file to a set of PNG files'), sub {
            $self->slice_to_png;
        }, undef, 'shape_handles.png');
        $self->{menu_item_reslice_now} = $self->_append_menu_item(
            $fileMenu, L("(&Re)Slice Now\tCtrl+S"), L('Start new slicing process'), 
            sub { $self->reslice_now; }, undef, 'shape_handles.png');
        $fileMenu->AppendSeparator();
        $self->_append_menu_item($fileMenu, L("Repair STL file…"), L('Automatically repair an STL file'), sub {
            $self->repair_stl;
        }, undef, 'wrench.png');
        $fileMenu->AppendSeparator();
        $self->_append_menu_item($fileMenu, L("&Quit"), L('Quit Slic3r'), sub {
            $self->Close(0);
        }, wxID_EXIT);
    }
    
    # Plater menu
    unless ($self->{no_plater}) {
        my $plater = $self->{plater};
        
        $self->{plater_menu} = Wx::Menu->new;
        $self->_append_menu_item($self->{plater_menu}, L("Export G-code..."), L('Export current plate as G-code'), sub {
            $plater->export_gcode;
        }, undef, 'cog_go.png');
        $self->_append_menu_item($self->{plater_menu}, L("Export plate as STL..."), L('Export current plate as STL'), sub {
            $plater->export_stl;
        }, undef, 'brick_go.png');
        $self->_append_menu_item($self->{plater_menu}, L("Export plate as AMF..."), L('Export current plate as AMF'), sub {
            $plater->export_amf;
        }, undef, 'brick_go.png');
        $self->_append_menu_item($self->{plater_menu}, L("Export plate as 3MF..."), L('Export current plate as 3MF'), sub {
            $plater->export_3mf;
        }, undef, 'brick_go.png');
        
        $self->{object_menu} = $self->{plater}->object_menu;
        $self->on_plater_selection_changed(0);
    }
    
    # Window menu
    my $windowMenu = Wx::Menu->new;
    {
        my $tab_offset = 0;
        if (!$self->{no_plater}) {
            $self->_append_menu_item($windowMenu, L("Select &Plater Tab\tCtrl+1"), L('Show the plater'), sub {
                $self->select_tab(0);
            }, undef, 'application_view_tile.png');
            $tab_offset += 1;
        }
        if (!$self->{no_controller}) {
            $self->_append_menu_item($windowMenu, L("Select &Controller Tab\tCtrl+T"), L('Show the printer controller'), sub {
                $self->select_tab(1);
            }, undef, 'printer_empty.png');
            $tab_offset += 1;
        }
        if ($tab_offset > 0) {
            $windowMenu->AppendSeparator();
        }
        $self->_append_menu_item($windowMenu, L("Select P&rint Settings Tab\tCtrl+2"), L('Show the print settings'), sub {
            $self->select_tab($tab_offset+0);
        }, undef, 'cog.png');
        $self->_append_menu_item($windowMenu, L("Select &Filament Settings Tab\tCtrl+3"), L('Show the filament settings'), sub {
            $self->select_tab($tab_offset+1);
        }, undef, 'spool.png');
        $self->_append_menu_item($windowMenu, L("Select Print&er Settings Tab\tCtrl+4"), L('Show the printer settings'), sub {
            $self->select_tab($tab_offset+2);
        }, undef, 'printer_empty.png');
    }

    # View menu
    if (!$self->{no_plater}) {
        $self->{viewMenu} = Wx::Menu->new;
        # \xA0 is a non-breaing space. It is entered here to spoil the automatic accelerators,
        # as the simple numeric accelerators spoil all numeric data entry.
        # The camera control accelerators are captured by 3DScene Perl module instead.
        my $accel = ($^O eq 'MSWin32') ? sub { $_[0] . "\t\xA0" . $_[1] } : sub { $_[0] };
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Iso'),    '0'), L('Iso View')    , sub { $self->select_view('iso'    ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Top'),    '1'), L('Top View')    , sub { $self->select_view('top'    ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Bottom'), '2'), L('Bottom View') , sub { $self->select_view('bottom' ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Front'),  '3'), L('Front View')  , sub { $self->select_view('front'  ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Rear'),   '4'), L('Rear View')   , sub { $self->select_view('rear'   ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Left'),   '5'), L('Left View')   , sub { $self->select_view('left'   ); });
        $self->_append_menu_item($self->{viewMenu}, $accel->(L('Right'),  '6'), L('Right View')  , sub { $self->select_view('right'  ); });
    }
    
    # Help menu
    my $helpMenu = Wx::Menu->new;
    {
        $self->_append_menu_item($helpMenu, L("Prusa 3D Drivers"), L('Open the Prusa3D drivers download page in your browser'), sub {
            Wx::LaunchDefaultBrowser('http://www.prusa3d.com/drivers/');
        });
        $self->_append_menu_item($helpMenu, L("Prusa Edition Releases"), L('Open the Prusa Edition releases page in your browser'), sub {
            Wx::LaunchDefaultBrowser('http://github.com/prusa3d/slic3r/releases');
        });
#        my $versioncheck = $self->_append_menu_item($helpMenu, "Check for &Updates...", 'Check for new Slic3r versions', sub {
#            wxTheApp->check_version(1);
#        });
#        $versioncheck->Enable(wxTheApp->have_version_check);
        $self->_append_menu_item($helpMenu, L("Slic3r &Website"), L('Open the Slic3r website in your browser'), sub {
            Wx::LaunchDefaultBrowser('http://slic3r.org/');
        });
        $self->_append_menu_item($helpMenu, L("Slic3r &Manual"), L('Open the Slic3r manual in your browser'), sub {
            Wx::LaunchDefaultBrowser('http://manual.slic3r.org/');
        });
        $helpMenu->AppendSeparator();
        $self->_append_menu_item($helpMenu, L("System Info"), L('Show system information'), sub {
            wxTheApp->system_info;
        });
        $self->_append_menu_item($helpMenu, L("Show &Configuration Folder"), L('Show user configuration folder (datadir)'), sub {
            Slic3r::GUI::desktop_open_datadir_folder();
        });
        $self->_append_menu_item($helpMenu, L("Report an Issue"), L('Report an issue on the Slic3r Prusa Edition'), sub {
            Wx::LaunchDefaultBrowser('http://github.com/prusa3d/slic3r/issues/new');
        });
        $self->_append_menu_item($helpMenu, L("&About Slic3r"), L('Show about dialog'), sub {
            Slic3r::GUI::about;
        });
    }

    # menubar
    # assign menubar to frame after appending items, otherwise special items
    # will not be handled correctly
    {
        my $menubar = Wx::MenuBar->new;
        $menubar->Append($fileMenu, L("&File"));
        $menubar->Append($self->{plater_menu}, L("&Plater")) if $self->{plater_menu};
        $menubar->Append($self->{object_menu}, L("&Object")) if $self->{object_menu};
        $menubar->Append($windowMenu, L("&Window"));
        $menubar->Append($self->{viewMenu}, L("&View")) if $self->{viewMenu};
        # Add additional menus from C++
        Slic3r::GUI::add_menus($menubar, $self->{preferences_event}, $self->{lang_ch_event});
        $menubar->Append($helpMenu, L("&Help"));
        $self->SetMenuBar($menubar);
    }
}

sub is_loaded {
    my ($self) = @_;
    return $self->{loaded};
}

# Selection of a 3D object changed on the platter.
sub on_plater_selection_changed {
    my ($self, $have_selection) = @_;
    return if !defined $self->{object_menu};
    $self->{object_menu}->Enable($_->GetId, $have_selection)
        for $self->{object_menu}->GetMenuItems;
}

sub slice_to_png {
    my $self = shift;
    $self->{plater}->stop_background_process;
    $self->{plater}->async_apply_config;
    $appController->print_ctl()->slice_to_png();
}

sub reslice_now {
    my ($self) = @_;
    $self->{plater}->reslice if $self->{plater};
}

sub repair_stl {
    my $self = shift;
    
    my $input_file;
    {
        my $dialog = Wx::FileDialog->new($self, L('Select the STL file to repair:'),
            wxTheApp->{app_config}->get_last_dir, "",
            &Slic3r::GUI::FILE_WILDCARDS->{stl}, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if ($dialog->ShowModal != wxID_OK) {
            $dialog->Destroy;
            return;
        }
        $input_file = $dialog->GetPaths;
        $dialog->Destroy;
    }
    
    my $output_file = $input_file;
    {
        $output_file =~ s/\.[sS][tT][lL]$/_fixed.obj/;
        my $dlg = Wx::FileDialog->new($self, L("Save OBJ file (less prone to coordinate errors than STL) as:"), dirname($output_file),
            basename($output_file), &Slic3r::GUI::FILE_WILDCARDS->{obj}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return undef;
        }
        $output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    my $tmesh = Slic3r::TriangleMesh->new;
    $tmesh->ReadSTLFile($input_file);
    $tmesh->repair;
    $tmesh->WriteOBJFile($output_file);
    Slic3r::GUI::show_info($self, L("Your file was repaired."), L("Repair"));
}

sub export_config {
    my $self = shift;
    # Generate a cummulative configuration for the selected print, filaments and printer.    
    my $config = wxTheApp->{preset_bundle}->full_config();
    # Validate the cummulative configuration.
    eval { $config->validate; };
    Slic3r::GUI::catch_error($self) and return;
    # Ask user for the file name for the config file.
    my $dlg = Wx::FileDialog->new($self, L('Save configuration as:'),
        $last_config ? dirname($last_config) : wxTheApp->{app_config}->get_last_dir,
        $last_config ? basename($last_config) : "config.ini",
        &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    my $file = ($dlg->ShowModal == wxID_OK) ? $dlg->GetPath : undef;
    $dlg->Destroy;
    if (defined $file) {
        wxTheApp->{app_config}->update_config_dir(dirname($file));
        $last_config = $file;
        $config->save($file);
    }
}

# Load a config file containing a Print, Filament & Printer preset.
sub load_config_file {
    my ($self, $file) = @_;
    if (!$file) {
        return unless Slic3r::GUI::check_unsaved_changes;
        my $dlg = Wx::FileDialog->new($self, L('Select configuration to load:'), 
            $last_config ? dirname($last_config) : wxTheApp->{app_config}->get_last_dir,
            "config.ini",
            'INI files (*.ini, *.gcode)|*.ini;*.INI;*.gcode;*.g', wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        return unless $dlg->ShowModal == wxID_OK;
        $file = $dlg->GetPaths;
        $dlg->Destroy;
    }
    eval { wxTheApp->{preset_bundle}->load_config_file($file); };
    # Dont proceed further if the config file cannot be loaded.
    return if Slic3r::GUI::catch_error($self);
    $_->load_current_preset for (values %{$self->{options_tabs}});
    wxTheApp->{app_config}->update_config_dir(dirname($file));
    $last_config = $file;
}

sub export_configbundle {
    my ($self) = @_;
    return unless Slic3r::GUI::check_unsaved_changes;
    # validate current configuration in case it's dirty
    eval { wxTheApp->{preset_bundle}->full_config->validate; };
    Slic3r::GUI::catch_error($self) and return;
    # Ask user for a file name.
    my $dlg = Wx::FileDialog->new($self, L('Save presets bundle as:'),
        $last_config ? dirname($last_config) : wxTheApp->{app_config}->get_last_dir,
        "Slic3r_config_bundle.ini", 
        &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    my $file = ($dlg->ShowModal == wxID_OK) ? $dlg->GetPath : undef;
    $dlg->Destroy;
    if (defined $file) {
        # Export the config bundle.
        wxTheApp->{app_config}->update_config_dir(dirname($file));
        eval { wxTheApp->{preset_bundle}->export_configbundle($file); };
        Slic3r::GUI::catch_error($self) and return;
    }
}

# Loading a config bundle with an external file name used to be used
# to auto-install a config bundle on a fresh user account,
# but that behavior was not documented and likely buggy.
sub load_configbundle {
    my ($self, $file, $reset_user_profile) = @_;
    return unless Slic3r::GUI::check_unsaved_changes;
    if (!$file) {
        my $dlg = Wx::FileDialog->new($self, L('Select configuration to load:'), 
            $last_config ? dirname($last_config) : wxTheApp->{app_config}->get_last_dir,
            "config.ini", 
            &Slic3r::GUI::FILE_WILDCARDS->{ini}, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        return unless $dlg->ShowModal == wxID_OK;
        $file = $dlg->GetPaths;
        $dlg->Destroy;
    }
    
    wxTheApp->{app_config}->update_config_dir(dirname($file));

    my $presets_imported = 0;
    eval { $presets_imported = wxTheApp->{preset_bundle}->load_configbundle($file); };
    Slic3r::GUI::catch_error($self) and return;

    # Load the currently selected preset into the GUI, update the preset selection box.
    foreach my $tab (values %{$self->{options_tabs}}) {
        $tab->load_current_preset;
    }
    
    my $message = sprintf L("%d presets successfully imported."), $presets_imported;
    Slic3r::GUI::show_info($self, $message);
}

# Load a provied DynamicConfig into the Print / Filament / Printer tabs, thus modifying the active preset.
# Also update the platter with the new presets.
sub load_config {
    my ($self, $config) = @_;
    $_->load_config($config) foreach values %{$self->{options_tabs}};
    $self->{plater}->on_config_change($config) if $self->{plater};
}

sub select_tab {
    my ($self, $tab) = @_;
    $self->{tabpanel}->SetSelection($tab);
}

# Set a camera direction, zoom to all objects.
sub select_view {
    my ($self, $direction) = @_;
    if (! $self->{no_plater}) {
        $self->{plater}->select_view($direction);
    }
}

sub _append_menu_item {
    my ($self, $menu, $string, $description, $cb, $id, $icon) = @_;
    $id //= &Wx::NewId();
    my $item = $menu->Append($id, $string, $description);
    $self->_set_menu_item_icon($item, $icon);
    EVT_MENU($self, $id, $cb);
    return $item;
}

sub _set_menu_item_icon {
    my ($self, $menuItem, $icon) = @_;
    # SetBitmap was not available on OS X before Wx 0.9927
    if ($icon && $menuItem->can('SetBitmap')) {
        $menuItem->SetBitmap(Wx::Bitmap->new(Slic3r::var($icon), wxBITMAP_TYPE_PNG));
    }
}

# Called after the Preferences dialog is closed and the program settings are saved.
# Update the UI based on the current preferences.
sub update_ui_from_settings {
    my ($self) = @_;
    $self->{menu_item_reslice_now}->Enable(! wxTheApp->{app_config}->get("background_processing"));
    $self->{plater}->update_ui_from_settings if ($self->{plater});
    for my $tab_name (qw(print filament printer)) {
        $self->{options_tabs}{$tab_name}->update_ui_from_settings;
    }
}

1;
