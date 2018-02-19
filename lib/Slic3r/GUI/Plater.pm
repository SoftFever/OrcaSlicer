# The "Plater" tab. It contains the "3D", "2D", "Preview" and "Layers" subtabs.

package Slic3r::GUI::Plater;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use List::Util qw(sum first max);
use Slic3r::Geometry qw(X Y Z scale unscale deg2rad rad2deg);
use LWP::UserAgent;
use threads::shared qw(shared_clone);
use Wx qw(:button :colour :cursor :dialog :filedialog :keycode :icon :font :id :listctrl :misc 
    :panel :sizer :toolbar :window wxTheApp :notebook :combobox wxNullBitmap);
use Wx::Event qw(EVT_BUTTON EVT_TOGGLEBUTTON EVT_COMMAND EVT_KEY_DOWN EVT_LIST_ITEM_ACTIVATED 
    EVT_LIST_ITEM_DESELECTED EVT_LIST_ITEM_SELECTED EVT_LEFT_DOWN EVT_MOUSE_EVENTS EVT_PAINT EVT_TOOL 
    EVT_CHOICE EVT_COMBOBOX EVT_TIMER EVT_NOTEBOOK_PAGE_CHANGED);
use base 'Wx::Panel';

use constant TB_ADD             => &Wx::NewId;
use constant TB_REMOVE          => &Wx::NewId;
use constant TB_RESET           => &Wx::NewId;
use constant TB_ARRANGE         => &Wx::NewId;
use constant TB_EXPORT_GCODE    => &Wx::NewId;
use constant TB_EXPORT_STL      => &Wx::NewId;
use constant TB_MORE    => &Wx::NewId;
use constant TB_FEWER   => &Wx::NewId;
use constant TB_45CW    => &Wx::NewId;
use constant TB_45CCW   => &Wx::NewId;
use constant TB_SCALE   => &Wx::NewId;
use constant TB_SPLIT   => &Wx::NewId;
use constant TB_CUT     => &Wx::NewId;
use constant TB_SETTINGS => &Wx::NewId;
use constant TB_LAYER_EDITING => &Wx::NewId;

# package variables to avoid passing lexicals to threads
our $PROGRESS_BAR_EVENT      : shared = Wx::NewEventType;
our $ERROR_EVENT             : shared = Wx::NewEventType;
# Emitted from the worker thread when the G-code export is finished.
our $EXPORT_COMPLETED_EVENT  : shared = Wx::NewEventType;
our $PROCESS_COMPLETED_EVENT : shared = Wx::NewEventType;

use constant FILAMENT_CHOOSERS_SPACING => 0;
use constant PROCESS_DELAY => 0.5 * 1000; # milliseconds

