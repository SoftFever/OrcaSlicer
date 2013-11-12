package Slic3r::GUI::Plater;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use List::Util qw(max sum first);
use Slic3r::Geometry::Clipper qw(offset JT_ROUND);
use Math::ConvexHull::MonotoneChain qw(convex_hull);
use Slic3r::Geometry qw(X Y Z MIN MAX);
use threads::shared qw(shared_clone);
use Wx qw(:bitmap :brush :button :cursor :dialog :filedialog :font :keycode :icon :id :listctrl :misc :panel :pen :sizer :toolbar :window);
use Wx::Event qw(EVT_BUTTON EVT_COMMAND EVT_KEY_DOWN EVT_LIST_ITEM_ACTIVATED EVT_LIST_ITEM_DESELECTED EVT_LIST_ITEM_SELECTED EVT_MOUSE_EVENTS EVT_PAINT EVT_TOOL EVT_CHOICE);
use base 'Wx::Panel';

use constant TB_LOAD            => &Wx::NewId;
use constant TB_REMOVE          => &Wx::NewId;
use constant TB_RESET           => &Wx::NewId;
use constant TB_ARRANGE         => &Wx::NewId;
use constant TB_EXPORT_GCODE    => &Wx::NewId;
use constant TB_EXPORT_STL      => &Wx::NewId;
use constant TB_MORE    => &Wx::NewId;
use constant TB_FEWER   => &Wx::NewId;
use constant TB_45CW    => &Wx::NewId;
use constant TB_45CCW   => &Wx::NewId;
use constant TB_ROTATE  => &Wx::NewId;
use constant TB_SCALE   => &Wx::NewId;
use constant TB_SPLIT   => &Wx::NewId;
use constant TB_VIEW    => &Wx::NewId;
use constant TB_SETTINGS => &Wx::NewId;

# package variables to avoid passing lexicals to threads
our $THUMBNAIL_DONE_EVENT    : shared = Wx::NewEventType;
our $PROGRESS_BAR_EVENT      : shared = Wx::NewEventType;
our $MESSAGE_DIALOG_EVENT    : shared = Wx::NewEventType;
our $EXPORT_COMPLETED_EVENT  : shared = Wx::NewEventType;
our $EXPORT_FAILED_EVENT     : shared = Wx::NewEventType;

use constant CANVAS_SIZE => [335,335];
use constant CANVAS_TEXT => join('-', +(localtime)[3,4]) eq '13-8'
    ? 'What do you want to print today? ™' # Sept. 13, 2006. The first part ever printed by a RepRap to make another RepRap.
    : 'Drag your objects here';
