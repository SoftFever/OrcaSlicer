package Slic3r::GUI::Plater;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use List::Util qw(max sum);
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2);
use Slic3r::Geometry::Clipper qw(JT_ROUND);
use threads::shared qw(shared_clone);
use Wx qw(:bitmap :brush :button :cursor :dialog :filedialog :font :keycode :icon :id :listctrl :misc :panel :pen :sizer :toolbar :window);
use Wx::Event qw(EVT_BUTTON EVT_COMMAND EVT_KEY_DOWN EVT_LIST_ITEM_DESELECTED EVT_LIST_ITEM_SELECTED EVT_MOUSE_EVENTS EVT_PAINT EVT_TOOL EVT_CHOICE);
use base 'Wx::Panel';

use constant TB_MORE    => &Wx::NewId;
use constant TB_LESS    => &Wx::NewId;
use constant TB_45CW    => &Wx::NewId;
use constant TB_45CCW   => &Wx::NewId;
use constant TB_ROTATE  => &Wx::NewId;
use constant TB_SCALE   => &Wx::NewId;
use constant TB_SPLIT   => &Wx::NewId;

my $THUMBNAIL_DONE_EVENT    : shared = Wx::NewEventType;
my $PROGRESS_BAR_EVENT      : shared = Wx::NewEventType;
my $MESSAGE_DIALOG_EVENT    : shared = Wx::NewEventType;
my $EXPORT_COMPLETED_EVENT  : shared = Wx::NewEventType;
my $EXPORT_FAILED_EVENT     : shared = Wx::NewEventType;