my $PreventListEvents = 0;

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{config} = Slic3r::Config::new_from_defaults_keys([qw(
        bed_shape complete_objects extruder_clearance_radius skirts skirt_distance brim_width variable_layer_height
        serial_port serial_speed octoprint_host octoprint_apikey
        nozzle_diameter single_extruder_multi_material 
        wipe_tower wipe_tower_x wipe_tower_y wipe_tower_width wipe_tower_per_color_wipe extruder_colour filament_colour
    )]);
    # C++ Slic3r::Model with Perl extensions in Slic3r/Model.pm
    $self->{model} = Slic3r::Model->new;
    # C++ Slic3r::Print with Perl extensions in Slic3r/Print.pm
    $self->{print} = Slic3r::Print->new;
    # List of Perl objects Slic3r::GUI::Plater::Object, representing a 2D preview of the platter.
    $self->{objects} = [];
    $self->{gcode_preview_data} = Slic3r::GCode::PreviewData->new;
    
    $self->{print}->set_status_cb(sub {
        my ($percent, $message) = @_;
        my $event = Wx::CommandEvent->new($PROGRESS_BAR_EVENT);
        $event->SetString($message);
        $event->SetInt($percent);
        Wx::PostEvent($self, $event);
    });
    
    # Initialize preview notebook
    $self->{preview_notebook} = Wx::Notebook->new($self, -1, wxDefaultPosition, [335,335], wxNB_BOTTOM);
    
    # Initialize handlers for canvases
    my $on_select_object = sub {
        my ($obj_idx) = @_;
        # Ignore the special objects (the wipe tower proxy and such).
        $self->select_object((defined($obj_idx) && $obj_idx < 1000) ? $obj_idx : undef);
    };
    my $on_double_click = sub {
        $self->object_settings_dialog if $self->selected_object;
    };
    my $on_right_click = sub {
        my ($canvas, $click_pos) = @_;
        
        my ($obj_idx, $object) = $self->selected_object;
        return if !defined $obj_idx;
        
        my $menu = $self->object_menu;
        $canvas->PopupMenu($menu, $click_pos);
        $menu->Destroy;
    };
    my $on_instances_moved = sub {
        $self->update;
    };
    
    # Initialize 3D plater
    if ($Slic3r::GUI::have_OpenGL) {
        $self->{canvas3D} = Slic3r::GUI::Plater::3D->new($self->{preview_notebook}, $self->{objects}, $self->{model}, $self->{print}, $self->{config});
        $self->{preview_notebook}->AddPage($self->{canvas3D}, '3D');
        $self->{canvas3D}->set_on_select_object($on_select_object);
        $self->{canvas3D}->set_on_double_click($on_double_click);
        $self->{canvas3D}->set_on_right_click(sub { $on_right_click->($self->{canvas3D}, @_); });
        $self->{canvas3D}->set_on_arrange(sub { $self->arrange });
        $self->{canvas3D}->set_on_rotate_object_left(sub { $self->rotate(-45, Z, 'relative') });
        $self->{canvas3D}->set_on_rotate_object_right(sub { $self->rotate( 45, Z, 'relative') });
        $self->{canvas3D}->set_on_scale_object_uniformly(sub { $self->changescale(undef) });
        $self->{canvas3D}->set_on_increase_objects(sub { $self->increase() });
        $self->{canvas3D}->set_on_decrease_objects(sub { $self->decrease() });
        $self->{canvas3D}->set_on_remove_object(sub { $self->remove() });
        $self->{canvas3D}->set_on_instances_moved($on_instances_moved);
        $self->{canvas3D}->set_on_wipe_tower_moved(sub {
            my ($new_pos_3f) = @_;
            my $cfg = Slic3r::Config->new;
            $cfg->set('wipe_tower_x', $new_pos_3f->x);
            $cfg->set('wipe_tower_y', $new_pos_3f->y);
            $self->GetFrame->{options_tabs}{print}->load_config($cfg);
        });
        $self->{canvas3D}->set_on_model_update(sub {
            if (wxTheApp->{app_config}->get("background_processing")) {
                $self->schedule_background_process;
            } else {
                # Hide the print info box, it is no more valid.
                $self->{"print_info_box_show"}->(0);
            }
        });
        $self->{canvas3D}->on_viewport_changed(sub {
            $self->{preview3D}->canvas->set_viewport_from_scene($self->{canvas3D});
        });
    }
    
    # Initialize 2D preview canvas
    $self->{canvas} = Slic3r::GUI::Plater::2D->new($self->{preview_notebook}, wxDefaultSize, $self->{objects}, $self->{model}, $self->{config});
    $self->{preview_notebook}->AddPage($self->{canvas}, '2D');
    $self->{canvas}->on_select_object($on_select_object);
    $self->{canvas}->on_double_click($on_double_click);
    $self->{canvas}->on_right_click(sub { $on_right_click->($self->{canvas}, @_); });
    $self->{canvas}->on_instances_moved($on_instances_moved);
    
    # Initialize 3D toolpaths preview
    if ($Slic3r::GUI::have_OpenGL) {
        $self->{preview3D} = Slic3r::GUI::Plater::3DPreview->new($self->{preview_notebook}, $self->{print}, $self->{gcode_preview_data}, $self->{config});
        $self->{preview3D}->canvas->on_viewport_changed(sub {
            $self->{canvas3D}->set_viewport_from_scene($self->{preview3D}->canvas);
        });
        $self->{preview_notebook}->AddPage($self->{preview3D}, 'Preview');
        $self->{preview3D_page_idx} = $self->{preview_notebook}->GetPageCount-1;
    }
    
    # Initialize toolpaths preview
    if ($Slic3r::GUI::have_OpenGL) {
        $self->{toolpaths2D} = Slic3r::GUI::Plater::2DToolpaths->new($self->{preview_notebook}, $self->{print});
        $self->{preview_notebook}->AddPage($self->{toolpaths2D}, 'Layers');
    }
    
    EVT_NOTEBOOK_PAGE_CHANGED($self, $self->{preview_notebook}, sub {
        my $preview = $self->{preview_notebook}->GetCurrentPage;
        if ($preview == $self->{preview3D})
        {
            $self->{preview3D}->canvas->set_legend_enabled(1);
            $self->{preview3D}->load_print(1);
        } else {
            $self->{preview3D}->canvas->set_legend_enabled(0);
        }

        $preview->OnActivate if $preview->can('OnActivate');        
    });
    
    # toolbar for object manipulation
    if (!&Wx::wxMSW) {
        Wx::ToolTip::Enable(1);
        $self->{htoolbar} = Wx::ToolBar->new($self, -1, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_TEXT | wxBORDER_SIMPLE | wxTAB_TRAVERSAL);
        $self->{htoolbar}->AddTool(TB_ADD, "Add…", Wx::Bitmap->new(Slic3r::var("brick_add.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_REMOVE, "Delete", Wx::Bitmap->new(Slic3r::var("brick_delete.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_RESET, "Delete All", Wx::Bitmap->new(Slic3r::var("cross.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_ARRANGE, "Arrange", Wx::Bitmap->new(Slic3r::var("bricks.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_MORE, "More", Wx::Bitmap->new(Slic3r::var("add.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_FEWER, "Fewer", Wx::Bitmap->new(Slic3r::var("delete.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_45CCW, "45° ccw", Wx::Bitmap->new(Slic3r::var("arrow_rotate_anticlockwise.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_45CW, "45° cw", Wx::Bitmap->new(Slic3r::var("arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SCALE, "Scale…", Wx::Bitmap->new(Slic3r::var("arrow_out.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SPLIT, "Split", Wx::Bitmap->new(Slic3r::var("shape_ungroup.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_CUT, "Cut…", Wx::Bitmap->new(Slic3r::var("package.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_SETTINGS, "Settings…", Wx::Bitmap->new(Slic3r::var("cog.png"), wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_LAYER_EDITING, 'Layer Editing', Wx::Bitmap->new(Slic3r::var("variable_layer_height.png"), wxBITMAP_TYPE_PNG), wxNullBitmap, 1, 0, 'Layer Editing');
    } else {
        my %tbar_buttons = (
            add             => "Add…",
            remove          => "Delete",
            reset           => "Delete All",
            arrange         => "Arrange",
            increase        => "",
            decrease        => "",
            rotate45ccw     => "",
            rotate45cw      => "",
            changescale     => "Scale…",
            split           => "Split",
            cut             => "Cut…",
            settings        => "Settings…",
            layer_editing   => "Layer editing",
        );
        $self->{btoolbar} = Wx::BoxSizer->new(wxHORIZONTAL);
        for (qw(add remove reset arrange increase decrease rotate45ccw rotate45cw changescale split cut settings)) {
            $self->{"btn_$_"} = Wx::Button->new($self, -1, $tbar_buttons{$_}, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            $self->{btoolbar}->Add($self->{"btn_$_"});
        }
        $self->{"btn_layer_editing"} = Wx::ToggleButton->new($self, -1, $tbar_buttons{'layer_editing'}, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        $self->{btoolbar}->Add($self->{"btn_layer_editing"});
    }

    $self->{list} = Wx::ListView->new($self, -1, wxDefaultPosition, wxDefaultSize,
        wxLC_SINGLE_SEL | wxLC_REPORT | wxBORDER_SUNKEN | wxTAB_TRAVERSAL | wxWANTS_CHARS );
    $self->{list}->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 145);
    $self->{list}->InsertColumn(1, "Copies", wxLIST_FORMAT_CENTER, 45);
    $self->{list}->InsertColumn(2, "Scale", wxLIST_FORMAT_CENTER, wxLIST_AUTOSIZE_USEHEADER);
    EVT_LIST_ITEM_SELECTED($self, $self->{list}, \&list_item_selected);
    EVT_LIST_ITEM_DESELECTED($self, $self->{list}, \&list_item_deselected);
    EVT_LIST_ITEM_ACTIVATED($self, $self->{list}, \&list_item_activated);
    EVT_KEY_DOWN($self->{list}, sub {
        my ($list, $event) = @_;
        if ($event->GetKeyCode == WXK_TAB) {
            $list->Navigate($event->ShiftDown ? &Wx::wxNavigateBackward : &Wx::wxNavigateForward);
        } else {
            $event->Skip;
        }
    });
    
    # right pane buttons
    $self->{btn_export_gcode} = Wx::Button->new($self, -1, "Export G-code…", wxDefaultPosition, [-1, 30], wxBU_LEFT);
    $self->{btn_reslice} = Wx::Button->new($self, -1, "Slice now", wxDefaultPosition, [-1, 30], wxBU_LEFT);
    $self->{btn_print} = Wx::Button->new($self, -1, "Print…", wxDefaultPosition, [-1, 30], wxBU_LEFT);
    $self->{btn_send_gcode} = Wx::Button->new($self, -1, "Send to printer", wxDefaultPosition, [-1, 30], wxBU_LEFT);
    $self->{btn_export_stl} = Wx::Button->new($self, -1, "Export STL…", wxDefaultPosition, [-1, 30], wxBU_LEFT);
    #$self->{btn_export_gcode}->SetFont($Slic3r::GUI::small_font);
    #$self->{btn_export_stl}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_print}->Hide;
    $self->{btn_send_gcode}->Hide;
    
    my %icons = qw(
        add             brick_add.png
        remove          brick_delete.png
        reset           cross.png
        arrange         bricks.png
        export_gcode    cog_go.png
        print           arrow_up.png
        send_gcode      arrow_up.png
        reslice         reslice.png
        export_stl      brick_go.png
        
        increase        add.png
        decrease        delete.png
        rotate45cw      arrow_rotate_clockwise.png
        rotate45ccw     arrow_rotate_anticlockwise.png
        changescale     arrow_out.png
        split           shape_ungroup.png
        cut             package.png
        settings        cog.png
    );
    for (grep $self->{"btn_$_"}, keys %icons) {
        $self->{"btn_$_"}->SetBitmap(Wx::Bitmap->new(Slic3r::var($icons{$_}), wxBITMAP_TYPE_PNG));
    }
    $self->selection_changed(0);
    $self->object_list_changed;
    EVT_BUTTON($self, $self->{btn_export_gcode}, sub {
        $self->export_gcode;
    });
    EVT_BUTTON($self, $self->{btn_print}, sub {
        $self->{print_file} = $self->export_gcode(Wx::StandardPaths::Get->GetTempDir());
    });
    EVT_BUTTON($self, $self->{btn_send_gcode}, sub {
        $self->{send_gcode_file} = $self->export_gcode(Wx::StandardPaths::Get->GetTempDir());
    });
    EVT_BUTTON($self, $self->{btn_reslice}, \&reslice);
    EVT_BUTTON($self, $self->{btn_export_stl}, \&export_stl);
    
    if ($self->{htoolbar}) {
        EVT_TOOL($self, TB_ADD, sub { $self->add; });
        EVT_TOOL($self, TB_REMOVE, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_TOOL($self, TB_RESET, sub { $self->reset; });
        EVT_TOOL($self, TB_ARRANGE, sub { $self->arrange; });
        EVT_TOOL($self, TB_MORE, sub { $self->increase; });
        EVT_TOOL($self, TB_FEWER, sub { $self->decrease; });
        EVT_TOOL($self, TB_45CW, sub { $_[0]->rotate(-45, Z, 'relative') });
        EVT_TOOL($self, TB_45CCW, sub { $_[0]->rotate(45, Z, 'relative') });
        EVT_TOOL($self, TB_SCALE, sub { $self->changescale(undef); });
        EVT_TOOL($self, TB_SPLIT, sub { $self->split_object; });
        EVT_TOOL($self, TB_CUT, sub { $_[0]->object_cut_dialog });
        EVT_TOOL($self, TB_SETTINGS, sub { $_[0]->object_settings_dialog });
        EVT_TOOL($self, TB_LAYER_EDITING, sub {
            my $state = $self->{canvas3D}->layer_editing_enabled;
            $self->{htoolbar}->ToggleTool(TB_LAYER_EDITING, ! $state);
            $self->on_layer_editing_toggled(! $state);
        });
    } else {
        EVT_BUTTON($self, $self->{btn_add}, sub { $self->add; });
        EVT_BUTTON($self, $self->{btn_remove}, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_BUTTON($self, $self->{btn_reset}, sub { $self->reset; });
        EVT_BUTTON($self, $self->{btn_arrange}, sub { $self->arrange; });
        EVT_BUTTON($self, $self->{btn_increase}, sub { $self->increase; });
        EVT_BUTTON($self, $self->{btn_decrease}, sub { $self->decrease; });
        EVT_BUTTON($self, $self->{btn_rotate45cw}, sub { $_[0]->rotate(-45, Z, 'relative') });
        EVT_BUTTON($self, $self->{btn_rotate45ccw}, sub { $_[0]->rotate(45, Z, 'relative') });
        EVT_BUTTON($self, $self->{btn_changescale}, sub { $self->changescale(undef); });
        EVT_BUTTON($self, $self->{btn_split}, sub { $self->split_object; });
        EVT_BUTTON($self, $self->{btn_cut}, sub { $_[0]->object_cut_dialog });
        EVT_BUTTON($self, $self->{btn_settings}, sub { $_[0]->object_settings_dialog });
        EVT_TOGGLEBUTTON($self, $self->{btn_layer_editing}, sub { $self->on_layer_editing_toggled($self->{btn_layer_editing}->GetValue); });
    }
    
    $_->SetDropTarget(Slic3r::GUI::Plater::DropTarget->new($self))
        for grep defined($_),
            $self, $self->{canvas}, $self->{canvas3D}, $self->{preview3D}, $self->{list};
    
    EVT_COMMAND($self, -1, $PROGRESS_BAR_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_progress_event($event->GetInt, $event->GetString);
    });
    
    EVT_COMMAND($self, -1, $ERROR_EVENT, sub {
        my ($self, $event) = @_;
        Slic3r::GUI::show_error($self, $event->GetString);
    });
    
    EVT_COMMAND($self, -1, $EXPORT_COMPLETED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_export_completed($event->GetInt);
    });
    
    EVT_COMMAND($self, -1, $PROCESS_COMPLETED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_process_completed($event->GetInt ? undef : $event->GetString);
    });
    
    {
        my $timer_id = Wx::NewId();
        $self->{apply_config_timer} = Wx::Timer->new($self, $timer_id);
        EVT_TIMER($self, $timer_id, sub {
            my ($self, $event) = @_;
            $self->async_apply_config;
        });
    }
    
    $self->{canvas}->update_bed_size;
    if ($self->{canvas3D}) {
        $self->{canvas3D}->update_bed_size;
        $self->{canvas3D}->zoom_to_bed;
    }
    if ($self->{preview3D}) {
        $self->{preview3D}->set_bed_shape($self->{config}->bed_shape);
    }
    $self->update;
    
    {
        my $presets;
        {
            $presets = $self->{presets_sizer} = Wx::FlexGridSizer->new(3, 2, 1, 2);
            $presets->AddGrowableCol(1, 1);
            $presets->SetFlexibleDirection(wxHORIZONTAL);
            my %group_labels = (
                print       => 'Print settings',
                filament    => 'Filament',
                printer     => 'Printer',
            );
            # UI Combo boxes for a print, multiple filaments, and a printer.
            # Initially a single filament combo box is created, but the number of combo boxes for the filament selection may increase,
            # once a printer preset with multiple extruders is activated.
            # $self->{preset_choosers}{$group}[$idx]
            $self->{preset_choosers} = {};
            for my $group (qw(print filament printer)) {
                my $text = Wx::StaticText->new($self, -1, "$group_labels{$group}:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
                $text->SetFont($Slic3r::GUI::small_font);
                my $choice = Wx::BitmapComboBox->new($self, -1, "", wxDefaultPosition, wxDefaultSize, [], wxCB_READONLY);
                if ($group eq 'filament') {
                    EVT_LEFT_DOWN($choice, sub { $self->filament_color_box_lmouse_down(0, @_); } );
                }
                $self->{preset_choosers}{$group} = [$choice];
                # setup the listener
                EVT_COMBOBOX($choice, $choice, sub {
                    my ($choice) = @_;
                    wxTheApp->CallAfter(sub {
                        $self->_on_select_preset($group, $choice, 0);
                    });
                });
                $presets->Add($text, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
                $presets->Add($choice, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxBOTTOM, 0);
            }
        }
        
        my $object_info_sizer;
        {
            my $box = Wx::StaticBox->new($self, -1, "Info");
            $object_info_sizer = Wx::StaticBoxSizer->new($box, wxVERTICAL);
            $object_info_sizer->SetMinSize([350,-1]);
            my $grid_sizer = Wx::FlexGridSizer->new(3, 4, 5, 5);
            $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
            $grid_sizer->AddGrowableCol(1, 1);
            $grid_sizer->AddGrowableCol(3, 1);
            $object_info_sizer->Add($grid_sizer, 0, wxEXPAND);
            
            my @info = (
                size        => "Size",
                volume      => "Volume",
                facets      => "Facets",
                materials   => "Materials",
                manifold    => "Manifold",
            );
            while (my $field = shift @info) {
                my $label = shift @info;
                my $text = Wx::StaticText->new($self, -1, "$label:", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
                $text->SetFont($Slic3r::GUI::small_font);
                $grid_sizer->Add($text, 0);
                
                $self->{"object_info_$field"} = Wx::StaticText->new($self, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
                $self->{"object_info_$field"}->SetFont($Slic3r::GUI::small_font);
                if ($field eq 'manifold') {
                    $self->{object_info_manifold_warning_icon} = Wx::StaticBitmap->new($self, -1, Wx::Bitmap->new(Slic3r::var("error.png"), wxBITMAP_TYPE_PNG));
                    $self->{object_info_manifold_warning_icon}->Hide;
                    
                    my $h_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
                    $h_sizer->Add($self->{object_info_manifold_warning_icon}, 0);
                    $h_sizer->Add($self->{"object_info_$field"}, 0);
                    $grid_sizer->Add($h_sizer, 0, wxEXPAND);
                } else {
                    $grid_sizer->Add($self->{"object_info_$field"}, 0);
                }
            }
        }

        my $print_info_sizer;
        {
            my $box = Wx::StaticBox->new($self, -1, "Sliced Info");
            $print_info_sizer = Wx::StaticBoxSizer->new($box, wxVERTICAL);
            $print_info_sizer->SetMinSize([350,-1]);
            my $grid_sizer = Wx::FlexGridSizer->new(2, 2, 5, 5);
            $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
            $grid_sizer->AddGrowableCol(1, 1);
            $grid_sizer->AddGrowableCol(3, 1);
            $print_info_sizer->Add($grid_sizer, 0, wxEXPAND);
            my @info = (
                fil_m   => "Used Filament (m)",
                fil_mm3 => "Used Filament (mm\x{00B3})",
                fil_g   => "Used Filament (g)",
                cost    => "Cost",
                time    => "Estimated printing time",
            );
            while (my $field = shift @info) {
                my $label = shift @info;
                my $text = Wx::StaticText->new($self, -1, "$label:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
                $text->SetFont($Slic3r::GUI::small_font);
                $grid_sizer->Add($text, 0);
                
                $self->{"print_info_$field"} = Wx::StaticText->new($self, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
                $self->{"print_info_$field"}->SetFont($Slic3r::GUI::small_font);
                $grid_sizer->Add($self->{"print_info_$field"}, 0);
            }
        }
        
        my $buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $self->{buttons_sizer} = $buttons_sizer;
        $buttons_sizer->AddStretchSpacer(1);
        $buttons_sizer->Add($self->{btn_export_stl}, 0, wxALIGN_RIGHT, 0);
        $buttons_sizer->Add($self->{btn_reslice}, 0, wxALIGN_RIGHT, 0);
        $buttons_sizer->Add($self->{btn_print}, 0, wxALIGN_RIGHT, 0);
        $buttons_sizer->Add($self->{btn_send_gcode}, 0, wxALIGN_RIGHT, 0);
        $buttons_sizer->Add($self->{btn_export_gcode}, 0, wxALIGN_RIGHT, 0);
        
        my $right_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $right_sizer->Add($presets, 0, wxEXPAND | wxTOP, 10) if defined $presets;
        $right_sizer->Add($buttons_sizer, 0, wxEXPAND | wxBOTTOM, 5);
        $right_sizer->Add($self->{list}, 1, wxEXPAND, 5);
        $right_sizer->Add($object_info_sizer, 0, wxEXPAND, 0);
        $right_sizer->Add($print_info_sizer, 0, wxEXPAND, 0);
        # Callback for showing / hiding the print info box.
        $self->{"print_info_box_show"} = sub {
            if ($right_sizer->IsShown(4) != $_[0]) { 
                $right_sizer->Show(4, $_[0]); 
                $self->Layout
            }
        };
        # Show the box initially, let it be shown after the slicing is finished.
        $self->{"print_info_box_show"}->(0);

        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $hsizer->Add($self->{preview_notebook}, 1, wxEXPAND | wxTOP, 1);
        $hsizer->Add($right_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 3);
        
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($self->{htoolbar}, 0, wxEXPAND, 0) if $self->{htoolbar};
        $sizer->Add($self->{btoolbar}, 0, wxEXPAND, 0) if $self->{btoolbar};
        $sizer->Add($hsizer, 1, wxEXPAND, 0);
        
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
    }

    $self->update_ui_from_settings();
    
    return $self;
}

# sets the callback
sub on_select_preset {
    my ($self, $cb) = @_;
    $self->{on_select_preset} = $cb;
}

# Called from the platter combo boxes selecting the active print, filament or printer.
sub _on_select_preset {
	my ($self, $group, $choice, $idx) = @_;
	# If user changed a filament preset and the selected machine is equipped with multiple extruders,
    # there are multiple filament selection combo boxes shown at the platter. In that case
    # don't propagate the filament selection changes to the tab.
    if ($group eq 'filament') {
        wxTheApp->{preset_bundle}->set_filament_preset($idx, $choice->GetStringSelection);
    }
	if ($group eq 'filament' && @{$self->{preset_choosers}{filament}} > 1) {
        # Only update the platter UI for the 2nd and other filaments.
        wxTheApp->{preset_bundle}->update_platter_filament_ui($idx, $choice);
	} else {
    	# call GetSelection() in scalar context as it's context-aware
    	$self->{on_select_preset}->($group, $choice->GetStringSelection)
    	    if $self->{on_select_preset};
    }
    # Synchronize config.ini with the current selections.
    wxTheApp->{preset_bundle}->export_selections(wxTheApp->{app_config});
	# get new config and generate on_config_change() event for updating plater and other things
	$self->on_config_change(wxTheApp->{preset_bundle}->full_config);
}

sub on_layer_editing_toggled {
    my ($self, $new_state) = @_;
    $self->{canvas3D}->layer_editing_enabled($new_state);
    if ($new_state && ! $self->{canvas3D}->layer_editing_enabled) {
        # Initialization of the OpenGL shaders failed. Disable the tool.
        if ($self->{htoolbar}) {
            $self->{htoolbar}->EnableTool(TB_LAYER_EDITING, 0);
            $self->{htoolbar}->ToggleTool(TB_LAYER_EDITING, 0);
        } else {
            $self->{"btn_layer_editing"}->Disable;
            $self->{"btn_layer_editing"}->SetValue(0);
        }
    }
    $self->{canvas3D}->Refresh;
    $self->{canvas3D}->Update;
}

sub GetFrame {
    my ($self) = @_;
    return &Wx::GetTopLevelParent($self);
}

# Called after the Preferences dialog is closed and the program settings are saved.
# Update the UI based on the current preferences.
sub update_ui_from_settings
{
    my ($self) = @_;
    if (defined($self->{btn_reslice}) && $self->{buttons_sizer}->IsShown($self->{btn_reslice}) != (! wxTheApp->{app_config}->get("background_processing"))) {
        $self->{buttons_sizer}->Show($self->{btn_reslice}, ! wxTheApp->{app_config}->get("background_processing"));
        $self->{buttons_sizer}->Layout;
    }
}

# Update preset combo boxes (Print settings, Filament, Printer) from their respective tabs.
# Called by 
#       Slic3r::GUI::Tab::Print::_on_presets_changed
#       Slic3r::GUI::Tab::Filament::_on_presets_changed
#       Slic3r::GUI::Tab::Printer::_on_presets_changed
# when the presets are loaded or the user selects another preset.
# For Print settings and Printer, synchronize the selection index with their tabs.
# For Filament, synchronize the selection index for a single extruder printer only, otherwise keep the selection.
sub update_presets {
    # $group: one of qw(print filament printer)
    # $presets: PresetCollection
    my ($self, $group, $presets) = @_;
    my @choosers = @{$self->{preset_choosers}{$group}};
    if ($group eq 'filament') {
        my $choice_idx = 0;
        if (int(@choosers) == 1) {
            # Single filament printer, synchronize the filament presets.
            wxTheApp->{preset_bundle}->set_filament_preset(0, wxTheApp->{preset_bundle}->filament->get_selected_preset->name);
        }
        foreach my $choice (@choosers) {
            wxTheApp->{preset_bundle}->update_platter_filament_ui($choice_idx, $choice);
            $choice_idx += 1;
        }
    } elsif ($group eq 'print') {
        wxTheApp->{preset_bundle}->print->update_platter_ui($choosers[0]);
    } elsif ($group eq 'printer') {
        # Update the print choosers to only contain the compatible presets, update the dirty flags.
        wxTheApp->{preset_bundle}->print->update_platter_ui($self->{preset_choosers}{print}->[0]);
        # Update the printer choosers, update the dirty flags.
        wxTheApp->{preset_bundle}->printer->update_platter_ui($choosers[0]);
        # Update the filament choosers to only contain the compatible presets, update the color preview, 
        # update the dirty flags.
        my $choice_idx = 0;
        foreach my $choice (@{$self->{preset_choosers}{filament}}) {
            wxTheApp->{preset_bundle}->update_platter_filament_ui($choice_idx, $choice);
            $choice_idx += 1;
        }
    }
    # Synchronize config.ini with the current selections.
    wxTheApp->{preset_bundle}->export_selections(wxTheApp->{app_config});
}

sub add {
    my ($self) = @_;
    my @input_files = wxTheApp->open_model($self);
    $self->load_files(\@input_files);
}

sub load_files {
    my ($self, $input_files) = @_;

    return if ! defined($input_files) || ! scalar(@$input_files);

    my $nozzle_dmrs = $self->{config}->get('nozzle_diameter');
    # One of the files is potentionally a bundle of files. Don't bundle them, but load them one by one.
    # Only bundle .stls or .objs if the printer has multiple extruders.
    my $one_by_one = (@$nozzle_dmrs <= 1) || (@$input_files == 1) || 
       defined(first { $_ =~ /.[aA][mM][fF]$/ || $_ =~ /.[aA][mM][fF].[xX][mM][lL]$/ || $_ =~ /.[zZ][iI][pP].[aA][mM][fF]$/ || $_ =~ /.3[mM][fF]$/ || $_ =~ /.[pP][rR][uI][sS][aA]$/ } @$input_files);
        
    my $process_dialog = Wx::ProgressDialog->new('Loading…', "Processing input file\n" . basename($input_files->[0]), 100, $self, 0);
    $process_dialog->Pulse;
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);

    # new_model to collect volumes, if all of them come from .stl or .obj and there is a chance, that they will be
    # possibly merged into a single multi-part object.
    my $new_model = $one_by_one ? undef : Slic3r::Model->new;
    # Object indices for the UI.
    my @obj_idx = ();
    # Collected file names to display the final message on the status bar.
    my @loaded_files = ();
    # For all input files.
    for (my $i = 0; $i < @$input_files; $i += 1) {
        my $input_file = $input_files->[$i];
        $process_dialog->Update(100. * $i / @$input_files, "Processing input file\n" . basename($input_file));

        my $model;
        if (($input_file =~ /.3[mM][fF]$/) || ($input_file =~ /.[zZ][iI][pP].[aA][mM][fF]$/))
        {
            $model = eval { Slic3r::Model->read_from_archive($input_file, wxTheApp->{preset_bundle}, 0) };
            Slic3r::GUI::show_error($self, $@) if $@;
            $_->load_current_preset for (values %{$self->GetFrame->{options_tabs}});
            wxTheApp->{app_config}->update_config_dir(dirname($input_file));
        }
        else
        {
            $model = eval { Slic3r::Model->read_from_file($input_file, 0) };
            Slic3r::GUI::show_error($self, $@) if $@;
        }

        next if ! defined $model;
        
        if ($model->looks_like_multipart_object) {
            my $dialog = Wx::MessageDialog->new($self,
                "This file contains several objects positioned at multiple heights. "
                . "Instead of considering them as multiple objects, should I consider\n"
                . "this file as a single object having multiple parts?\n",
                'Multi-part object detected', wxICON_WARNING | wxYES | wxNO);
            $model->convert_multipart_object if $dialog->ShowModal() == wxID_YES;
        }
        
        if ($one_by_one) {
            push @obj_idx, $self->load_model_objects(@{$model->objects});
        } else {
            # This must be an .stl or .obj file, which may contain a maximum of one volume.
            $new_model->add_object($_) for (@{$model->objects});
        }
    }

    if ($new_model) {
        my $dialog = Wx::MessageDialog->new($self,
            "Multiple objects were loaded for a multi-material printer.\n"
            . "Instead of considering them as multiple objects, should I consider\n"
            . "these files to represent a single object having multiple parts?\n",
            'Multi-part object detected', wxICON_WARNING | wxYES | wxNO);
        $new_model->convert_multipart_object if $dialog->ShowModal() == wxID_YES;
        push @obj_idx, $self->load_model_objects(@{$new_model->objects});
    }

    # Note the current directory for the file open dialog.
    wxTheApp->{app_config}->update_skein_dir(dirname($input_files->[-1]));
    
    $process_dialog->Destroy;
    $self->statusbar->SetStatusText("Loaded " . join(',', @loaded_files));
    return @obj_idx;
}

sub load_model_objects {
    my ($self, @model_objects) = @_;
    
    my $bed_centerf = $self->bed_centerf;
    my $bed_shape = Slic3r::Polygon->new_scale(@{$self->{config}->bed_shape});
    my $bed_size = $bed_shape->bounding_box->size;
    
    my $need_arrange = 0;
    my $scaled_down = 0;
    my @obj_idx = ();
    foreach my $model_object (@model_objects) {
        my $o = $self->{model}->add_object($model_object);
        my $object_name = $model_object->name;
        $object_name = basename($model_object->input_file) if ($object_name eq '');
        push @{ $self->{objects} }, Slic3r::GUI::Plater::Object->new(name => $object_name);
        push @obj_idx, $#{ $self->{objects} };
    
        if ($model_object->instances_count == 0) {
            # if object has no defined position(s) we need to rearrange everything after loading
            $need_arrange = 1;
        
            # add a default instance and center object around origin
            $o->center_around_origin;  # also aligns object to Z = 0
            $o->add_instance(offset => $bed_centerf);
        }
        
        {
            # if the object is too large (more than 5 times the bed), scale it down
            my $size = $o->bounding_box->size;
            my $ratio = max(@$size[X,Y]) / unscale(max(@$bed_size[X,Y]));
            if ($ratio > 5) {
                $_->set_scaling_factor(1/$ratio) for @{$o->instances};
                $scaled_down = 1;
            }
        }
    
        $self->{print}->auto_assign_extruders($o);
        $self->{print}->add_model_object($o);
    }
    
    # if user turned autocentering off, automatic arranging would disappoint them
    if (! wxTheApp->{app_config}->get("autocenter")) {
        $need_arrange = 0;
    }
    
    if ($scaled_down) {
        Slic3r::GUI::show_info(
            $self,
            'Your object appears to be too large, so it was automatically scaled down to fit your print bed.',
            'Object too large?',
        );
    }
    
    foreach my $obj_idx (@obj_idx) {
        my $object = $self->{objects}[$obj_idx];
        my $model_object = $self->{model}->objects->[$obj_idx];
        $self->{list}->InsertStringItem($obj_idx, $object->name);
        $self->{list}->SetItemFont($obj_idx, Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL))
            if $self->{list}->can('SetItemFont');  # legacy code for wxPerl < 0.9918 not supporting SetItemFont()
    
        $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
        $self->{list}->SetItem($obj_idx, 2, ($model_object->instances->[0]->scaling_factor * 100) . "%");
    
        $self->reset_thumbnail($obj_idx);
    }
    $self->arrange if $need_arrange;
    $self->update;
    
    # zoom to objects
    $self->{canvas3D}->zoom_to_volumes
        if $self->{canvas3D};
    
    $self->{list}->Update;
    $self->{list}->Select($obj_idx[-1], 1);
    $self->object_list_changed;
    
    $self->schedule_background_process;
    
    return @obj_idx;
}

sub bed_centerf {
    my ($self) = @_;
    
    my $bed_shape = Slic3r::Polygon->new_scale(@{$self->{config}->bed_shape});
    my $bed_center = $bed_shape->bounding_box->center;
    return Slic3r::Pointf->new(unscale($bed_center->x), unscale($bed_center->y)); #)
}

sub remove {
    my $self = shift;
    my ($obj_idx) = @_;
    
    $self->stop_background_process;
    
    # Prevent toolpaths preview from rendering while we modify the Print object
    $self->{toolpaths2D}->enabled(0) if $self->{toolpaths2D};
    $self->{preview3D}->enabled(0) if $self->{preview3D};
    
    # if no object index is supplied, remove the selected one
    if (! defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
        return if ! defined $obj_idx;
    }
    
    splice @{$self->{objects}}, $obj_idx, 1;
    $self->{model}->delete_object($obj_idx);
    $self->{print}->delete_object($obj_idx);
    $self->{list}->DeleteItem($obj_idx);
    $self->object_list_changed;
    
    $self->select_object(undef);
    $self->update;
    $self->schedule_background_process;
}

sub reset {
    my $self = shift;
    
    $self->stop_background_process;
    
    # Prevent toolpaths preview from rendering while we modify the Print object
    $self->{toolpaths2D}->enabled(0) if $self->{toolpaths2D};
    $self->{preview3D}->enabled(0) if $self->{preview3D};
    
    @{$self->{objects}} = ();
    $self->{model}->clear_objects;
    $self->{print}->clear_objects;
    $self->{list}->DeleteAllItems;
    $self->object_list_changed;
    
    $self->select_object(undef);
    $self->update;
}

sub increase {
    my ($self, $copies) = @_;
    $copies //= 1;
    my ($obj_idx, $object) = $self->selected_object;
    return if ! defined $obj_idx;
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $instance = $model_object->instances->[-1];
    for my $i (1..$copies) {
        $instance = $model_object->add_instance(
            offset          => Slic3r::Pointf->new(map 10+$_, @{$instance->offset}),
            scaling_factor  => $instance->scaling_factor,
            rotation        => $instance->rotation,
        );
        $self->{print}->objects->[$obj_idx]->add_copy($instance->offset);
    }
    $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
    
    # only autoarrange if user has autocentering enabled
    $self->stop_background_process;
    if (wxTheApp->{app_config}->get("autocenter")) {
        $self->arrange;
    } else {
        $self->update;
    }
    $self->schedule_background_process;
}

sub decrease {
    my ($self, $copies_asked) = @_;
    my $copies = $copies_asked // 1;
    my ($obj_idx, $object) = $self->selected_object;
    return if ! defined $obj_idx;

    $self->stop_background_process;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    if ($model_object->instances_count > $copies) {
        for my $i (1..$copies) {
            $model_object->delete_last_instance;
            $self->{print}->objects->[$obj_idx]->delete_last_copy;
        }
        $self->{list}->SetItem($obj_idx, 1, $model_object->instances_count);
    } elsif (defined $copies_asked) {
        # The "decrease" came from the "set number of copies" dialog.
        $self->remove;
    } else {
        # The "decrease" came from the "-" button. Don't allow the object to disappear.
        $self->resume_background_process;
        return;
    }
    
    if ($self->{objects}[$obj_idx]) {
        $self->{list}->Select($obj_idx, 0);
        $self->{list}->Select($obj_idx, 1);
    }
    $self->update;
    $self->schedule_background_process;
}

sub set_number_of_copies {
    my ($self) = @_;
    
    $self->pause_background_process;
    
    # get current number of copies
    my ($obj_idx, $object) = $self->selected_object;
    my $model_object = $self->{model}->objects->[$obj_idx];
    
    # prompt user
    my $copies = Wx::GetNumberFromUser("", "Enter the number of copies of the selected object:", "Copies", $model_object->instances_count, 0, 1000, $self);
    my $diff = $copies - $model_object->instances_count;
    if ($diff == 0) {
        # no variation
        $self->resume_background_process;
    } elsif ($diff > 0) {
        $self->increase($diff);
    } elsif ($diff < 0) {
        $self->decrease(-$diff);
    }
}

sub _get_number_from_user {
    my ($self, $title, $prompt_message, $error_message, $default, $only_positive) = @_;
    for (;;) {
        my $value = Wx::GetTextFromUser($prompt_message, $title, $default, $self);
        # Accept both dashes and dots as a decimal separator.
        $value =~ s/,/./;
        # If scaling value is being entered, remove the trailing percent sign.
        $value =~ s/%$// if $only_positive;
        # User canceled the selection, return undef.
        return if $value eq '';
        # Validate a numeric value.
        return $value if ($value =~ /^-?\d*(?:\.\d*)?$/) && (! $only_positive || $value > 0);
        Wx::MessageBox(
            $error_message . 
            (($only_positive && $value <= 0) ? 
                ": $value\nNon-positive value." : 
                ": $value\nNot a numeric value."), 
            "Slic3r Error", wxOK | wxICON_EXCLAMATION, $self);
        $default = $value;
    }
}

sub rotate {
    my ($self, $angle, $axis, $relative_key) = @_;
    $relative_key //= 'absolute'; # relative or absolute coordinates
    $axis //= Z; # angle is in degrees

    my $relative = $relative_key eq 'relative';    
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
        
    if (!defined $angle) {
        my $axis_name = $axis == X ? 'X' : $axis == Y ? 'Y' : 'Z';
        my $default = $axis == Z ? rad2deg($model_instance->rotation) : 0;
        $angle = $self->_get_number_from_user("Enter the rotation angle:", "Rotate around $axis_name axis", "Invalid rotation angle entered", $default);
        return if $angle eq '';
    }
    
    $self->stop_background_process;
    
    if ($axis == Z) {
        my $new_angle = deg2rad($angle);
        $_->set_rotation(($relative ? $_->rotation : 0.) + $new_angle) for @{ $model_object->instances };
        $object->transform_thumbnail($self->{model}, $obj_idx);
    } else {
        # rotation around X and Y needs to be performed on mesh
        # so we first apply any Z rotation
        if ($model_instance->rotation != 0) {
            $model_object->rotate($model_instance->rotation, Z);
            $_->set_rotation(0) for @{ $model_object->instances };
        }
        $model_object->rotate(deg2rad($angle), $axis);
        
        # realign object to Z = 0
        $model_object->center_around_origin;
        $self->reset_thumbnail($obj_idx);
    }
    
    # update print and start background processing
    $self->{print}->add_model_object($model_object, $obj_idx);
    
    $self->selection_changed;  # refresh info (size etc.)
    $self->update;
    $self->schedule_background_process;
}

sub mirror {
    my ($self, $axis) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # apply Z rotation before mirroring
    if ($model_instance->rotation != 0) {
        $model_object->rotate($model_instance->rotation, Z);
        $_->set_rotation(0) for @{ $model_object->instances };
    }
    
    $model_object->mirror($axis);
    
    # realign object to Z = 0
    $model_object->center_around_origin;
    $self->reset_thumbnail($obj_idx);
        
    # update print and start background processing
    $self->stop_background_process;
    $self->{print}->add_model_object($model_object, $obj_idx);
    
    $self->selection_changed;  # refresh info (size etc.)
    $self->update;
    $self->schedule_background_process;
}

sub changescale {
    my ($self, $axis, $tosize) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    my $object_size = $model_object->bounding_box->size;
    my $bed_size = Slic3r::Polygon->new_scale(@{$self->{config}->bed_shape})->bounding_box->size;
    
    if (defined $axis) {
        my $axis_name = $axis == X ? 'X' : $axis == Y ? 'Y' : 'Z';
        my $scale;
        if ($tosize) {
            my $cursize = $object_size->[$axis];
            my $newsize = $self->_get_number_from_user(
                sprintf('Enter the new size for the selected object (print bed: %smm):', unscale($bed_size->[$axis])), 
                "Scale along $axis_name", 'Invalid scaling value entered', $cursize, 1);
            return if $newsize eq '';
            $scale = $newsize / $cursize * 100;
        } else {
            $scale = $self->_get_number_from_user('Enter the scale % for the selected object:', "Scale along $axis_name", 'Invalid scaling value entered', 100, 1);
            return if $scale eq '';
        }
        
        # apply Z rotation before scaling
        if ($model_instance->rotation != 0) {
            $model_object->rotate($model_instance->rotation, Z);
            $_->set_rotation(0) for @{ $model_object->instances };
        }
        
        my $versor = [1,1,1];
        $versor->[$axis] = $scale/100;
        $model_object->scale_xyz(Slic3r::Pointf3->new(@$versor));
        #FIXME Scale the layer height profile when $axis == Z?
        #FIXME Scale the layer height ranges $axis == Z?
        # object was already aligned to Z = 0, so no need to realign it
        $self->reset_thumbnail($obj_idx);
    } else {
        my $scale;
        if ($tosize) {
            my $cursize = max(@$object_size);
            my $newsize = $self->_get_number_from_user('Enter the new max size for the selected object:', 'Scale', 'Invalid scaling value entered', $cursize, 1);
            return if ! defined($newsize) || $newsize eq '';
            $scale = $model_instance->scaling_factor * $newsize / $cursize * 100;
        } else {
            # max scale factor should be above 2540 to allow importing files exported in inches
            $scale = $self->_get_number_from_user('Enter the scale % for the selected object:', 'Scale', 'Invalid scaling value entered', $model_instance->scaling_factor*100, 1);
            return if ! defined($scale) || $scale eq '';
        }
    
        $self->{list}->SetItem($obj_idx, 2, "$scale%");
        $scale /= 100;  # turn percent into factor
        
        my $variation = $scale / $model_instance->scaling_factor;
        #FIXME Scale the layer height profile?
        foreach my $range (@{ $model_object->layer_height_ranges }) {
            $range->[0] *= $variation;
            $range->[1] *= $variation;
        }
        $_->set_scaling_factor($scale) for @{ $model_object->instances };
        $object->transform_thumbnail($self->{model}, $obj_idx);
    }
    
    # update print and start background processing
    $self->stop_background_process;
    $self->{print}->add_model_object($model_object, $obj_idx);
    
    $self->selection_changed(1);  # refresh info (size, volume etc.)
    $self->update;
    $self->schedule_background_process;
}

sub arrange {
    my $self = shift;
    
    $self->pause_background_process;
    
    my $bb = Slic3r::Geometry::BoundingBoxf->new_from_points($self->{config}->bed_shape);
    my $success = $self->{model}->arrange_objects(wxTheApp->{preset_bundle}->full_config->min_object_distance, $bb);
    # ignore arrange failures on purpose: user has visual feedback and we don't need to warn him
    # when parts don't fit in print bed
    
    # Force auto center of the aligned grid of of objects on the print bed.
    $self->update(1);
}

sub split_object {
    my $self = shift;
    
    my ($obj_idx, $current_object)  = $self->selected_object;
    
    # we clone model object because split_object() adds the split volumes
    # into the same model object, thus causing duplicates when we call load_model_objects()
    my $new_model = $self->{model}->clone;  # store this before calling get_object()
    my $current_model_object = $new_model->get_object($obj_idx);
    
    if ($current_model_object->volumes_count > 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object can't be split because it contains more than one volume/material.");
        return;
    }
    
    $self->pause_background_process;
    
    my @model_objects = @{$current_model_object->split_object};
    if (@model_objects == 1) {
        $self->resume_background_process;
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be split because it contains only one part.");
        $self->resume_background_process;
        return;
    }
    
    $_->center_around_origin for (@model_objects);

    $self->remove($obj_idx);
    $current_object = $obj_idx = undef;
    
    # load all model objects at once, otherwise the plate would be rearranged after each one
    # causing original positions not to be kept
    $self->load_model_objects(@model_objects);
}

sub schedule_background_process {
    my ($self) = @_;
    
    if (defined $self->{apply_config_timer}) {
        $self->{apply_config_timer}->Start(PROCESS_DELAY, 1);  # 1 = one shot
    }
}

# Executed asynchronously by a timer every PROCESS_DELAY (0.5 second).
# The timer is started by schedule_background_process(), 
sub async_apply_config {
    my ($self) = @_;

    # pause process thread before applying new config
    # since we don't want to touch data that is being used by the threads
    $self->pause_background_process;
    
    # apply new config
    my $invalidated = $self->{print}->apply_config(wxTheApp->{preset_bundle}->full_config);

    # Just redraw the 3D canvas without reloading the scene.
#    $self->{canvas3D}->Refresh if ($invalidated && $self->{canvas3D}->layer_editing_enabled);
    $self->{canvas3D}->Refresh if ($self->{canvas3D}->layer_editing_enabled);

    # Hide the slicing results if the current slicing status is no more valid.    
    $self->{"print_info_box_show"}->(0) if $invalidated;

    if (wxTheApp->{app_config}->get("background_processing")) {    
        if ($invalidated) {
            # kill current thread if any
            $self->stop_background_process;
        } else {
            $self->resume_background_process;
        }
        # schedule a new process thread in case it wasn't running
        $self->start_background_process;
    }

    # Reset preview canvases. If the print has been invalidated, the preview canvases will be cleared.
    # Otherwise they will be just refreshed.
    if ($invalidated) {
        $self->{gcode_preview_data}->reset;
        $self->{toolpaths2D}->reload_print if $self->{toolpaths2D};
        $self->{preview3D}->reload_print if $self->{preview3D};
    }
}

sub start_background_process {
    my ($self) = @_;
    
    return if !@{$self->{objects}};
    return if $self->{process_thread};
    
    # It looks like declaring a local $SIG{__WARN__} prevents the ugly
    # "Attempt to free unreferenced scalar" warning...
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    
    # don't start process thread if config is not valid
    eval {
        # this will throw errors if config is not valid
        wxTheApp->{preset_bundle}->full_config->validate;
        $self->{print}->validate;
    };
    if ($@) {
        $self->statusbar->SetStatusText($@);
        return;
    }
    
    # Copy the names of active presets into the placeholder parser.
    wxTheApp->{preset_bundle}->export_selections_pp($self->{print}->placeholder_parser);
    
    # start thread
    @_ = ();
    $self->{process_thread} = Slic3r::spawn_thread(sub {
        eval {
            $self->{print}->process;
        };
        my $event = Wx::CommandEvent->new($PROCESS_COMPLETED_EVENT);
        if ($@) {
            Slic3r::debugf "Background process error: $@\n";
            $event->SetInt(0);
            $event->SetString($@);
        } else {
            $event->SetInt(1);
        }
        Wx::PostEvent($self, $event);
        Slic3r::thread_cleanup();
    });
    Slic3r::debugf "Background processing started.\n";
}

sub stop_background_process {
    my ($self) = @_;
    
    $self->{apply_config_timer}->Stop if defined $self->{apply_config_timer};
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("");
    $self->{toolpaths2D}->reload_print if $self->{toolpaths2D};
    $self->{preview3D}->reload_print if $self->{preview3D};
    
    if ($self->{process_thread}) {
        Slic3r::debugf "Killing background process.\n";
        Slic3r::kill_all_threads();
        $self->{process_thread} = undef;
    } else {
        Slic3r::debugf "No background process running.\n";
    }
    
    # if there's an export process, kill that one as well
    if ($self->{export_thread}) {
        Slic3r::debugf "Killing background export process.\n";
        Slic3r::kill_all_threads();
        $self->{export_thread} = undef;
    }
}

sub pause_background_process {
    my ($self) = @_;
    
    if ($self->{process_thread} || $self->{export_thread}) {
        Slic3r::pause_all_threads();
        return 1;
    } elsif (defined $self->{apply_config_timer} && $self->{apply_config_timer}->IsRunning) {
        $self->{apply_config_timer}->Stop;
        return 1;
    }
    
    return 0;
}

sub resume_background_process {
    my ($self) = @_;
    
    if ($self->{process_thread} || $self->{export_thread}) {
        Slic3r::resume_all_threads();
    }
}

sub reslice {
    # explicitly cancel a previous thread and start a new one.
    my ($self) = @_;
    # Don't reslice if export of G-code or sending to OctoPrint is running.
    if (! defined($self->{export_gcode_output_file}) && ! defined($self->{send_gcode_file})) {
        # Stop the background processing threads, stop the async update timer.
        $self->stop_background_process;
        # Rather perform one additional unnecessary update of the print object instead of skipping a pending async update.
        $self->async_apply_config;
        $self->statusbar->SetCancelCallback(sub {
            $self->stop_background_process;
            $self->statusbar->SetStatusText("Slicing cancelled");
            # this updates buttons status
            $self->object_list_changed;
        });
        $self->start_background_process;
    }
}

sub export_gcode {
    my ($self, $output_file) = @_;
    
    return if !@{$self->{objects}};
    
    if ($self->{export_gcode_output_file}) {
        Wx::MessageDialog->new($self, "Another export job is currently running.", 'Error', wxOK | wxICON_ERROR)->ShowModal;
        return;
    }
    
    # if process is not running, validate config
    # (we assume that if it is running, config is valid)
    eval {
        # this will throw errors if config is not valid
        wxTheApp->{preset_bundle}->full_config->validate;
        $self->{print}->validate;
    };
    Slic3r::GUI::catch_error($self) and return;
    
    
    # apply config and validate print
    my $config = wxTheApp->{preset_bundle}->full_config;
    eval {
        # this will throw errors if config is not valid
        $config->validate;
        $self->{print}->apply_config($config);
        $self->{print}->validate;
    };
    Slic3r::GUI::catch_error($self) and return;
    
    # select output file
    if ($output_file) {
        $self->{export_gcode_output_file} = eval { $self->{print}->output_filepath($output_file) };
        Slic3r::GUI::catch_error($self) and return;
    } else {
        my $default_output_file = eval { $self->{print}->output_filepath($main::opt{output} // '') };
        Slic3r::GUI::catch_error($self) and return;
        my $dlg = Wx::FileDialog->new($self, 'Save G-code file as:', 
            wxTheApp->{app_config}->get_last_output_dir(dirname($default_output_file)),
            basename($default_output_file), &Slic3r::GUI::FILE_WILDCARDS->{gcode}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return;
        }
        my $path = $dlg->GetPath;
        wxTheApp->{app_config}->update_last_output_dir(dirname($path));
        $self->{export_gcode_output_file} = $path;
        $dlg->Destroy;
    }
    
    $self->statusbar->StartBusy;
    
    $self->statusbar->SetCancelCallback(sub {
        $self->stop_background_process;
        $self->statusbar->SetStatusText("Export cancelled");
        $self->{export_gcode_output_file} = undef;
        $self->{send_gcode_file} = undef;
        
        # this updates buttons status
        $self->object_list_changed;
    });
    
    # start background process, whose completion event handler
    # will detect $self->{export_gcode_output_file} and proceed with export
    $self->start_background_process;
    
    # this updates buttons status
    $self->object_list_changed;
    
    return $self->{export_gcode_output_file};
}

# This gets called only if we have threads.
sub on_process_completed {
    my ($self, $error) = @_;
    
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText($error // "");
    
    Slic3r::debugf "Background processing completed.\n";
    $self->{process_thread}->detach if $self->{process_thread};
    $self->{process_thread} = undef;
    
    # if we're supposed to perform an explicit export let's display the error in a dialog
    if ($error && $self->{export_gcode_output_file}) {
        $self->{export_gcode_output_file} = undef;
        Slic3r::GUI::show_error($self, $error);
    }
    
    return if $error;
    $self->{toolpaths2D}->reload_print if $self->{toolpaths2D};
    $self->{preview3D}->reload_print if $self->{preview3D};
    
    # if we have an export filename, start a new thread for exporting G-code
    if ($self->{export_gcode_output_file}) {
        @_ = ();
        
        # workaround for "Attempt to free un referenced scalar..."
        our $_thread_self = $self;
        
        $self->{export_thread} = Slic3r::spawn_thread(sub {
            eval {
                $_thread_self->{print}->export_gcode(output_file => $_thread_self->{export_gcode_output_file}, gcode_preview_data => $_thread_self->{gcode_preview_data});
            };
            my $export_completed_event = Wx::CommandEvent->new($EXPORT_COMPLETED_EVENT);
            if ($@) {
                {
                    my $error_event = Wx::CommandEvent->new($ERROR_EVENT);
                    $error_event->SetString($@);
                    Wx::PostEvent($_thread_self, $error_event);
                }
                $export_completed_event->SetInt(0);
                $export_completed_event->SetString($@);
            } else {
                $export_completed_event->SetInt(1);
            }
            Wx::PostEvent($_thread_self, $export_completed_event);
            Slic3r::thread_cleanup();
        });
        Slic3r::debugf "Background G-code export started.\n";
    }
}

# This gets called also if we have no threads.
sub on_progress_event {
    my ($self, $percent, $message) = @_;
    
    $self->statusbar->SetProgress($percent);
    $self->statusbar->SetStatusText("$message…");
}

# Called when the G-code export finishes, either successfully or with an error.
# This gets called also if we don't have threads.
sub on_export_completed {
    my ($self, $result) = @_;
    
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("");
    
    Slic3r::debugf "Background export process completed.\n";
    $self->{export_thread}->detach if $self->{export_thread};
    $self->{export_thread} = undef;
    
    my $message;
    my $send_gcode = 0;
    my $do_print = 0;
    if ($result) {
        # G-code file exported successfully.
        if ($self->{print_file}) {
            $message = "File added to print queue";
            $do_print = 1;
        } elsif ($self->{send_gcode_file}) {
            $message = "Sending G-code file to the OctoPrint server...";
            $send_gcode = 1;
        } else {
            $message = "G-code file exported to " . $self->{export_gcode_output_file};
        }
    } else {
        $message = "Export failed";
    }
    $self->{export_gcode_output_file} = undef;
    $self->statusbar->SetStatusText($message);
    wxTheApp->notify($message);
    
    $self->do_print if $do_print;
    # Send $self->{send_gcode_file} to OctoPrint.
    $self->send_gcode if $send_gcode;
    $self->{print_file} = undef;
    $self->{send_gcode_file} = undef;
    $self->{"print_info_cost"}->SetLabel(sprintf("%.2f" , $self->{print}->total_cost));
    $self->{"print_info_fil_g"}->SetLabel(sprintf("%.2f" , $self->{print}->total_weight));
    $self->{"print_info_fil_mm3"}->SetLabel(sprintf("%.2f" , $self->{print}->total_extruded_volume));
    $self->{"print_info_time"}->SetLabel($self->{print}->estimated_print_time);
    $self->{"print_info_fil_m"}->SetLabel(sprintf("%.2f" , $self->{print}->total_used_filament / 1000));
    $self->{"print_info_box_show"}->(1);

    # this updates buttons status
    $self->object_list_changed;
    
    # refresh preview
    $self->{toolpaths2D}->reload_print if $self->{toolpaths2D};
    $self->{preview3D}->reload_print if $self->{preview3D};
}

sub do_print {
    my ($self) = @_;
    
    my $controller = $self->GetFrame->{controller};
    my $printer_preset = wxTheApp->{preset_bundle}->printer->get_edited_preset;    
    my $printer_panel = $controller->add_printer($printer_preset->name, $printer_preset->config);
    
    my $filament_stats = $self->{print}->filament_stats;
    my $filament_names = wxTheApp->{preset_bundle}->filament_presets;
    $filament_stats = { map { $filament_names->[$_] => $filament_stats->{$_} } keys %$filament_stats };
    $printer_panel->load_print_job($self->{print_file}, $filament_stats);
    
    $self->GetFrame->select_tab(1);
}

# Send $self->{send_gcode_file} to OctoPrint.
#FIXME Currently this call blocks the UI. Make it asynchronous.
sub send_gcode {
    my ($self) = @_;
    
    $self->statusbar->StartBusy;
    
    my $ua = LWP::UserAgent->new;
    $ua->timeout(180);
    
    my $res = $ua->post(
        "http://" . $self->{config}->octoprint_host . "/api/files/local",
        Content_Type => 'form-data',
        'X-Api-Key' => $self->{config}->octoprint_apikey,
        Content => [
            file => [ 
                # On Windows, the path has to be encoded in local code page for perl to be able to open it.
                Slic3r::encode_path($self->{send_gcode_file}),
                # Remove the UTF-8 flag from the perl string, so the LWP::UserAgent can insert 
                # the UTF-8 encoded string into the request as a byte stream.
                Slic3r::path_to_filename_raw($self->{send_gcode_file})
            ],
            print => $self->{send_gcode_file_print} ? 1 : 0,
        ],
    );
    
    $self->statusbar->StopBusy;
    
    if ($res->is_success) {
        $self->statusbar->SetStatusText("G-code file successfully uploaded to the OctoPrint server");
    } else {
        my $message = "Error while uploading to the OctoPrint server: " . $res->status_line;
        Slic3r::GUI::show_error($self, $message);
        $self->statusbar->SetStatusText($message);
    }
}

sub export_stl {
    my ($self) = @_;
    return if !@{$self->{objects}};
    # Ask user for a file name to write into.
    my $output_file = $self->_get_export_file('STL') or return;
    # Store a binary STL.
    $self->{model}->store_stl($output_file, 1);
    $self->statusbar->SetStatusText("STL file exported to $output_file");
}

sub reload_from_disk {
    my ($self) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    
    my $model_object = $self->{model}->objects->[$obj_idx];
    #FIXME convert to local file encoding
    return if !$model_object->input_file
        || !-e $model_object->input_file;
    
    my @new_obj_idx = $self->load_files([$model_object->input_file]);
    return if !@new_obj_idx;
    
    foreach my $new_obj_idx (@new_obj_idx) {
        my $o = $self->{model}->objects->[$new_obj_idx];
        $o->clear_instances;
        $o->add_instance($_) for @{$model_object->instances};
        #$o->invalidate_bounding_box;
        
        if ($o->volumes_count == $model_object->volumes_count) {
            for my $i (0..($o->volumes_count-1)) {
                $o->get_volume($i)->config->apply($model_object->get_volume($i)->config);
            }
        }
        #FIXME restore volumes and their configs, layer_height_ranges, layer_height_profile, layer_height_profile_valid,
    }
    
    $self->remove($obj_idx);
}

sub export_object_stl {
    my ($self) = @_;
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    my $model_object = $self->{model}->objects->[$obj_idx];
    # Ask user for a file name to write into.        
    my $output_file = $self->_get_export_file('STL') or return;
    $model_object->mesh->write_binary($output_file);
    $self->statusbar->SetStatusText("STL file exported to $output_file");
}

sub export_amf {
    my ($self) = @_;
    return if !@{$self->{objects}};
    # Ask user for a file name to write into.
    my $output_file = $self->_get_export_file('AMF') or return;
    my $res = $self->{model}->store_amf($output_file, $self->{print});
    if ($res)
    {
        $self->statusbar->SetStatusText("AMF file exported to $output_file");
    }
    else
    {
        $self->statusbar->SetStatusText("Error exporting AMF file $output_file");
    }
}

sub export_3mf {
    my ($self) = @_;
    return if !@{$self->{objects}};
    # Ask user for a file name to write into.
    my $output_file = $self->_get_export_file('3MF') or return;
    my $res = $self->{model}->store_3mf($output_file, $self->{print});
    if ($res)
    {
        $self->statusbar->SetStatusText("3MF file exported to $output_file");
    }
    else
    {
        $self->statusbar->SetStatusText("Error exporting 3MF file $output_file");
    }
}

# Ask user to select an output file for a given file format (STl, AMF, 3MF).
# Propose a default file name based on the 'output_filename_format' configuration value.
sub _get_export_file {
    my ($self, $format) = @_;    
    my $suffix = '';
    if ($format eq 'STL')
    {
        $suffix = '.stl';
    }
    elsif ($format eq 'AMF')
    {
        $suffix = '.zip.amf';
    }
    elsif ($format eq '3MF')
    {
        $suffix = '.3mf';
    }
    my $output_file = eval { $self->{print}->output_filepath($main::opt{output} // '') };
    Slic3r::GUI::catch_error($self) and return undef;
    $output_file =~ s/\.[gG][cC][oO][dD][eE]$/$suffix/;
    my $dlg = Wx::FileDialog->new($self, "Save $format file as:", dirname($output_file),
        basename($output_file), &Slic3r::GUI::MODEL_WILDCARD, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if ($dlg->ShowModal != wxID_OK) {
        $dlg->Destroy;
        return undef;
    }
    $output_file = $dlg->GetPath;
    $dlg->Destroy;
    return $output_file;
}

sub reset_thumbnail {
    my ($self, $obj_idx) = @_;
    $self->{objects}[$obj_idx]->thumbnail(undef);
}

# this method gets called whenever print center is changed or the objects' bounding box changes
# (i.e. when an object is added/removed/moved/rotated/scaled)
sub update {
    my ($self, $force_autocenter) = @_;

    if (wxTheApp->{app_config}->get("autocenter") || $force_autocenter) {
        $self->{model}->center_instances_around_point($self->bed_centerf);
    }
    
    my $running = $self->pause_background_process;
    my $invalidated = $self->{print}->reload_model_instances();
    
    # The mere fact that no steps were invalidated when reloading model instances 
    # doesn't mean that all steps were done: for example, validation might have 
    # failed upon previous instance move, so we have no running thread and no steps
    # are invalidated on this move, thus we need to schedule a new run.
    if ($invalidated || !$running) {
        $self->schedule_background_process;
    } else {
        $self->resume_background_process;
    }

    $self->{canvas}->reload_scene if $self->{canvas};
    $self->{canvas3D}->reload_scene if $self->{canvas3D};
    $self->{preview3D}->reload_print if $self->{preview3D};
}

# When a number of extruders changes, the UI needs to be updated to show a single filament selection combo box per extruder.
# Also the wxTheApp->{preset_bundle}->filament_presets needs to be resized accordingly
# and some reasonable default has to be selected for the additional extruders.
sub on_extruders_change {
    my ($self, $num_extruders) = @_;
    my $choices = $self->{preset_choosers}{filament};

    while (int(@$choices) < $num_extruders) {
        # copy strings from first choice
        my @presets = $choices->[0]->GetStrings;
        
        # initialize new choice
        my $choice = Wx::BitmapComboBox->new($self, -1, "", wxDefaultPosition, wxDefaultSize, [@presets], wxCB_READONLY);
        my $extruder_idx = scalar @$choices;
        EVT_LEFT_DOWN($choice, sub { $self->filament_color_box_lmouse_down($extruder_idx, @_); } );
        push @$choices, $choice;
        # copy icons from first choice
        $choice->SetItemBitmap($_, $choices->[0]->GetItemBitmap($_)) for 0..$#presets;
        # insert new choice into sizer
        $self->{presets_sizer}->Insert(4 + ($#$choices-1)*2, 0, 0);
        $self->{presets_sizer}->Insert(5 + ($#$choices-1)*2, $choice, 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
        # setup the listener
        EVT_COMBOBOX($choice, $choice, sub {
            my ($choice) = @_;
            wxTheApp->CallAfter(sub {
                $self->_on_select_preset('filament', $choice, $extruder_idx);
            });
        });
        # initialize selection
        wxTheApp->{preset_bundle}->update_platter_filament_ui($extruder_idx, $choice);
    }
    
    # remove unused choices if any
    while (@$choices > $num_extruders) {
        $self->{presets_sizer}->Remove(4 + ($#$choices-1)*2);  # label
        $self->{presets_sizer}->Remove(4 + ($#$choices-1)*2);  # wxChoice
        $choices->[-1]->Destroy;
        pop @$choices;
    }
    $self->Layout;
}

sub on_config_change {
    my ($self, $config) = @_;
    
    my $update_scheduled;
    foreach my $opt_key (@{$self->{config}->diff($config)}) {
        $self->{config}->set($opt_key, $config->get($opt_key));
        if ($opt_key eq 'bed_shape') {
            $self->{canvas}->update_bed_size;
            $self->{canvas3D}->update_bed_size if $self->{canvas3D};
            $self->{preview3D}->set_bed_shape($self->{config}->bed_shape)
                if $self->{preview3D};
            $update_scheduled = 1;
        } elsif ($opt_key =~ '^wipe_tower' || $opt_key eq 'single_extruder_multi_material') {
            $update_scheduled = 1;
        } elsif ($opt_key eq 'serial_port') {
            $self->{btn_print}->Show($config->get('serial_port'));
            $self->Layout;
        } elsif ($opt_key eq 'octoprint_host') {
            $self->{btn_send_gcode}->Show($config->get('octoprint_host'));
            $self->Layout;
        } elsif ($opt_key eq 'variable_layer_height') {
            if ($config->get('variable_layer_height') != 1) {
                if ($self->{htoolbar}) {
                    $self->{htoolbar}->EnableTool(TB_LAYER_EDITING, 0);
                    $self->{htoolbar}->ToggleTool(TB_LAYER_EDITING, 0);
                } else {
                    $self->{"btn_layer_editing"}->Disable;
                    $self->{"btn_layer_editing"}->SetValue(0);
                }
                $self->{canvas3D}->layer_editing_enabled(0);
                $self->{canvas3D}->Refresh;
                $self->{canvas3D}->Update;
            } elsif ($self->{canvas3D}->layer_editing_allowed) {
                # Want to allow the layer editing, but do it only if the OpenGL supports it.
                if ($self->{htoolbar}) {
                    $self->{htoolbar}->EnableTool(TB_LAYER_EDITING, 1);
                } else {
                    $self->{"btn_layer_editing"}->Enable;
                }
            }
        } elsif ($opt_key eq 'extruder_colour') {
            $update_scheduled = 1;
            my $extruder_colors = $config->get('extruder_colour');
            $self->{preview3D}->set_number_extruders(scalar(@{$extruder_colors}));
        }
    }

    $self->update if $update_scheduled;
    
    return if !$self->GetFrame->is_loaded;
    
    # (re)start timer
    $self->schedule_background_process;
}

sub list_item_deselected {
    my ($self, $event) = @_;
    return if $PreventListEvents;
    if ($self->{list}->GetFirstSelected == -1) {
        $self->select_object(undef);
        $self->{canvas}->Refresh;
        #FIXME VBOs are being refreshed just to change a selection color?
        $self->{canvas3D}->reload_scene if $self->{canvas3D};
    }
}

sub list_item_selected {
    my ($self, $event) = @_;
    return if $PreventListEvents;
    my $obj_idx = $event->GetIndex;
    $self->select_object($obj_idx);
    $self->{canvas}->Refresh;
    #FIXME VBOs are being refreshed just to change a selection color?
    $self->{canvas3D}->reload_scene if $self->{canvas3D};
}

sub list_item_activated {
    my ($self, $event, $obj_idx) = @_;
    
    $obj_idx //= $event->GetIndex;
	$self->object_settings_dialog($obj_idx);
}

# Called when clicked on the filament preset combo box.
# When clicked on the icon, show the color picker.
sub filament_color_box_lmouse_down
{
    my ($self, $extruder_idx, $combobox, $event) = @_;
    my $pos = $event->GetLogicalPosition(Wx::ClientDC->new($combobox));
    my( $x, $y ) = ( $pos->x, $pos->y );
    if ($x > 24) {
        # Let the combo box process the mouse click.
        $event->Skip;
    } else {
        # Swallow the mouse click and open the color picker.
        my $data = Wx::ColourData->new;
        $data->SetChooseFull(1);
        my $dialog = Wx::ColourDialog->new($self->GetFrame, $data);
        if ($dialog->ShowModal == wxID_OK) {
            my $cfg = Slic3r::Config->new;
            my $colors = wxTheApp->{preset_bundle}->full_config->get('extruder_colour');
            $colors->[$extruder_idx] = $dialog->GetColourData->GetColour->GetAsString(wxC2S_HTML_SYNTAX);
            $cfg->set('extruder_colour', $colors);
            $self->GetFrame->{options_tabs}{printer}->load_config($cfg);
            wxTheApp->{preset_bundle}->update_platter_filament_ui($extruder_idx, $combobox);
        }
        $dialog->Destroy();
    }
}

sub object_cut_dialog {
    my ($self, $obj_idx) = @_;
    
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    if (!$Slic3r::GUI::have_OpenGL) {
        Slic3r::GUI::show_error($self, "Please install the OpenGL modules to use this feature (see build instructions).");
        return;
    }
    
    my $dlg = Slic3r::GUI::Plater::ObjectCutDialog->new($self,
		object              => $self->{objects}[$obj_idx],
		model_object        => $self->{model}->objects->[$obj_idx],
	);
	return unless $dlg->ShowModal == wxID_OK;
	
	if (my @new_objects = $dlg->NewModelObjects) {
	    $self->remove($obj_idx);
	    $self->load_model_objects(grep defined($_), @new_objects);
	    $self->arrange;
	}
}

sub object_settings_dialog {
    my ($self, $obj_idx) = @_;
    ($obj_idx, undef) = $self->selected_object if !defined $obj_idx;
    my $model_object = $self->{model}->objects->[$obj_idx];
    
    # validate config before opening the settings dialog because
    # that dialog can't be closed if validation fails, but user
    # can't fix any error which is outside that dialog
    eval { wxTheApp->{preset_bundle}->full_config->validate; };
    return if Slic3r::GUI::catch_error($_[0]);
    
    my $dlg = Slic3r::GUI::Plater::ObjectSettingsDialog->new($self,
		object          => $self->{objects}[$obj_idx],
		model_object    => $model_object,
        config          => wxTheApp->{preset_bundle}->full_config,
	);
	$self->pause_background_process;
	$dlg->ShowModal;
	
    # update thumbnail since parts may have changed
    if ($dlg->PartsChanged) {
	    # recenter and re-align to Z = 0
	    $model_object->center_around_origin;
        $self->reset_thumbnail($obj_idx);
    }
	
	# update print
	if ($dlg->PartsChanged || $dlg->PartSettingsChanged) {
	    $self->stop_background_process;
        $self->{print}->reload_object($obj_idx);
        $self->schedule_background_process;
        $self->{canvas}->reload_scene if $self->{canvas};
        $self->{canvas3D}->reload_scene if $self->{canvas3D};
    } else {
        $self->resume_background_process;
    }
}

# Called to update various buttons depending on whether there are any objects or
# whether background processing (export of a G-code, sending to Octoprint, forced background re-slicing) is active.
sub object_list_changed {
    my $self = shift;
        
    # Enable/disable buttons depending on whether there are any objects on the platter.
    my $have_objects = @{$self->{objects}} ? 1 : 0;
    my $variable_layer_height_allowed = $self->{config}->variable_layer_height && $self->{canvas3D}->layer_editing_allowed;
    if ($self->{htoolbar}) {
        # On OSX or Linux
        $self->{htoolbar}->EnableTool($_, $have_objects)
            for (TB_RESET, TB_ARRANGE, TB_LAYER_EDITING);
        $self->{htoolbar}->EnableTool(TB_LAYER_EDITING, 0) if (! $variable_layer_height_allowed);
    } else {
        # On MSW
        my $method = $have_objects ? 'Enable' : 'Disable';
        $self->{"btn_$_"}->$method
            for grep $self->{"btn_$_"}, qw(reset arrange reslice export_gcode export_stl print send_gcode layer_editing);
        $self->{"btn_layer_editing"}->Disable if (! $variable_layer_height_allowed);
    }

    my $export_in_progress = $self->{export_gcode_output_file} || $self->{send_gcode_file};
    my $method = ($have_objects && ! $export_in_progress) ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(reslice export_gcode print send_gcode);
}

# Selection of an active 3D object changed.
sub selection_changed {
    my ($self) = @_;
    my ($obj_idx, $object) = $self->selected_object;
    my $have_sel = defined $obj_idx;
    
    if ($self->{htoolbar}) {
        # On OSX or Linux
        $self->{htoolbar}->EnableTool($_, $have_sel)
            for (TB_REMOVE, TB_MORE, TB_FEWER, TB_45CW, TB_45CCW, TB_SCALE, TB_SPLIT, TB_CUT, TB_SETTINGS);
    } else {
        # On MSW
        my $method = $have_sel ? 'Enable' : 'Disable';
        $self->{"btn_$_"}->$method
            for grep $self->{"btn_$_"}, qw(remove increase decrease rotate45cw rotate45ccw changescale split cut settings);
    }
    
    if ($self->{object_info_size}) { # have we already loaded the info pane?
        if ($have_sel) {
            my $model_object = $self->{model}->objects->[$obj_idx];
            #FIXME print_info runs model fixing in two rounds, it is very slow, it should not be performed here!
            # $model_object->print_info;
            my $model_instance = $model_object->instances->[0];
            $self->{object_info_size}->SetLabel(sprintf("%.2f x %.2f x %.2f", @{$model_object->instance_bounding_box(0)->size}));
            $self->{object_info_materials}->SetLabel($model_object->materials_count);
            
            if (my $stats = $model_object->mesh_stats) {
                $self->{object_info_volume}->SetLabel(sprintf('%.2f', $stats->{volume} * ($model_instance->scaling_factor**3)));
                $self->{object_info_facets}->SetLabel(sprintf('%d (%d shells)', $model_object->facets_count, $stats->{number_of_parts}));
                if (my $errors = sum(@$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)})) {
                    $self->{object_info_manifold}->SetLabel(sprintf("Auto-repaired (%d errors)", $errors));
                    $self->{object_info_manifold_warning_icon}->Show;
                    
                    # we don't show normals_fixed because we never provide normals
	                # to admesh, so it generates normals for all facets
                    my $message = sprintf '%d degenerate facets, %d edges fixed, %d facets removed, %d facets added, %d facets reversed, %d backwards edges',
                        @$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)};
                    $self->{object_info_manifold}->SetToolTipString($message);
                    $self->{object_info_manifold_warning_icon}->SetToolTipString($message);
                } else {
                    $self->{object_info_manifold}->SetLabel("Yes");
                }
            } else {
                $self->{object_info_facets}->SetLabel($object->facets);
            }
        } else {
            $self->{"object_info_$_"}->SetLabel("") for qw(size volume facets materials manifold);
            $self->{object_info_manifold_warning_icon}->Hide;
            $self->{object_info_manifold}->SetToolTipString("");
        }
        $self->Layout;
    }
    
    # prepagate the event to the frame (a custom Wx event would be cleaner)
    $self->GetFrame->on_plater_selection_changed($have_sel);
}

sub select_object {
    my ($self, $obj_idx) = @_;
    
    $_->selected(0) for @{ $self->{objects} };
    if (defined $obj_idx) {
        $self->{objects}->[$obj_idx]->selected(1);
        # We use this flag to avoid circular event handling
        # Select() happens to fire a wxEVT_LIST_ITEM_SELECTED on Windows, 
        # whose event handler calls this method again and again and again
        $PreventListEvents = 1;
        $self->{list}->Select($obj_idx, 1);
        $PreventListEvents = 0;
    } else {
        # TODO: deselect all in list
    }
    $self->selection_changed(1);
}

sub selected_object {
    my ($self) = @_;
    my $obj_idx = first { $self->{objects}[$_]->selected } 0..$#{ $self->{objects} };
    return defined $obj_idx ? ($obj_idx, $self->{objects}[$obj_idx]) : undef;
}

sub statusbar {
    return $_[0]->GetFrame->{statusbar};
}

sub object_menu {
    my ($self) = @_;
    
    my $frame = $self->GetFrame;
    my $menu = Wx::Menu->new;
    my $accel = ($^O eq 'MSWin32') ? sub { $_[0] . "\t\xA0" . $_[1] } : sub { $_[0] };
    $frame->_append_menu_item($menu, $accel->('Delete', 'Del'), 'Remove the selected object', sub {
        $self->remove;
    }, undef, 'brick_delete.png');
    $frame->_append_menu_item($menu, $accel->('Increase copies', '+'), 'Place one more copy of the selected object', sub {
        $self->increase;
    }, undef, 'add.png');
    $frame->_append_menu_item($menu, $accel->('Decrease copies', '-'), 'Remove one copy of the selected object', sub {
        $self->decrease;
    }, undef, 'delete.png');
    $frame->_append_menu_item($menu, "Set number of copies…", 'Change the number of copies of the selected object', sub {
        $self->set_number_of_copies;
    }, undef, 'textfield.png');
    $menu->AppendSeparator();
    $frame->_append_menu_item($menu, $accel->('Rotate 45° clockwise', 'l'), 'Rotate the selected object by 45° clockwise', sub {
        $self->rotate(-45, Z, 'relative');
    }, undef, 'arrow_rotate_clockwise.png');
    $frame->_append_menu_item($menu, $accel->('Rotate 45° counter-clockwise', 'r'), 'Rotate the selected object by 45° counter-clockwise', sub {
        $self->rotate(+45, Z, 'relative');
    }, undef, 'arrow_rotate_anticlockwise.png');
    
    my $rotateMenu = Wx::Menu->new;
    my $rotateMenuItem = $menu->AppendSubMenu($rotateMenu, "Rotate", 'Rotate the selected object by an arbitrary angle');
    $frame->_set_menu_item_icon($rotateMenuItem, 'textfield.png');
    $frame->_append_menu_item($rotateMenu, "Around X axis…", 'Rotate the selected object by an arbitrary angle around X axis', sub {
        $self->rotate(undef, X);
    }, undef, 'bullet_red.png');
    $frame->_append_menu_item($rotateMenu, "Around Y axis…", 'Rotate the selected object by an arbitrary angle around Y axis', sub {
        $self->rotate(undef, Y);
    }, undef, 'bullet_green.png');
    $frame->_append_menu_item($rotateMenu, "Around Z axis…", 'Rotate the selected object by an arbitrary angle around Z axis', sub {
        $self->rotate(undef, Z);
    }, undef, 'bullet_blue.png');
    
    my $mirrorMenu = Wx::Menu->new;
    my $mirrorMenuItem = $menu->AppendSubMenu($mirrorMenu, "Mirror", 'Mirror the selected object');
    $frame->_set_menu_item_icon($mirrorMenuItem, 'shape_flip_horizontal.png');
    $frame->_append_menu_item($mirrorMenu, "Along X axis…", 'Mirror the selected object along the X axis', sub {
        $self->mirror(X);
    }, undef, 'bullet_red.png');
    $frame->_append_menu_item($mirrorMenu, "Along Y axis…", 'Mirror the selected object along the Y axis', sub {
        $self->mirror(Y);
    }, undef, 'bullet_green.png');
    $frame->_append_menu_item($mirrorMenu, "Along Z axis…", 'Mirror the selected object along the Z axis', sub {
        $self->mirror(Z);
    }, undef, 'bullet_blue.png');
    
    my $scaleMenu = Wx::Menu->new;
    my $scaleMenuItem = $menu->AppendSubMenu($scaleMenu, "Scale", 'Scale the selected object along a single axis');
    $frame->_set_menu_item_icon($scaleMenuItem, 'arrow_out.png');
    $frame->_append_menu_item($scaleMenu, $accel->('Uniformly…', 's'), 'Scale the selected object along the XYZ axes', sub {
        $self->changescale(undef);
    });
    $frame->_append_menu_item($scaleMenu, "Along X axis…", 'Scale the selected object along the X axis', sub {
        $self->changescale(X);
    }, undef, 'bullet_red.png');
    $frame->_append_menu_item($scaleMenu, "Along Y axis…", 'Scale the selected object along the Y axis', sub {
        $self->changescale(Y);
    }, undef, 'bullet_green.png');
    $frame->_append_menu_item($scaleMenu, "Along Z axis…", 'Scale the selected object along the Z axis', sub {
        $self->changescale(Z);
    }, undef, 'bullet_blue.png');
    
    my $scaleToSizeMenu = Wx::Menu->new;
    my $scaleToSizeMenuItem = $menu->AppendSubMenu($scaleToSizeMenu, "Scale to size", 'Scale the selected object along a single axis');
    $frame->_set_menu_item_icon($scaleToSizeMenuItem, 'arrow_out.png');
    $frame->_append_menu_item($scaleToSizeMenu, "Uniformly…", 'Scale the selected object along the XYZ axes', sub {
        $self->changescale(undef, 1);
    });
    $frame->_append_menu_item($scaleToSizeMenu, "Along X axis…", 'Scale the selected object along the X axis', sub {
        $self->changescale(X, 1);
    }, undef, 'bullet_red.png');
    $frame->_append_menu_item($scaleToSizeMenu, "Along Y axis…", 'Scale the selected object along the Y axis', sub {
        $self->changescale(Y, 1);
    }, undef, 'bullet_green.png');
    $frame->_append_menu_item($scaleToSizeMenu, "Along Z axis…", 'Scale the selected object along the Z axis', sub {
        $self->changescale(Z, 1);
    }, undef, 'bullet_blue.png');
    
    $frame->_append_menu_item($menu, "Split", 'Split the selected object into individual parts', sub {
        $self->split_object;
    }, undef, 'shape_ungroup.png');
    $frame->_append_menu_item($menu, "Cut…", 'Open the 3D cutting tool', sub {
        $self->object_cut_dialog;
    }, undef, 'package.png');
    $menu->AppendSeparator();
    $frame->_append_menu_item($menu, "Settings…", 'Open the object editor dialog', sub {
        $self->object_settings_dialog;
    }, undef, 'cog.png');
    $menu->AppendSeparator();
    $frame->_append_menu_item($menu, "Reload from Disk", 'Reload the selected file from Disk', sub {
        $self->reload_from_disk;
    }, undef, 'arrow_refresh.png');
    $frame->_append_menu_item($menu, "Export object as STL…", 'Export this single object as STL file', sub {
        $self->export_object_stl;
    }, undef, 'brick_go.png');
    
    return $menu;
}

# Set a camera direction, zoom to all objects.
sub select_view {
    my ($self, $direction) = @_;
    my $idx_page = $self->{preview_notebook}->GetSelection;
    my $page = ($idx_page == &Wx::wxNOT_FOUND) ? '3D' : $self->{preview_notebook}->GetPageText($idx_page);
    if ($page eq 'Preview') {
        $self->{preview3D}->canvas->select_view($direction);
        $self->{canvas3D}->set_viewport_from_scene($self->{preview3D}->canvas);
    } else {
        $self->{canvas3D}->select_view($direction);
        $self->{preview3D}->canvas->set_viewport_from_scene($self->{canvas3D});
    }
}

package Slic3r::GUI::Plater::DropTarget;
use Wx::DND;
use base 'Wx::FileDropTarget';

sub new {
    my ($class, $window) = @_;
    my $self = $class->SUPER::new;
    $self->{window} = $window;
    return $self;
}

sub OnDropFiles {
    my ($self, $x, $y, $filenames) = @_;    
    # stop scalars leaking on older perl
    # https://rt.perl.org/rt3/Public/Bug/Display.html?id=70602
    @_ = ();
    # only accept STL, OBJ, AMF, 3MF and PRUSA files
    return 0 if grep !/\.(?:[sS][tT][lL]|[oO][bB][jJ]|[aA][mM][fF]|[3][mM][fF]|[aA][mM][fF].[xX][mM][lL]|[zZ][iI][pP].[aA][mM][lL]|[pP][rR][uU][sS][aA])$/, @$filenames;
    $self->{window}->load_files($filenames);
}

# 2D preview of an object. Each object is previewed by its convex hull.
package Slic3r::GUI::Plater::Object;
use Moo;

has 'name'                  => (is => 'rw', required => 1);
has 'thumbnail'             => (is => 'rw'); # ExPolygon::Collection in scaled model units with no transforms
has 'transformed_thumbnail' => (is => 'rw');
has 'instance_thumbnails'   => (is => 'ro', default => sub { [] });  # array of ExPolygon::Collection objects, each one representing the actual placed thumbnail of each instance in pixel units
has 'selected'              => (is => 'rw', default => sub { 0 });

sub make_thumbnail {
    my ($self, $model, $obj_idx) = @_;
    # make method idempotent
    $self->thumbnail->clear;
    # raw_mesh is the non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
    my $mesh = $model->objects->[$obj_idx]->raw_mesh;
#FIXME The "correct" variant could be extremely slow.
#    if ($mesh->facets_count <= 5000) {
#        # remove polygons with area <= 1mm
#        my $area_threshold = Slic3r::Geometry::scale 1;
#        $self->thumbnail->append(
#            grep $_->area >= $area_threshold,
#            @{ $mesh->horizontal_projection },   # horizontal_projection returns scaled expolygons
#        );
#        $self->thumbnail->simplify(0.5);
#    } else {
        my $convex_hull = Slic3r::ExPolygon->new($mesh->convex_hull);
        $self->thumbnail->append($convex_hull);
#    }
    return $self->thumbnail;
}

sub transform_thumbnail {
    my ($self, $model, $obj_idx) = @_;
    
    return unless defined $self->thumbnail;
    
    my $model_object = $model->objects->[$obj_idx];
    my $model_instance = $model_object->instances->[0];
    
    # the order of these transformations MUST be the same everywhere, including
    # in Slic3r::Print->add_model_object()
    my $t = $self->thumbnail->clone;
    $t->rotate($model_instance->rotation, Slic3r::Point->new(0,0));
    $t->scale($model_instance->scaling_factor);
    
    $self->transformed_thumbnail($t);
}

1;