use constant FILAMENT_CHOOSERS_SPACING => 3;

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{config} = Slic3r::Config->new_from_defaults(qw(
        bed_size print_center complete_objects extruder_clearance_radius skirts skirt_distance
    ));
    $self->{objects} = [];
    $self->{selected_objects} = [];
    
    $self->{canvas} = Wx::Panel->new($self, -1, wxDefaultPosition, CANVAS_SIZE, wxTAB_TRAVERSAL);
    $self->{canvas}->SetBackgroundColour(Wx::wxWHITE);
    EVT_PAINT($self->{canvas}, \&repaint);
    EVT_MOUSE_EVENTS($self->{canvas}, \&mouse_event);
    
    $self->{objects_brush} = Wx::Brush->new(Wx::Colour->new(210,210,210), wxSOLID);
    $self->{selected_brush} = Wx::Brush->new(Wx::Colour->new(255,128,128), wxSOLID);
    $self->{dragged_brush} = Wx::Brush->new(Wx::Colour->new(128,128,255), wxSOLID);
    $self->{transparent_brush} = Wx::Brush->new(Wx::Colour->new(0,0,0), wxTRANSPARENT);
    $self->{grid_pen} = Wx::Pen->new(Wx::Colour->new(230,230,230), 1, wxSOLID);
    $self->{print_center_pen} = Wx::Pen->new(Wx::Colour->new(200,200,200), 1, wxSOLID);
    $self->{clearance_pen} = Wx::Pen->new(Wx::Colour->new(0,0,200), 1, wxSOLID);
    $self->{skirt_pen} = Wx::Pen->new(Wx::Colour->new(150,150,150), 1, wxSOLID);
    
    # toolbar for object manipulation
    if (!&Wx::wxMSW) {
        Wx::ToolTip::Enable(1);
        $self->{htoolbar} = Wx::ToolBar->new($self, -1, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_TEXT | wxBORDER_SIMPLE | wxTAB_TRAVERSAL);
        $self->{htoolbar}->AddTool(TB_LOAD, "Add…", Wx::Bitmap->new("$Slic3r::var/brick_add.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_REMOVE, "Delete", Wx::Bitmap->new("$Slic3r::var/brick_delete.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_RESET, "Delete All", Wx::Bitmap->new("$Slic3r::var/cross.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_ARRANGE, "Arrange", Wx::Bitmap->new("$Slic3r::var/bricks.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_MORE, "More", Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_FEWER, "Fewer", Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_45CCW, "45° ccw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_anticlockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_45CW, "45° cw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_ROTATE, "Rotate…", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SCALE, "Scale…", Wx::Bitmap->new("$Slic3r::var/arrow_out.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SPLIT, "Split", Wx::Bitmap->new("$Slic3r::var/shape_ungroup.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_VIEW, "View", Wx::Bitmap->new("$Slic3r::var/package.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_SETTINGS, "Settings…", Wx::Bitmap->new("$Slic3r::var/cog.png", wxBITMAP_TYPE_PNG), '');
    } else {
        my %tbar_buttons = (
            load            => "Add…",
            remove          => "Delete",
            reset           => "Delete All",
            arrange         => "Arrange",
            increase        => "",
            decrease        => "",
            rotate45ccw     => "",
            rotate45cw      => "",
            rotate          => "Rotate…",
            changescale     => "Scale…",
            split           => "Split",
            view            => "View",
            settings        => "Settings…",
        );
        $self->{btoolbar} = Wx::BoxSizer->new(wxHORIZONTAL);
        for (qw(load remove reset arrange increase decrease rotate45ccw rotate45cw rotate changescale split view settings)) {
            $self->{"btn_$_"} = Wx::Button->new($self, -1, $tbar_buttons{$_}, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            $self->{btoolbar}->Add($self->{"btn_$_"});
        }
    }

    $self->{list} = Wx::ListView->new($self, -1, wxDefaultPosition, wxDefaultSize, wxLC_SINGLE_SEL | wxLC_REPORT | wxBORDER_SUNKEN | wxTAB_TRAVERSAL | wxWANTS_CHARS);
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
    $self->{btn_export_gcode} = Wx::Button->new($self, -1, "Export G-code…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_stl} = Wx::Button->new($self, -1, "Export STL…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_gcode}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_export_stl}->SetFont($Slic3r::GUI::small_font);
    
    if ($Slic3r::GUI::have_button_icons) {
        my %icons = qw(
            load            brick_add.png
            remove          brick_delete.png
            reset           cross.png
            arrange         bricks.png
            export_gcode    cog_go.png
            export_stl      brick_go.png
            
            increase        add.png
            decrease        delete.png
            rotate45cw      arrow_rotate_clockwise.png
            rotate45ccw     arrow_rotate_anticlockwise.png
            rotate          arrow_rotate_clockwise.png
            changescale     arrow_out.png
            split           shape_ungroup.png
            view            package.png
            settings        cog.png
        );
        for (grep $self->{"btn_$_"}, keys %icons) {
            $self->{"btn_$_"}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/$icons{$_}", wxBITMAP_TYPE_PNG));
        }
    }
    $self->selection_changed(0);
    $self->object_list_changed;
    EVT_BUTTON($self, $self->{btn_export_gcode}, \&export_gcode);
    EVT_BUTTON($self, $self->{btn_export_stl}, \&export_stl);
    
    if ($self->{htoolbar}) {
        EVT_TOOL($self, TB_LOAD, \&load);
        EVT_TOOL($self, TB_REMOVE, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_TOOL($self, TB_RESET, \&reset);
        EVT_TOOL($self, TB_ARRANGE, \&arrange);
        EVT_TOOL($self, TB_MORE, \&increase);
        EVT_TOOL($self, TB_FEWER, \&decrease);
        EVT_TOOL($self, TB_45CW, sub { $_[0]->rotate(-45) });
        EVT_TOOL($self, TB_45CCW, sub { $_[0]->rotate(45) });
        EVT_TOOL($self, TB_ROTATE, sub { $_[0]->rotate(undef) });
        EVT_TOOL($self, TB_SCALE, \&changescale);
        EVT_TOOL($self, TB_SPLIT, \&split_object);
        EVT_TOOL($self, TB_VIEW, sub { $_[0]->object_preview_dialog });
        EVT_TOOL($self, TB_SETTINGS, sub { $_[0]->object_settings_dialog });
    } else {
        EVT_BUTTON($self, $self->{btn_load}, \&load);
        EVT_BUTTON($self, $self->{btn_remove}, sub { $self->remove() }); # explicitly pass no argument to remove
        EVT_BUTTON($self, $self->{btn_reset}, \&reset);
        EVT_BUTTON($self, $self->{btn_arrange}, \&arrange);
        EVT_BUTTON($self, $self->{btn_increase}, \&increase);
        EVT_BUTTON($self, $self->{btn_decrease}, \&decrease);
        EVT_BUTTON($self, $self->{btn_rotate45cw}, sub { $_[0]->rotate(-45) });
        EVT_BUTTON($self, $self->{btn_rotate45ccw}, sub { $_[0]->rotate(45) });
        EVT_BUTTON($self, $self->{btn_changescale}, \&changescale);
        EVT_BUTTON($self, $self->{btn_rotate}, sub { $_[0]->rotate(undef) });
        EVT_BUTTON($self, $self->{btn_split}, \&split_object);
        EVT_BUTTON($self, $self->{btn_view}, sub { $_[0]->object_preview_dialog });
        EVT_BUTTON($self, $self->{btn_settings}, sub { $_[0]->object_settings_dialog });
    }
    
    $_->SetDropTarget(Slic3r::GUI::Plater::DropTarget->new($self))
        for $self, $self->{canvas}, $self->{list};
    
    EVT_COMMAND($self, -1, $THUMBNAIL_DONE_EVENT, sub {
        my ($self, $event) = @_;
        my ($obj_idx) = @{$event->GetData};
        return if !$self->{objects}[$obj_idx];  # object was deleted before thumbnail generation completed
        
        $self->on_thumbnail_made($obj_idx);
    });
    
    EVT_COMMAND($self, -1, $PROGRESS_BAR_EVENT, sub {
        my ($self, $event) = @_;
        my ($percent, $message) = @{$event->GetData};
        $self->statusbar->SetProgress($percent);
        $self->statusbar->SetStatusText("$message…");
    });
    
    EVT_COMMAND($self, -1, $MESSAGE_DIALOG_EVENT, sub {
        my ($self, $event) = @_;
        Wx::MessageDialog->new($self, @{$event->GetData})->ShowModal;
    });
    
    EVT_COMMAND($self, -1, $EXPORT_COMPLETED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_export_completed(@{$event->GetData});
    });
    
    EVT_COMMAND($self, -1, $EXPORT_FAILED_EVENT, sub {
        my ($self, $event) = @_;
        $self->on_export_failed;
    });
    
    $self->_update_bed_size;
    $self->recenter;
    
    {
        my $presets;
        if ($self->skeinpanel->{mode} eq 'expert') {
            $presets = Wx::BoxSizer->new(wxVERTICAL);
            my %group_labels = (
                print       => 'Print settings',
                filament    => 'Filament',
                printer     => 'Printer',
            );
            $self->{preset_choosers} = {};
            $self->{preset_choosers_sizers} = {};
            for my $group (qw(print filament printer)) {
                my $text = Wx::StaticText->new($self, -1, "$group_labels{$group}:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
                $text->SetFont($Slic3r::GUI::small_font);
                my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, [140, -1], []);
                $choice->SetFont($Slic3r::GUI::small_font);
                $self->{preset_choosers}{$group} = [$choice];
                EVT_CHOICE($choice, $choice, sub { $self->on_select_preset($group, @_) });
                
                $self->{preset_choosers_sizers}{$group} = Wx::BoxSizer->new(wxVERTICAL);
                $self->{preset_choosers_sizers}{$group}->Add($choice, 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
                
                $presets->Add($text, 0, wxALIGN_LEFT | wxRIGHT, 4);
                $presets->Add($self->{preset_choosers_sizers}{$group}, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 8);
            }
        }
        
        my $object_info_sizer;
        {
            my $box = Wx::StaticBox->new($self, -1, "Info");
            $object_info_sizer = Wx::StaticBoxSizer->new($box, wxVERTICAL);
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
                    $self->{object_info_manifold_warning_icon} = Wx::StaticBitmap->new($self, -1, Wx::Bitmap->new("$Slic3r::var/error.png", wxBITMAP_TYPE_PNG));
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
        
        my $right_buttons_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $right_buttons_sizer->Add($presets, 0, wxEXPAND, 0) if defined $presets;
        $right_buttons_sizer->Add($self->{btn_export_gcode}, 0, wxEXPAND | wxTOP, 8);
        $right_buttons_sizer->Add($self->{btn_export_stl}, 0, wxEXPAND | wxTOP, 2);
        
        my $right_top_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $right_top_sizer->Add($self->{list}, 1, wxEXPAND | wxLEFT, 5);
        $right_top_sizer->Add($right_buttons_sizer, 0, wxEXPAND | wxALL, 10);
        
        my $right_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $right_sizer->Add($right_top_sizer, 1, wxEXPAND | wxBOTTOM, 10);
        $right_sizer->Add($object_info_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $hsizer->Add($self->{canvas}, 0, wxTOP, 1);
        $hsizer->Add($right_sizer, 1, wxEXPAND | wxBOTTOM, 0);
        
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($self->{htoolbar}, 0, wxEXPAND, 0) if $self->{htoolbar};
        $sizer->Add($self->{btoolbar}, 0, wxEXPAND, 0) if $self->{btoolbar};
        $sizer->Add($hsizer, 1, wxEXPAND, 0);
        
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
    }
    return $self;
}

sub on_select_preset {
	my $self = shift;
	my ($group, $choice) = @_;
	
	if ($group eq 'filament' && @{$self->{preset_choosers}{filament}} > 1) {
		my @filament_presets = $self->filament_presets;
		$Slic3r::GUI::Settings->{presets}{filament} = $choice->GetString($filament_presets[0]) . ".ini";
		$Slic3r::GUI::Settings->{presets}{"filament_${_}"} = $choice->GetString($filament_presets[$_])
			for 1 .. $#filament_presets;
		Slic3r::GUI->save_settings;
		return;
	}
	$self->skeinpanel->{options_tabs}{$group}->select_preset($choice->GetSelection);
}

sub skeinpanel {
    my $self = shift;
    return $self->GetParent->GetParent;
}

sub update_presets {
    my $self = shift;
    my ($group, $items, $selected) = @_;
    
    foreach my $choice (@{ $self->{preset_choosers}{$group} }) {
        my $sel = $choice->GetSelection;
        $choice->Clear;
        $choice->Append($_) for @$items;
        $choice->SetSelection($sel) if $sel <= $#$items;
    }
    $self->{preset_choosers}{$group}[0]->SetSelection($selected);
}

sub filament_presets {
    my $self = shift;
    
    return map $_->GetSelection, @{ $self->{preset_choosers}{filament} };
}

sub load {
    my $self = shift;
    
    my $dir = $Slic3r::GUI::Settings->{recent}{skein_directory} || $Slic3r::GUI::Settings->{recent}{config_directory} || '';
    my $dialog = Wx::FileDialog->new($self, 'Choose one or more files (STL/OBJ/AMF):', $dir, "", &Slic3r::GUI::SkeinPanel::MODEL_WILDCARD, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if ($dialog->ShowModal != wxID_OK) {
        $dialog->Destroy;
        return;
    }
    my @input_files = $dialog->GetPaths;
    $dialog->Destroy;
    $self->load_file($_) for @input_files;
}

sub load_file {
    my $self = shift;
    my ($input_file) = @_;
    
    my $basename = basename($input_file);
    $Slic3r::GUI::Settings->{recent}{skein_directory} = dirname($input_file);
    Slic3r::GUI->save_settings;
    
    my $process_dialog = Wx::ProgressDialog->new('Loading…', "Processing input file…", 100, $self, 0);
    $process_dialog->Pulse;
    
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    my $model = Slic3r::Model->read_from_file($input_file);
    for my $i (0 .. $#{$model->objects}) {
        my $object = Slic3r::GUI::Plater::Object->new(
            name                    => $basename,
            input_file              => $input_file,
            input_file_object_id    => $i,
            model                   => $model,
            model_object_idx        => $i,
            mesh_stats              => $model->objects->[$i]->mesh_stats,  # so that we can free model
            instances               => [
                $model->objects->[$i]->instances
                    ? (map $_->offset, @{$model->objects->[$i]->instances})
                    : [0,0],
            ],
        );
        
        # we only consider the rotation of the first instance for now
        $object->rotate($model->objects->[$i]->instances->[0]->rotation)
            if $model->objects->[$i]->instances;
        
        push @{ $self->{objects} }, $object;
        $self->object_loaded($#{ $self->{objects} }, no_arrange => (@{$object->instances} > 1));
    }
    
    $process_dialog->Destroy;
    $self->statusbar->SetStatusText("Loaded $basename");
}

sub object_loaded {
    my $self = shift;
    my ($obj_idx, %params) = @_;
    
    my $object = $self->{objects}[$obj_idx];
    $self->{list}->InsertStringItem($obj_idx, $object->name);
    $self->{list}->SetItemFont($obj_idx, Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL))
        if $self->{list}->can('SetItemFont');  # legacy code for wxPerl < 0.9918 not supporting SetItemFont()
    
    $self->{list}->SetItem($obj_idx, 1, $object->instances_count);
    $self->{list}->SetItem($obj_idx, 2, ($object->scale * 100) . "%");
    
    $self->make_thumbnail($obj_idx);
    $self->arrange unless $params{no_arrange};
    $self->{list}->Update;
    $self->{list}->Select($obj_idx, 1);
    $self->object_list_changed;
}

sub remove {
    my $self = shift;
    my ($obj_idx) = @_;
    
    # if no object index is supplied, remove the selected one
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    splice @{$self->{objects}}, $obj_idx, 1;
    $self->{list}->DeleteItem($obj_idx);
    
    $self->{selected_objects} = [];
    $self->selection_changed(0);
    $self->object_list_changed;
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub reset {
    my $self = shift;
    
    @{$self->{objects}} = ();
    $self->{list}->DeleteAllItems;
    
    $self->{selected_objects} = [];
    $self->selection_changed(0);
    $self->object_list_changed;
    $self->{canvas}->Refresh;
}

sub increase {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    my $instances = $object->instances;
    push @$instances, [ $instances->[-1]->[X] + 10, $instances->[-1]->[Y] + 10 ];
    $self->{list}->SetItem($obj_idx, 1, $object->instances_count);
    $self->arrange;
}

sub decrease {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    if ($object->instances_count >= 2) {
        pop @{$object->instances};
        $self->{list}->SetItem($obj_idx, 1, $object->instances_count);
    } else {
        $self->remove;
    }
    
    if ($self->{objects}[$obj_idx]) {
        $self->{list}->Select($obj_idx, 0);
        $self->{list}->Select($obj_idx, 1);
    }
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub rotate {
    my $self = shift;
    my ($angle) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    
    # we need thumbnail to be computed before allowing rotation
    return if !$object->thumbnail;
    
    if (!defined $angle) {
        $angle = Wx::GetNumberFromUser("", "Enter the rotation angle:", "Rotate", $object->rotate, -364, 364, $self);
        return if !$angle || $angle == -1;
        $angle = 0 - $angle;  # rotate clockwise (be consistent with button icon)
    }
    
    $object->rotate($object->rotate + $angle);
    $self->selection_changed(1);  # refresh info (size etc.)
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub changescale {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    
    # we need thumbnail to be computed before allowing scaling
    return if !$object->thumbnail;
    
    # max scale factor should be above 2540 to allow importing files exported in inches
    my $scale = Wx::GetNumberFromUser("", "Enter the scale % for the selected object:", "Scale", $object->scale*100, 0, 100000, $self);
    return if !$scale || $scale == -1;
    
    $self->{list}->SetItem($obj_idx, 2, "$scale%");
    $object->changescale($scale / 100);
    $self->selection_changed(1);  # refresh info (size, volume etc.)
    $self->arrange;
}

sub arrange {
    my $self = shift;
    
    my $total_parts = sum(map $_->instances_count, @{$self->{objects}}) or return;
    my @size = ();
    for my $a (X,Y) {
        $size[$a] = max(map $_->transformed_size->[$a], @{$self->{objects}});
    }
    
    eval {
        my $config = $self->skeinpanel->config;
        my @positions = Slic3r::Geometry::arrange
            ($total_parts, @size, @{$config->bed_size}, $config->min_object_distance, $self->skeinpanel->config);
        
        @$_ = @{shift @positions}
            for map @{$_->instances}, @{$self->{objects}};
    };
    # ignore arrange warnings on purpose
    
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub split_object {
    my $self = shift;
    
    my ($obj_idx, $current_object) = $self->selected_object;
    my $current_copies_num = $current_object->instances_count;
    my $model_object = $current_object->get_model_object;
    
    if (@{$model_object->volumes} > 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be split because it contains more than one volume/material.");
        return;
    }
    
    my $mesh = $model_object->mesh;
    $mesh->align_to_origin;
    
    $mesh->repair;
    my @new_meshes = @{$mesh->split};
    if (@new_meshes == 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be split because it already contains a single part.");
        return;
    }
    
    # remove the original object before spawning the object_loaded event, otherwise 
    # we'll pass the wrong $obj_idx to it (which won't be recognized after the
    # thumbnail thread returns)
    $self->remove($obj_idx);
    
    # create a bogus Model object, we only need to instantiate the new Model::Object objects
    my $new_model = Slic3r::Model->new;
    
    foreach my $mesh (@new_meshes) {
        $mesh->repair;
        my $bb = $mesh->bounding_box;
        my $model_object = $new_model->add_object;
        $model_object->add_volume(mesh => $mesh);
        my $object = Slic3r::GUI::Plater::Object->new(
            name                    => basename($current_object->input_file),
            input_file              => $current_object->input_file,
            input_file_object_id    => undef,
            model                   => $new_model,
            model_object_idx        => $#{$new_model->objects},
            instances               => [ map $bb->min_point, 1..$current_copies_num ],
        );
        push @{ $self->{objects} }, $object;
        $self->object_loaded($#{ $self->{objects} }, no_arrange => 1);
    }
    
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub export_gcode {
    my $self = shift;
    
    if ($self->{export_thread}) {
        Wx::MessageDialog->new($self, "Another slicing job is currently running.", 'Error', wxOK | wxICON_ERROR)->ShowModal;
        return;
    }
    
    # get config before spawning the thread because it needs GetParent and it's not available there
    our $config          = $self->skeinpanel->config;
    our $extra_variables = $self->skeinpanel->extra_variables;
    
    # select output file
    $self->{output_file} = $main::opt{output};
    {
        $self->{output_file} = $self->skeinpanel->init_print->expanded_output_filepath($self->{output_file}, $self->{objects}[0]->input_file);
        my $dlg = Wx::FileDialog->new($self, 'Save G-code file as:', Slic3r::GUI->output_path(dirname($self->{output_file})),
            basename($self->{output_file}), &Slic3r::GUI::SkeinPanel::FILE_WILDCARDS->{gcode}, wxFD_SAVE);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return;
        }
        $Slic3r::GUI::Settings->{_}{last_output_path} = dirname($dlg->GetPath);
        Slic3r::GUI->save_settings;
        $self->{output_file} = $Slic3r::GUI::SkeinPanel::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    $self->statusbar->StartBusy;
    
    $_->free_model_object for @{$self->{objects}};
    
    # It looks like declaring a local $SIG{__WARN__} prevents the ugly
    # "Attempt to free unreferenced scalar" warning...
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    
    if ($Slic3r::have_threads) {
        @_ = ();
        
        # some perls (including 5.14.2) crash on threads->exit if we pass lexicals to the thread
        our $_thread_self = $self;
        
        $self->{export_thread} = threads->create(sub {
            $_thread_self->export_gcode2(
                $config,
                $extra_variables,
                $_thread_self->{output_file},
                progressbar     => sub { Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $PROGRESS_BAR_EVENT, shared_clone([@_]))) },
                message_dialog  => sub { Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $MESSAGE_DIALOG_EVENT, shared_clone([@_]))) },
                on_completed    => sub { Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $EXPORT_COMPLETED_EVENT, shared_clone([@_]))) },
                catch_error     => sub {
                    Slic3r::GUI::catch_error($_thread_self, $_[0], sub {
                        Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $MESSAGE_DIALOG_EVENT, shared_clone([@_])));
                        Wx::PostEvent($_thread_self, Wx::PlThreadEvent->new(-1, $EXPORT_FAILED_EVENT, undef));
                    });
                },
            );
            Slic3r::thread_cleanup();
        });
        $self->statusbar->SetCancelCallback(sub {
            $self->{export_thread}->kill('KILL')->join;
            $self->{export_thread} = undef;
            $self->statusbar->StopBusy;
            $self->statusbar->SetStatusText("Export cancelled");
        });
    } else {
        $self->export_gcode2(
            $config,
            $extra_variables,
            $self->{output_file},
            progressbar => sub {
                my ($percent, $message) = @_;
                $self->statusbar->SetProgress($percent);
                $self->statusbar->SetStatusText("$message…");
            },
            message_dialog => sub { Wx::MessageDialog->new($self, @_)->ShowModal },
            on_completed => sub { $self->on_export_completed(@_) },
            catch_error => sub { Slic3r::GUI::catch_error($self, @_) && $self->on_export_failed },
        );
    }
}

sub export_gcode2 {
    my $self = shift;
    my ($config, $extra_variables, $output_file, %params) = @_;
    local $SIG{'KILL'} = sub {
        Slic3r::debugf "Exporting cancelled; exiting thread...\n";
        Slic3r::thread_cleanup();
        threads->exit();
    } if $Slic3r::have_threads;
    
    my $print = Slic3r::Print->new(
        config          => $config,
        extra_variables => $extra_variables,
    );
    
    eval {
        $print->config->validate;
        $print->add_model($self->make_model);
        $print->validate;
        
        {
            my @warnings = ();
            local $SIG{__WARN__} = sub { push @warnings, $_[0] };
            my %params = (
                output_file => $output_file,
                status_cb   => sub { $params{progressbar}->(@_) },
                quiet       => 1,
            );
            if ($params{export_svg}) {
                $print->export_svg(%params);
            } else {
                $print->export_gcode(%params);
            }
            Slic3r::GUI::warning_catcher($self, $Slic3r::have_threads ? sub {
                Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $MESSAGE_DIALOG_EVENT, shared_clone([@_])));
            } : undef)->($_) for @warnings;
        }
        
        my $message = "Your files were successfully sliced";
        if ($print->processing_time) {
            $message .= ' in';
            my $minutes = int($print->processing_time/60);
            $message .= sprintf " %d minutes and", $minutes if $minutes;
            $message .= sprintf " %.1f seconds", $print->processing_time - $minutes*60;
        }
        $message .= ".";
        $params{on_completed}->($message);
    };
    $params{catch_error}->();
}

sub on_export_completed {
    my $self = shift;
    my ($message) = @_;
    
    $self->{export_thread}->detach if $self->{export_thread};
    $self->{export_thread} = undef;
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("G-code file exported to $self->{output_file}");
    &Wx::wxTheApp->notify($message);
}

sub on_export_failed {
    my $self = shift;
    
    $self->{export_thread}->detach if $self->{export_thread};
    $self->{export_thread} = undef;
    $self->statusbar->SetCancelCallback(undef);
    $self->statusbar->StopBusy;
    $self->statusbar->SetStatusText("Export failed");
}

sub export_stl {
    my $self = shift;
        
    my $output_file = $self->_get_export_file('STL') or return;
    Slic3r::Format::STL->write_file($output_file, $self->make_model, binary => 1);
    $self->statusbar->SetStatusText("STL file exported to $output_file");
}

sub export_amf {
    my $self = shift;
        
    my $output_file = $self->_get_export_file('AMF') or return;
    Slic3r::Format::AMF->write_file($output_file, $self->make_model);
    $self->statusbar->SetStatusText("AMF file exported to $output_file");
}

sub _get_export_file {
    my $self = shift;
    my ($format) = @_;
    
    my $suffix = $format eq 'STL' ? '.stl' : '.amf.xml';
    
    my $output_file = $main::opt{output};
    {
        $output_file = $self->skeinpanel->init_print->expanded_output_filepath($output_file, $self->{objects}[0]->input_file);
        $output_file =~ s/\.gcode$/$suffix/i;
        my $dlg = Wx::FileDialog->new($self, "Save $format file as:", dirname($output_file),
            basename($output_file), &Slic3r::GUI::SkeinPanel::MODEL_WILDCARD, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return undef;
        }
        $output_file = $Slic3r::GUI::SkeinPanel::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    return $output_file;
}

sub make_model {
    my $self = shift;
    
    my $model = Slic3r::Model->new;
    foreach my $plater_object (@{$self->{objects}}) {
        my $model_object = $plater_object->get_model_object;
        
        my $new_model_object = $model->add_object(
            input_file  => $plater_object->input_file,
            config      => $plater_object->config,
            layer_height_ranges => $plater_object->layer_height_ranges,
            material_mapping => $plater_object->material_mapping,
        );
        foreach my $volume (@{$model_object->volumes}) {
            $new_model_object->add_volume(
                material_id => $volume->material_id,
                mesh        => $volume->mesh,
            );
            $model->set_material($volume->material_id || 0, {});
        }
        $new_model_object->align_to_origin;
        $new_model_object->add_instance(
            rotation    => $plater_object->rotate,  # around center point
            scaling_factor => $plater_object->scale,
            offset      => Slic3r::Point->new(@$_),
        ) for @{$plater_object->instances};
    }
    
    $model->align_to_origin;
    return $model;
}

sub make_thumbnail {
    my $self = shift;
    my ($obj_idx) = @_;
    
    my $object = $self->{objects}[$obj_idx];
    $object->thumbnail_scaling_factor($self->{scaling_factor});
    $object->thumbnail(Slic3r::ExPolygon::Collection->new);
    my $cb = sub {
        $object->make_thumbnail;
        
        if ($Slic3r::have_threads) {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $THUMBNAIL_DONE_EVENT, shared_clone([ $obj_idx ])));
            Slic3r::thread_cleanup();
            threads->exit;
        } else {
            $self->on_thumbnail_made($obj_idx);
        }
    };
    
    @_ = ();
    $Slic3r::have_threads ? threads->create($cb)->detach : $cb->();
}

sub on_thumbnail_made {
    my $self = shift;
    my ($obj_idx) = @_;
    
    $self->{objects}[$obj_idx]->_transform_thumbnail;
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub recenter {
    my $self = shift;
    
    return unless @{$self->{objects}};
    
    # calculate displacement needed to center the print
    my $print_bb = Slic3r::Geometry::BoundingBox->new_from_points([
        map {
            my $obj = $_;
            my $bb = $obj->transformed_bounding_box;
            my @points = ($bb->min_point, $bb->max_point);
            map Slic3r::Geometry::move_points($_, @points), @{$obj->instances};
        } @{$self->{objects}},
    ]);
    
    # $self->{shift} contains the offset in pixels to add to object instances in order to center them
    # it is expressed in upwards Y
    my $print_size = $print_bb->size;
    $self->{shift} = [
        $self->to_pixel(-$print_bb->x_min) + ($self->{canvas}->GetSize->GetWidth  - $self->to_pixel($print_size->[X])) / 2,
        $self->to_pixel(-$print_bb->y_min) + ($self->{canvas}->GetSize->GetHeight - $self->to_pixel($print_size->[Y])) / 2,
    ];
}

sub on_config_change {
    my $self = shift;
    my ($opt_key, $value) = @_;
    if ($opt_key eq 'extruders_count' && defined $value) {
        my $choices = $self->{preset_choosers}{filament};
        while (@$choices < $value) {
        	my @presets = $choices->[0]->GetStrings;
            push @$choices, Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], [@presets]);
            $choices->[-1]->SetFont($Slic3r::GUI::small_font);
            $self->{preset_choosers_sizers}{filament}->Add($choices->[-1], 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
            EVT_CHOICE($choices->[-1], $choices->[-1], sub { $self->on_select_preset('filament', @_) });
            my $i = first { $choices->[-1]->GetString($_) eq ($Slic3r::GUI::Settings->{presets}{"filament_" . $#$choices} || '') } 0 .. $#presets;
        	$choices->[-1]->SetSelection($i || 0);
        }
        while (@$choices > $value) {
            $self->{preset_choosers_sizers}{filament}->Remove(-1);
            $choices->[-1]->Destroy;
            pop @$choices;
        }
        $self->Layout;
    } elsif ($self->{config}->has($opt_key)) {
        $self->{config}->set($opt_key, $value);
        $self->_update_bed_size if $opt_key eq 'bed_size';
    }
}

sub _update_bed_size {
    my $self = shift;
    
    # supposing the preview canvas is square, calculate the scaling factor
    # to constrain print bed area inside preview
    # when the canvas is not rendered yet, its GetSize() method returns 0,0
    $self->{scaling_factor} = CANVAS_SIZE->[X] / max(@{ $self->{config}->bed_size });
    $_->thumbnail_scaling_factor($self->{scaling_factor}) for @{ $self->{objects} };
    $self->recenter;
}

# this is called on the canvas
sub repaint {
    my ($self, $event) = @_;
    my $parent = $self->GetParent;
    
    my $dc = Wx::PaintDC->new($self);
    my $size = $self->GetSize;
    my @size = ($size->GetWidth, $size->GetHeight);
    
    # draw grid
    $dc->SetPen($parent->{grid_pen});
    my $step = 10 * $parent->{scaling_factor};
    for (my $x = $step; $x <= $size[X]; $x += $step) {
        $dc->DrawLine($x, 0, $x, $size[Y]);
    }
    for (my $y = $step; $y <= $size[Y]; $y += $step) {
        $dc->DrawLine(0, $y, $size[X], $y);
    }
    
    # draw print center
    if (@{$parent->{objects}}) {
        $dc->SetPen($parent->{print_center_pen});
        $dc->DrawLine($size[X]/2, 0, $size[X]/2, $size[Y]);
        $dc->DrawLine(0, $size[Y]/2, $size[X], $size[Y]/2);
        $dc->SetTextForeground(Wx::Colour->new(0,0,0));
        $dc->SetFont(Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawLabel("X = " . $parent->{config}->print_center->[X], Wx::Rect->new(0, 0, $self->GetSize->GetWidth, $self->GetSize->GetHeight), wxALIGN_CENTER_HORIZONTAL | wxALIGN_BOTTOM);
        $dc->DrawRotatedText("Y = " . $parent->{config}->print_center->[Y], 0, $size[Y]/2+15, 90);
    }
    
    # draw frame
    if (0) {
        $dc->SetPen(wxBLACK_PEN);
        $dc->SetBrush($parent->{transparent_brush});
        $dc->DrawRectangle(0, 0, @size);
    }
    
    # draw text if plate is empty
    if (!@{$parent->{objects}}) {
        $dc->SetTextForeground(Wx::Colour->new(150,50,50));
        $dc->SetFont(Wx::Font->new(14, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawLabel(CANVAS_TEXT, Wx::Rect->new(0, 0, $self->GetSize->GetWidth, $self->GetSize->GetHeight), wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
    }
    
    # draw thumbnails
    $dc->SetPen(wxBLACK_PEN);
    @{$parent->{object_previews}} = ();
    for my $obj_idx (0 .. $#{$parent->{objects}}) {
        my $object = $parent->{objects}[$obj_idx];
        next unless defined $object->thumbnail;
        for my $instance_idx (0 .. $#{$object->instances}) {
            my $instance = $object->instances->[$instance_idx];
            
            my $thumbnail = $object->transformed_thumbnail->clone;
            $thumbnail->translate(map $parent->to_pixel($instance->[$_]) + $parent->{shift}[$_], (X,Y));
            
            push @{$parent->{object_previews}}, [ $obj_idx, $instance_idx, $thumbnail ];
            
            my $drag_object = $self->{drag_object};
            if (defined $drag_object && $obj_idx == $drag_object->[0] && $instance_idx == $drag_object->[1]) {
                $dc->SetBrush($parent->{dragged_brush});
            } elsif (grep { $_->[0] == $obj_idx } @{$parent->{selected_objects}}) {
                $dc->SetBrush($parent->{selected_brush});
            } else {
                $dc->SetBrush($parent->{objects_brush});
            }
            $dc->DrawPolygon($parent->_y($_), 0, 0) for map $_->contour->pp, @{ $parent->{object_previews}->[-1][2] };
            
            # if sequential printing is enabled and we have more than one object
            if ($parent->{config}->complete_objects && (map @{$_->instances}, @{$parent->{objects}}) > 1) {
            	my $convex_hull = Slic3r::Polygon->new(@{convex_hull([ map @{$_->contour->pp}, @{$parent->{object_previews}->[-1][2]} ])});
                my ($clearance) = @{offset([$convex_hull], $parent->{config}->extruder_clearance_radius / 2 * $parent->{scaling_factor}, 100, JT_ROUND)};
                $dc->SetPen($parent->{clearance_pen});
                $dc->SetBrush($parent->{transparent_brush});
                $dc->DrawPolygon($parent->_y($clearance), 0, 0);
            }
        }
    }
    
    # draw skirt
    if (@{$parent->{object_previews}} && $parent->{config}->skirts) {
        my $convex_hull = Slic3r::Polygon->new(@{convex_hull([ map @{$_->contour->pp}, map @{$_->[2]}, @{$parent->{object_previews}} ])});
        ($convex_hull) = @{offset([$convex_hull], $parent->{config}->skirt_distance * $parent->{scaling_factor}, 100, JT_ROUND)};
        $dc->SetPen($parent->{skirt_pen});
        $dc->SetBrush($parent->{transparent_brush});
        $dc->DrawPolygon($parent->_y($convex_hull), 0, 0) if $convex_hull;
    }
    
    $event->Skip;
}

sub mouse_event {
    my ($self, $event) = @_;
    my $parent = $self->GetParent;
    
    my $point = $event->GetPosition;
    my $pos = Slic3r::Point->new(map @$_, map @$_, $parent->_y([[$point->x, $point->y]])); #]]
    if ($event->ButtonDown(&Wx::wxMOUSE_BTN_LEFT)) {
        $parent->{selected_objects} = [];
        $parent->{list}->Select($parent->{list}->GetFirstSelected, 0);
        $parent->selection_changed(0);
        for my $preview (@{$parent->{object_previews}}) {
            my ($obj_idx, $instance_idx, $thumbnail) = @$preview;
            if (defined first { $_->contour->encloses_point($pos) } @$thumbnail) {
                $parent->{selected_objects} = [ [$obj_idx, $instance_idx] ];
                $parent->{list}->Select($obj_idx, 1);
                $parent->selection_changed(1);
                my $instance = $parent->{objects}[$obj_idx]->instances->[$instance_idx];
                $self->{drag_start_pos} = [ map $pos->[$_] - $parent->{shift}[$_] - $parent->to_pixel($instance->[$_]), X,Y ];   # displacement between the click and the instance origin
                $self->{drag_object} = $preview;
            }
        }
        $parent->Refresh;
    } elsif ($event->ButtonUp(&Wx::wxMOUSE_BTN_LEFT)) {
        $parent->recenter;
        $parent->Refresh;
        $self->{drag_start_pos} = undef;
        $self->{drag_object} = undef;
        $self->SetCursor(wxSTANDARD_CURSOR);
    } elsif ($event->ButtonDClick) {
    	$parent->object_preview_dialog if @{$parent->{selected_objects}};
    } elsif ($event->Dragging) {
        return if !$self->{drag_start_pos}; # concurrency problems
        for my $preview ($self->{drag_object}) {
            my ($obj_idx, $instance_idx, $thumbnail) = @$preview;
            my $instance = $parent->{objects}[$obj_idx]->instances->[$instance_idx];
            $instance->[$_] = $parent->to_units($pos->[$_] - $self->{drag_start_pos}[$_] - $parent->{shift}[$_]) for X,Y;
            $parent->Refresh;
        }
    } elsif ($event->Moving) {
        my $cursor = wxSTANDARD_CURSOR;
        for my $preview (@{$parent->{object_previews}}) {
            if (defined first { $_->contour->encloses_point($pos) } @{ $preview->[2] }) {
                $cursor = Wx::Cursor->new(wxCURSOR_HAND);
                last;
            }
        }
        $self->SetCursor($cursor);
    }
}

sub list_item_deselected {
    my ($self, $event) = @_;
    
    if ($self->{list}->GetFirstSelected == -1) {
        $self->{selected_objects} = [];
        $self->{canvas}->Refresh;
        $self->selection_changed(0);
    }
}

sub list_item_selected {
    my ($self, $event) = @_;
    
    my $obj_idx = $event->GetIndex;
    $self->{selected_objects} = [ grep $_->[0] == $obj_idx, @{$self->{object_previews}} ];
    $self->{canvas}->Refresh;
    $self->selection_changed(1);
}

sub list_item_activated {
    my ($self, $event, $obj_idx) = @_;
    
    $obj_idx //= $event->GetIndex;
	$self->object_preview_dialog($obj_idx);
}

sub object_preview_dialog {
    my $self = shift;
    my ($obj_idx) = @_;
    
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    if (!$Slic3r::GUI::have_OpenGL) {
        Slic3r::GUI::show_error($self, "Please install the OpenGL modules to use this feature (see build instructions).");
        return;
    }
    
    my $dlg = Slic3r::GUI::Plater::ObjectPreviewDialog->new($self,
		object => $self->{objects}[$obj_idx],
	);
	$dlg->ShowModal;
}

sub object_settings_dialog {
    my $self = shift;
    my ($obj_idx) = @_;
    
    if (!defined $obj_idx) {
        ($obj_idx, undef) = $self->selected_object;
    }
    
    # validate config before opening the settings dialog because
    # that dialog can't be closed if validation fails, but user
    # can't fix any error which is outside that dialog
    return unless $self->validate_config;
    
    my $dlg = Slic3r::GUI::Plater::ObjectSettingsDialog->new($self,
		object => $self->{objects}[$obj_idx],
	);
	$dlg->ShowModal;
}

sub object_list_changed {
    my $self = shift;
    
    my $have_objects = @{$self->{objects}} ? 1 : 0;
    my $method = $have_objects ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(reset arrange export_gcode export_stl);
    
    if ($self->{htoolbar}) {
        $self->{htoolbar}->EnableTool($_, $have_objects)
            for (TB_RESET, TB_ARRANGE);
    }
}

sub selection_changed {
    my $self = shift;
    my ($have_sel) = @_;
    
    my $method = $have_sel ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(remove increase decrease rotate45cw rotate45ccw rotate changescale split view settings);
    
    if ($self->{htoolbar}) {
        $self->{htoolbar}->EnableTool($_, $have_sel)
            for (TB_REMOVE, TB_MORE, TB_FEWER, TB_45CW, TB_45CCW, TB_ROTATE, TB_SCALE, TB_SPLIT, TB_VIEW, TB_SETTINGS);
    }
    
    if ($self->{object_info_size}) { # have we already loaded the info pane?
        if ($have_sel) {
            my ($obj_idx, $object) = $self->selected_object;
            $self->{object_info_size}->SetLabel(sprintf("%.2f x %.2f x %.2f", @{$object->transformed_size}));
            $self->{object_info_materials}->SetLabel($object->materials);
            
            if (my $stats = $object->mesh_stats) {
                $self->{object_info_volume}->SetLabel(sprintf('%.2f', $stats->{volume} * ($object->scale**3)));
                $self->{object_info_facets}->SetLabel(sprintf('%d (%d shells)', $object->facets, $stats->{number_of_parts}));
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
}

sub selected_object {
    my $self = shift;
    my $obj_idx = $self->{selected_objects}[0] ? $self->{selected_objects}[0][0] : $self->{list}->GetFirstSelected;
    return ($obj_idx, $self->{objects}[$obj_idx]),
}

sub validate_config {
    my $self = shift;
    
    eval {
        $self->skeinpanel->config->validate;
    };
    return 0 if Slic3r::GUI::catch_error($self);    
    return 1;
}

sub statusbar {
    my $self = shift;
    return $self->skeinpanel->GetParent->{statusbar};
}

sub to_pixel {
    my $self = shift;
    return $_[0] * $self->{scaling_factor};
}

sub to_units {
    my $self = shift;
    return $_[0] / $self->{scaling_factor};
}

sub _y {
    my $self = shift;
    my ($points) = @_;
    my $height = $self->{canvas}->GetSize->GetHeight;
    return [ map [ $_->[X], $height - $_->[Y] ], @$points ];
}

package Slic3r::GUI::Plater::DropTarget;
use Wx::DND;
use base 'Wx::FileDropTarget';

sub new {
    my $class = shift;
    my ($window) = @_;
    my $self = $class->SUPER::new;
    $self->{window} = $window;
    return $self;
}

sub OnDropFiles {
    my $self = shift;
    my ($x, $y, $filenames) = @_;
    
    # stop scalars leaking on older perl
    # https://rt.perl.org/rt3/Public/Bug/Display.html?id=70602
    @_ = ();
    
    # only accept STL, OBJ and AMF files
    return 0 if grep !/\.(?:stl|obj|amf(?:\.xml)?)$/i, @$filenames;
    
    $self->{window}->load_file($_) for @$filenames;
}

package Slic3r::GUI::Plater::Object;
use Moo;

use List::Util qw(first);
use Math::ConvexHull::MonotoneChain qw();
use Slic3r::Geometry qw(X Y Z MIN MAX deg2rad);

has 'name'                  => (is => 'rw', required => 1);
has 'input_file'            => (is => 'rw', required => 1);
has 'input_file_object_id'  => (is => 'rw');  # undef means keep model object
has 'model'                 => (is => 'rw', required => 1, trigger => \&_trigger_model_object);
has 'model_object_idx'      => (is => 'rw', required => 1, trigger => \&_trigger_model_object);
has 'bounding_box'          => (is => 'rw');  # 3D bb of original object (aligned to origin) with no rotation or scaling
has 'convex_hull'           => (is => 'rw');  # 2D convex hull of original object (aligned to origin) with no rotation or scaling
has 'scale'                 => (is => 'rw', default => sub { 1 }, trigger => \&_transform_thumbnail);
has 'rotate'                => (is => 'rw', default => sub { 0 }, trigger => \&_transform_thumbnail); # around object center point
has 'instances'             => (is => 'rw', default => sub { [] }); # upward Y axis
has 'thumbnail'             => (is => 'rw', trigger => \&_transform_thumbnail);
has 'transformed_thumbnail' => (is => 'rw');
has 'thumbnail_scaling_factor' => (is => 'rw', trigger => \&_transform_thumbnail);
has 'config'                => (is => 'rw', default => sub { Slic3r::Config->new });
has 'layer_height_ranges'   => (is => 'rw', default => sub { [] }); # [ z_min, z_max, layer_height ]
has 'material_mapping'      => (is => 'rw', default => sub { {} }); # { material_id => extruder_idx }
has 'mesh_stats'            => (is => 'rw');

# statistics
has 'facets'                => (is => 'rw');
has 'vertices'              => (is => 'rw');
has 'materials'             => (is => 'rw');
has 'is_manifold'           => (is => 'rw');

sub _trigger_model_object {
    my $self = shift;
    if ($self->model && defined $self->model_object_idx) {
        my $model_object = $self->model->objects->[$self->model_object_idx];
        $model_object->align_to_origin;
	    $self->bounding_box($model_object->bounding_box);
	    
    	my $mesh = $model_object->mesh;
    	$mesh->repair;
        $self->convex_hull(Slic3r::Polygon->new(@{Math::ConvexHull::MonotoneChain::convex_hull($mesh->vertices)}));
	    $self->facets($mesh->facets_count);
	    $self->vertices(scalar @{$mesh->vertices});
	    $self->materials($model_object->materials_count);
	}
}

sub changescale {
    my $self = shift;
    my ($scale) = @_;
    
    my $variation = $scale / $self->scale;
    foreach my $range (@{ $self->layer_height_ranges }) {
        $range->[0] *= $variation;
        $range->[1] *= $variation;
    }
    $self->scale($scale);
}

sub needed_repair {
	my $self = shift;
	
    if ($self->get_model_object->needed_repair) {
        warn "Warning: the input file contains manifoldness errors. "
            . "Slic3r repaired it successfully by guessing what the correct shape should be, "
            . "but you might still want to inspect the G-code before printing.\n";
        $self->is_manifold(0);
    } else {
        $self->is_manifold(1);
    }
	return $self->is_manifold;
}

sub free_model_object {
    my $self = shift;
    
    # only delete mesh from memory if we can retrieve it from the original file
    return unless $self->input_file && defined $self->input_file_object_id;
    $self->model(undef);
    $self->model_object_idx(undef);
}

sub get_model_object {
    my $self = shift;
    
    if ($self->model) {
        return $self->model->objects->[$self->model_object_idx];
    }
    
    return Slic3r::Model
        ->read_from_file($self->input_file)
        ->objects
        ->[$self->input_file_object_id];
}

sub instances_count {
    my $self = shift;
    return scalar @{$self->instances};
}

sub make_thumbnail {
    my $self = shift;
    
    my $mesh = $self->get_model_object->mesh;  # $self->model_object is already aligned to origin
    $mesh->repair;
    if (@{$mesh->facets} <= 5000) {
        # remove polygons with area <= 1mm
        my $area_threshold = Slic3r::Geometry::scale 1;
        $self->thumbnail->append(
            map $_->simplify(0.5),
            grep $_->area >= $area_threshold,
            @{ $mesh->horizontal_projection },
        );
    } else {
        my $convex_hull = Slic3r::ExPolygon->new($self->convex_hull)->clone;
        $convex_hull->scale(1/&Slic3r::SCALING_FACTOR);
        $self->thumbnail->append($convex_hull);
    }
    
    $self->thumbnail->scale(&Slic3r::SCALING_FACTOR);
    
    return $self->thumbnail;
}

sub _transform_thumbnail {
    my $self = shift;
    
    return unless defined $self->thumbnail;
    my $t = $self->_apply_transform($self->thumbnail);
    $t->scale($self->thumbnail_scaling_factor);
    
    $self->transformed_thumbnail($t);
}

# bounding box with applied rotation and scaling
sub transformed_bounding_box {
    my $self = shift;
    
    my $bb = Slic3r::Geometry::BoundingBox->new_from_points($self->_apply_transform($self->convex_hull));
    $bb->extents->[Z] = $self->bounding_box->clone->extents->[Z];
    $bb->extents->[Z][MAX] *= $self->scale;
    return $bb;
}

sub _apply_transform {
    my $self = shift;
    my ($entity) = @_;    # can be anything that implements ->clone(), ->rotate() and ->scale()
    
    # the order of these transformations MUST be the same everywhere, including
    # in Slic3r::Print->add_model()
    my $result = $entity->clone;
    $result->rotate(deg2rad($self->rotate), $self->bounding_box->center_2D);
    $result->scale($self->scale);
    return $result;
}

sub transformed_size {
    my $self = shift;
    return $self->transformed_bounding_box->size;
}

1;