use constant CANVAS_SIZE => [300,300];
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
        $self->{htoolbar}->AddTool(TB_MORE, "More", Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_LESS, "Less", Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_45CCW, "45° ccw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_anticlockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_45CW, "45° cw", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddTool(TB_ROTATE, "Rotate…", Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_SCALE, "Scale…", Wx::Bitmap->new("$Slic3r::var/arrow_out.png", wxBITMAP_TYPE_PNG), '');
        $self->{htoolbar}->AddSeparator;
        $self->{htoolbar}->AddTool(TB_SPLIT, "Split", Wx::Bitmap->new("$Slic3r::var/shape_ungroup.png", wxBITMAP_TYPE_PNG), '');
    } else {
        my %tbar_buttons = (increase => "More", decrease => "Less", rotate45ccw => "45°", rotate45cw => "45°",
            rotate => "Rotate…", changescale => "Scale…", split => "Split");
        $self->{btoolbar} = Wx::BoxSizer->new(wxHORIZONTAL);
        for (qw(increase decrease rotate45ccw rotate45cw rotate changescale split)) {
            $self->{"btn_$_"} = Wx::Button->new($self, -1, $tbar_buttons{$_}, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            $self->{btoolbar}->Add($self->{"btn_$_"});
        }
    }

    $self->{list} = Wx::ListView->new($self, -1, wxDefaultPosition, [-1, 180], wxLC_SINGLE_SEL | wxLC_REPORT | wxBORDER_SUNKEN | wxTAB_TRAVERSAL | wxWANTS_CHARS);
    $self->{list}->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
    $self->{list}->InsertColumn(1, "Copies", wxLIST_FORMAT_CENTER, 50);
    $self->{list}->InsertColumn(2, "Scale", wxLIST_FORMAT_CENTER, wxLIST_AUTOSIZE_USEHEADER);
    EVT_LIST_ITEM_SELECTED($self, $self->{list}, \&list_item_selected);
    EVT_LIST_ITEM_DESELECTED($self, $self->{list}, \&list_item_deselected);
    EVT_KEY_DOWN($self->{list}, sub {
        my ($list, $event) = @_;
        if ($event->GetKeyCode == WXK_TAB) {
            $list->Navigate($event->ShiftDown ? &Wx::wxNavigateBackward : &Wx::wxNavigateForward);
        } else {
            $event->Skip;
        }
    });
    
    # general buttons
    $self->{btn_load} = Wx::Button->new($self, -1, "Add…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_remove} = Wx::Button->new($self, -1, "Delete", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_reset} = Wx::Button->new($self, -1, "Delete All", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_arrange} = Wx::Button->new($self, -1, "Autoarrange", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_gcode} = Wx::Button->new($self, -1, "Export G-code…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_export_stl} = Wx::Button->new($self, -1, "Export STL…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    
    if (&Wx::wxVERSION_STRING =~ / 2\.9\.[1-9]/) {
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
        );
        for (grep $self->{"btn_$_"}, keys %icons) {
            $self->{"btn_$_"}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/$icons{$_}", wxBITMAP_TYPE_PNG));
        }
    }
    $self->selection_changed(0);
    $self->object_list_changed;
    EVT_BUTTON($self, $self->{btn_load}, \&load);
    EVT_BUTTON($self, $self->{btn_remove}, sub { $self->remove() }); # explicitly pass no argument to remove
    EVT_BUTTON($self, $self->{btn_reset}, \&reset);
    EVT_BUTTON($self, $self->{btn_arrange}, \&arrange);
    EVT_BUTTON($self, $self->{btn_export_gcode}, \&export_gcode);
    EVT_BUTTON($self, $self->{btn_export_stl}, \&export_stl);
    
    if ($self->{htoolbar}) {
        EVT_TOOL($self, TB_MORE, \&increase);
        EVT_TOOL($self, TB_LESS, \&decrease);
        EVT_TOOL($self, TB_45CW, sub { $_[0]->rotate(-45) });
        EVT_TOOL($self, TB_45CCW, sub { $_[0]->rotate(45) });
        EVT_TOOL($self, TB_ROTATE, sub { $_[0]->rotate(undef) });
        EVT_TOOL($self, TB_SCALE, \&changescale);
        EVT_TOOL($self, TB_SPLIT, \&split_object);
    } else {
        EVT_BUTTON($self, $self->{btn_increase}, \&increase);
        EVT_BUTTON($self, $self->{btn_decrease}, \&decrease);
        EVT_BUTTON($self, $self->{btn_rotate45cw}, sub { $_[0]->rotate(-45) });
        EVT_BUTTON($self, $self->{btn_rotate45ccw}, sub { $_[0]->rotate(45) });
        EVT_BUTTON($self, $self->{btn_changescale}, \&changescale);
        EVT_BUTTON($self, $self->{btn_rotate}, sub { $_[0]->rotate(undef) });
        EVT_BUTTON($self, $self->{btn_split}, \&split_object);
    }
    
    $_->SetDropTarget(Slic3r::GUI::Plater::DropTarget->new($self))
        for $self, $self->{canvas}, $self->{list};
    
    EVT_COMMAND($self, -1, $THUMBNAIL_DONE_EVENT, sub {
        my ($self, $event) = @_;
        my ($obj_idx, $thumbnail) = @{$event->GetData};
        $self->{objects}[$obj_idx]->thumbnail($thumbnail);
        $self->mesh(undef);
        $self->on_thumbnail_made;
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
    $self->{objects} = [];
    $self->{selected_objects} = [];
    $self->recenter;
    
    {
        my $buttons = Wx::GridSizer->new(2, 3, 5, 5);
        $buttons->Add($self->{"btn_load"}, 0, wxEXPAND | wxALL);
        $buttons->Add($self->{"btn_arrange"}, 0, wxEXPAND | wxALL);
        $buttons->Add($self->{"btn_export_gcode"}, 0, wxEXPAND | wxALL);
        $buttons->Add($self->{"btn_remove"}, 0, wxEXPAND | wxALL);
        $buttons->Add($self->{"btn_reset"}, 0, wxEXPAND | wxALL);
        $buttons->Add($self->{"btn_export_stl"}, 0, wxEXPAND | wxALL);
        # force sane tab order
        my @taborder = qw/btn_load btn_arrange btn_export_gcode btn_remove btn_reset btn_export_stl/;
        $self->{$taborder[$_]}->MoveAfterInTabOrder($self->{$taborder[$_-1]}) for (1..$#taborder);
        
        my $vertical_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $vertical_sizer->Add($self->{htoolbar}, 0, wxEXPAND, 0) if $self->{htoolbar};
        $vertical_sizer->Add($self->{btoolbar}, 0, wxEXPAND, 0) if $self->{btoolbar};
        $vertical_sizer->Add($self->{list}, 1, wxEXPAND | wxBOTTOM, 10);
        $vertical_sizer->Add($buttons, 0, wxEXPAND);
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $hsizer->Add($self->{canvas}, 0, wxALL, 10);
        $hsizer->Add($vertical_sizer, 1, wxEXPAND | wxALL, 10);
        
        my $presets = Wx::BoxSizer->new(wxHORIZONTAL);
        $presets->AddStretchSpacer(1);
        my %group_labels = (
            print       => 'Print settings',
            filament    => 'Filament',
            printer     => 'Printer',
        );
        $self->{preset_choosers} = {};
        $self->{preset_choosers_sizers} = {};
        for my $group (qw(print filament printer)) {
            my $text = Wx::StaticText->new($self, -1, "$group_labels{$group}:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
            my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], []);
            $self->{preset_choosers}{$group} = [$choice];
            EVT_CHOICE($choice, $choice, sub {
                my $choice = shift;  # avoid leaks
                return if $group eq 'filament' && @{$self->{preset_choosers}{filament}} > 1; #/
                $self->skeinpanel->{options_tabs}{$group}->select_preset($choice->GetSelection);
            });
            
            $self->{preset_choosers_sizers}{$group} = Wx::BoxSizer->new(wxVERTICAL);
            $self->{preset_choosers_sizers}{$group}->Add($choice, 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
            
            $presets->Add($text, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
            $presets->Add($self->{preset_choosers_sizers}{$group}, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
        }
        $presets->AddStretchSpacer(1);
        
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        $sizer->Add($hsizer, 1, wxEXPAND | wxBOTTOM, 10);
        $sizer->Add($presets, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
    }
    return $self;
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
    my $dialog = Wx::FileDialog->new($self, 'Choose one or more files (STL/OBJ/AMF):', $dir, "", $Slic3r::GUI::SkeinPanel::model_wildcard, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
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
    
    $Slic3r::GUI::Settings->{recent}{skein_directory} = dirname($input_file);
    Slic3r::GUI->save_settings;
    
    my $process_dialog = Wx::ProgressDialog->new('Loading…', "Processing input file…", 100, $self, 0);
    $process_dialog->Pulse;
    
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    my $model = Slic3r::Model->read_from_file($input_file);
    for my $i (0 .. $#{$model->objects}) {
        my $object = Slic3r::GUI::Plater::Object->new(
            name                    => basebane($input_file),
            input_file              => $input_file,
            input_file_object_id    => $i,
            mesh                    => $model->objects->[$i]->mesh,
            instances               => [
                $model->objects->[$i]->instances
                    ? (map $_->offset, @{$model->objects->[$i]->instances})
                    : [0,0],
            ],
        );
        push @{ $self->{objects} }, $object;
        $self->object_loaded($#{ $self->{objects} });
    }
    
    $process_dialog->Destroy;
    $self->statusbar->SetStatusText("Loaded $input_file");
}

sub object_loaded {
    my $self = shift;
    my ($obj_idx, %params) = @_;
    
    my $object = $self->{objects}[$obj_idx];
    $self->{list}->InsertStringItem($obj_idx, $object->name);
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
    $self->{selected_objects} = [ +(grep { $_->[0] == $obj_idx } @{$self->{object_previews}})[-1] ];
    $self->remove;
    
    if ($self->{objects}[$obj_idx]) {
        $self->{list}->Select($obj_idx, 0);
        $self->{list}->Select($obj_idx, 1);
    }
}

sub rotate {
    my $self = shift;
    my ($angle) = @_;
    
    my ($obj_idx, $object) = $self->selected_object;
    
    if (!defined $angle) {
        $angle = Wx::GetNumberFromUser("", "Enter the rotation angle:", "Rotate", $object->rotate, -364, 364, $self);
        return if !$angle || $angle == -1;
    }
    
    $object->set_rotation($angle);
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub changescale {
    my $self = shift;
    
    my ($obj_idx, $object) = $self->selected_object;
    
    # max scale factor should be above 2540 to allow importing files exported in inches
    my $scale = Wx::GetNumberFromUser("", "Enter the scale % for the selected object:", "Scale", $object->scale*100, 0, 5000, $self);
    return if !$scale || $scale == -1;
    
    $object->set_scale($scale);
    $self->arrange;
}

sub arrange {
    my $self = shift;
    
    my $total_parts = sum(map $_->instances_count, @{$self->{objects}}) or return;
    my @size = ();
    for my $a (X,Y) {
        $size[$a] = max(map $_->thumbnail->size->[$a], @{$self->{objects}});
    }
    
    eval {
        my $config = $self->skeinpanel->config;
        my @positions = Slic3r::Geometry::arrange
            ($total_parts, @size, @{$config->bed_size}, $config->min_object_distance);
    };
    # ignore arrange warnings on purpose
    
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub split_object {
    my $self = shift;
    
    my ($obj_idx, $current_object) = $self->selected_object;
    my $current_copies_num = $current_object->instances_count;
    my $mesh = $current_object->mesh->clone;
    $mesh->scale(&Slic3r::SCALING_FACTOR);
    
    my @new_meshes = $mesh->split_mesh;
    if (@new_meshes == 1) {
        Slic3r::GUI::warning_catcher($self)->("The selected object couldn't be splitted because it already contains a single part.");
        return;
    }
    
    # remove the original object before spawning the object_loaded event, otherwise 
    # we'll pass the wrong $obj_idx to it (which won't be recognized after the
    # thumbnail thread returns)
    $self->remove($obj_idx);
    
    foreach my $mesh (@new_meshes) {
        my $object = $self->{print}->add_object_from_mesh($mesh);
        $object->input_file($current_object->input_file);
        my $new_obj_idx = $#{$self->{print}->objects};
        push @{$self->{print}->copies->[$new_obj_idx]}, [0,0] for 2..$current_copies_num;
        $self->object_loaded($new_obj_idx, no_arrange => 1);
    }
    
    $self->arrange;
}

sub export_gcode {
    my $self = shift;
    
    if ($self->{export_thread}) {
        Wx::MessageDialog->new($self, "Another slicing job is currently running.", 'Error', wxOK | wxICON_ERROR)->ShowModal;
        return;
    }
    
    # set this before spawning the thread because ->config needs GetParent and it's not available there
    $self->{print}->config($self->skeinpanel->config);
    
    # select output file
    $self->{output_file} = $main::opt{output};
    {
        $self->{output_file} = $self->{print}->expanded_output_filepath($self->{output_file});
        my $dlg = Wx::FileDialog->new($self, 'Save G-code file as:', dirname($self->{output_file}),
            basename($self->{output_file}), $Slic3r::GUI::SkeinPanel::gcode_wildcard, wxFD_SAVE);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return;
        }
        $self->{output_file} = $Slic3r::GUI::SkeinPanel::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    $self->statusbar->StartBusy;
    if ($Slic3r::have_threads) {
        $self->{export_thread} = threads->create(sub {
            $self->export_gcode2(
                $self->{output_file},
                progressbar     => sub { Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $PROGRESS_BAR_EVENT, shared_clone([@_]))) },
                message_dialog  => sub { Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $MESSAGE_DIALOG_EVENT, shared_clone([@_]))) },
                on_completed    => sub { Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $EXPORT_COMPLETED_EVENT, shared_clone([@_]))) },
                catch_error     => sub {
                    Slic3r::GUI::catch_error($self, $_[0], sub {
                        Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $MESSAGE_DIALOG_EVENT, shared_clone([@_])));
                        Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $EXPORT_FAILED_EVENT, undef));
                    });
                },
            );
        });
        $self->statusbar->SetCancelCallback(sub {
            $self->{export_thread}->kill('KILL');
            $self->{export_thread} = undef;
            $self->statusbar->StopBusy;
            $self->statusbar->SetStatusText("Export cancelled");
        });
    } else {
        $self->export_gcode2(
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
    my ($output_file, %params) = @_;
    $Slic3r::Geometry::Clipper::clipper = Math::Clipper->new;
    local $SIG{'KILL'} = sub {
        Slic3r::debugf "Exporting cancelled; exiting thread...\n";
        threads->exit();
    } if $Slic3r::have_threads;
    
    eval {
        my $print = $self->{print};
        $print->config->validate;
        $print->validate;
        
        {
            my @warnings = ();
            local $SIG{__WARN__} = sub { push @warnings, $_[0] };
            my %params = (
                output_file => $output_file,
                status_cb   => sub { $params{progressbar}->(@_) },
                keep_meshes => 1,
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
        $print->cleanup;
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
        $output_file = $self->{print}->expanded_output_filepath($output_file);
        $output_file =~ s/\.gcode$/$suffix/i;
        my $dlg = Wx::FileDialog->new($self, "Save $format file as:", dirname($output_file),
            basename($output_file), $Slic3r::GUI::SkeinPanel::model_wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
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
    for my $obj_idx (0 .. $#{$self->{objects}}) {
        my $object = $self->{objects}[$obj_idx];
        # TODO: reload file
        my $mesh = $self->{print}->[$obj_idx]->mesh->clone;
        $mesh->scale(&Slic3r::SCALING_FACTOR);
        my $model_object = $model->add_object(vertices => $mesh->vertices);
        $model_object->add_volume(facets => $mesh->facets);
        for my $copy (@{$self->{print}->copies->[$obj_idx]}) {
            $object->add_instance(rotation => 0, offset => [ map unscale $_, @$copy ]);
        }
    }
    # TODO: $model->align_to_origin;
    
    return $model;
}

sub make_thumbnail {
    my $self = shift;
    my ($obj_idx) = @_;
    
    my $cb = sub {
        my $object = $self->{objects}[$obj_idx];
        my $thumbnail = $object->make_thumbnail;
        
        if ($Slic3r::have_threads) {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $THUMBNAIL_DONE_EVENT, shared_clone([ $obj_idx, $thumbnail ])));
            threads->exit;
        } else {
            $self->on_thumbnail_made;
        }
    };
    
    $Slic3r::have_threads ? threads->create($cb)->detach : $cb->();
}

sub on_thumbnail_made {
    my $self = shift;
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub recenter {
    my $self = shift;
    
    # calculate displacement needed to center the print
    my @print_bb = (0,0,0,0);
    foreach my $object (@{$self->{objects}}) {
        foreach my $instance (@{$object->instances}) {
            my @bb = (@$instance, map $instance->[$_] + $object->size->[$_], X,Y);
            $print_bb[X1] = $bb[X1] if $bb[X1] < $print_bb[X1];
            $print_bb[Y1] = $bb[Y1] if $bb[Y1] < $print_bb[Y1];
            $print_bb[X2] = $bb[X2] if $bb[X2] > $print_bb[X2];
            $print_bb[Y2] = $bb[Y2] if $bb[Y2] > $print_bb[Y2];
        }
    }
    $self->{shift} = [
        ($self->{canvas}->GetSize->GetWidth  - ($self->to_pixel($print_bb[X2] + $print_bb[X1]))) / 2,
        ($self->{canvas}->GetSize->GetHeight - ($self->to_pixel($print_bb[Y2] + $print_bb[Y1]))) / 2,
    ];
}

sub on_config_change {
    my $self = shift;
    my ($opt_key, $value) = @_;
    if ($opt_key eq 'extruders_count' && defined $value) {
        my $choices = $self->{preset_choosers}{filament};
        while (@$choices < $value) {
            push @$choices, Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], [$choices->[0]->GetStrings]);
            $self->{preset_choosers_sizers}{filament}->Add($choices->[-1], 0, wxEXPAND | wxBOTTOM, FILAMENT_CHOOSERS_SPACING);
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
    my $bed_size = $self->{config}->bed_size;
    my $canvas_side = CANVAS_SIZE->[X];  # when the canvas is not rendered yet, its GetSize() method returns 0,0
    my $bed_largest_side = $bed_size->[X] > $bed_size->[Y] ? $bed_size->[X] : $bed_size->[Y];
    my $old_scaling_factor = $self->{scaling_factor};
    $self->{scaling_factor} = $canvas_side / $bed_largest_side;
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
    $dc->SetPen(wxBLACK_PEN);
    $dc->SetBrush($parent->{transparent_brush});
    $dc->DrawRectangle(0, 0, @size);
    
    # draw text if plate is empty
    if (@{$parent->{objects}}) {
        $dc->SetTextForeground(Wx::Colour->new(150,50,50));
        $dc->SetFont(Wx::Font->new(14, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawLabel(CANVAS_TEXT, Wx::Rect->new(0, 0, $self->GetSize->GetWidth, $self->GetSize->GetHeight), wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
    }
    
    # draw thumbnails
    $dc->SetPen(wxBLACK_PEN);
    @{$parent->{object_previews}} = ();
    for my $obj_idx (0 .. $#{$parent->{objects}}) {
        next unless $parent->{thumbnails}[$obj_idx];
        for my $copy_idx (0 .. $#{$print->copies->[$obj_idx]}) {
            my $copy = $print->copies->[$obj_idx][$copy_idx];
            push @{$parent->{object_previews}}, [ $obj_idx, $copy_idx, $parent->{thumbnails}[$obj_idx]->clone ];
            $parent->{object_previews}->[-1][2]->translate(map $parent->to_pixel($copy->[$_]) + $parent->{shift}[$_], (X,Y));
            
            my $drag_object = $self->{drag_object};
            if (defined $drag_object && $obj_idx == $drag_object->[0] && $copy_idx == $drag_object->[1]) {
                $dc->SetBrush($parent->{dragged_brush});
            } elsif (grep { $_->[0] == $obj_idx } @{$parent->{selected_objects}}) {
                $dc->SetBrush($parent->{selected_brush});
            } else {
                $dc->SetBrush($parent->{objects_brush});
            }
            $dc->DrawPolygon($parent->_y($parent->{object_previews}->[-1][2]), 0, 0);
            
            # if sequential printing is enabled and we have more than one object
            if ($parent->{config}->complete_objects && (map @$_, @{$print->copies}) > 1) {
                my $clearance = +($parent->{object_previews}->[-1][2]->offset($parent->{config}->extruder_clearance_radius / 2 * $parent->{scaling_factor}, 1, JT_ROUND))[0];
                $dc->SetPen($parent->{clearance_pen});
                $dc->SetBrush($parent->{transparent_brush});
                $dc->DrawPolygon($parent->_y($clearance), 0, 0);
            }
        }
    }
    
    # draw skirt
    if (@{$parent->{object_previews}} && $parent->{config}->skirts) {
        my $convex_hull = Slic3r::Polygon->new(convex_hull([ map @{$_->[2]}, @{$parent->{object_previews}} ]));
        $convex_hull = +($convex_hull->offset($parent->{config}->skirt_distance * $parent->{scaling_factor}, 1, JT_ROUND))[0];
        $dc->SetPen($parent->{skirt_pen});
        $dc->SetBrush($parent->{transparent_brush});
        $dc->DrawPolygon($parent->_y($convex_hull), 0, 0) if $convex_hull;
    }
    
    $event->Skip;
}

sub mouse_event {
    my ($self, $event) = @_;
    my $parent = $self->GetParent;
    my $print = $parent->{print};
    
    my $point = $event->GetPosition;
    my $pos = $parent->_y([[$point->x, $point->y]])->[0]; #]]
    if ($event->ButtonDown(&Wx::wxMOUSE_BTN_LEFT)) {
        $parent->{selected_objects} = [];
        $parent->{list}->Select($parent->{list}->GetFirstSelected, 0);
        $parent->selection_changed(0);
        for my $preview (@{$parent->{object_previews}}) {
            if ($preview->[2]->encloses_point($pos)) {
                $parent->{selected_objects} = [$preview];
                $parent->{list}->Select($preview->[0], 1);
                $parent->selection_changed(1);
                my $copy = $print->copies->[ $preview->[0] ]->[ $preview->[1] ];
                $self->{drag_start_pos} = [ map $pos->[$_] - $parent->{shift}[$_] - $parent->to_pixel($copy->[$_]), X,Y ];   # displacement between the click and the copy's origin
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
    } elsif ($event->Dragging) {
        return if !$self->{drag_start_pos}; # concurrency problems
        for my $obj ($self->{drag_object}) {
            my $copy = $print->copies->[ $obj->[0] ]->[ $obj->[1] ];
            $copy->[$_] = $parent->to_scaled($pos->[$_] - $self->{drag_start_pos}[$_] - $parent->{shift}[$_]) for X,Y;
            $parent->Refresh;
        }
    } elsif ($event->Moving) {
        my $cursor = wxSTANDARD_CURSOR;
        for my $preview (@{$parent->{object_previews}}) {
            if ($preview->[2]->encloses_point($pos)) {
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

sub object_list_changed {
    my $self = shift;
    
    my $method = $self->{print} && @{$self->{print}->objects} ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(reset arrange export_gcode export_stl);
}

sub selection_changed {
    my $self = shift;
    my ($have_sel) = @_;
    
    my $method = $have_sel ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for grep $self->{"btn_$_"}, qw(remove increase decrease rotate45cw rotate45ccw rotate changescale split);
    
    if ($self->{htoolbar}) {
        $self->{htoolbar}->EnableTool($_, $have_sel)
            for (TB_MORE, TB_LESS, TB_45CW, TB_45CCW, TB_ROTATE, TB_SCALE, TB_SPLIT);
    }
}

sub selected_object {
    my $self = shift;
    my $obj_idx = $self->{selected_objects}[0] ? $self->{selected_objects}[0][0] : $self->{list}->GetFirstSelected;
    return ($obj_idx, $self->{objects}[$obj_idx]),
}

sub statusbar {
    my $self = shift;
    return $self->skeinpanel->GetParent->{statusbar};
}

sub to_pixel {
    my $self = shift;
    return $_[0] * $self->{scaling_factor};
}

sub to_scaled {
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
    
    # only accept STL and AMF files
    return 0 if grep !/\.(?:stl|amf(?:\.xml)?)$/i, @$filenames;
    
    $self->{window}->load_file($_) for @$filenames;
}

package Slic3r::GUI::Plater::Object;
use Moo;

use Math::ConvexHull qw(convex_hull);
use Slic3r::Geometry qw(X Y);

has 'name'                  => (is => 'rw', required => 1);
has 'input_file'            => (is => 'rw', required => 1);
has 'input_file_object_id'  => (is => 'rw', required => 1);
has 'mesh'                  => (is => 'rw', required => 1, trigger => 1);
has 'size'                  => (is => 'rw');
has 'scale'                 => (is => 'rw', default => sub { 1 });
has 'rotate'                => (is => 'rw', default => sub { 0 });
has 'instances'             => (is => 'rw', default => sub { [] });
has 'thumbnail'             => (is => 'rw');

sub _trigger_mesh {
    my $self = shift;
    $self->size($self->mesh->size) if $self->mesh;
}

sub instances_count {
    my $self = shift;
    return scalar @{$self->instances};
}

sub make_thumbnail {
    my $self = shift;
    
    my @points = map [ @$_[X,Y] ], @{$self->mesh->vertices};
    my $convex_hull = Slic3r::Polygon->new(convex_hull(\@points));
    for (@$convex_hull) {
        @$_ = map $self->to_pixel($_), @$_;
    }
    $convex_hull->simplify(0.3);
    
    $self->thumbnail($convex_hull);  # ignored in multi-threaded environments
    $self->mesh(undef);
    return $convex_hull;
}

sub set_rotation {
    my $self = shift;
    my ($angle) = @_;
    
    $self->thumbnail->rotate($angle - $self->rotate);
    $self->rotate($angle);
}

sub set_scale {
    my $self = shift;
    my ($scale) = @_;
    
    $self->thumbnail->scale($scale - $self->scale);
    $self->scale($scale);
}

1;
