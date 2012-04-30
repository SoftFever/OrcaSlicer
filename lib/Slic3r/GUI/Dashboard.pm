package Slic3r::GUI::Dashboard;
use strict;
use warnings;
my $have_threads = eval "use threads; 1";
use threads::shared;
use utf8;

use File::Basename qw(basename dirname);
use Math::ConvexHull qw(convex_hull);
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 scale unscale);
use Slic3r::Geometry::Clipper qw(JT_ROUND);
use Wx qw(:sizer :progressdialog wxOK wxICON_INFORMATION wxICON_WARNING wxICON_ERROR wxICON_QUESTION
    wxOK wxCANCEL wxID_OK wxFD_OPEN wxFD_SAVE wxDEFAULT wxNORMAL);
use Wx::Event qw(EVT_BUTTON EVT_PAINT EVT_MOUSE_EVENTS EVT_LIST_ITEM_SELECTED EVT_LIST_ITEM_DESELECTED
    EVT_COMMAND);
use base 'Wx::Panel';

my $THUMBNAIL_DONE_EVENT : shared = Wx::NewEventType;

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->{canvas} = Wx::Panel->new($self, -1, [-1, -1], [300, 300]);
    $self->{canvas}->SetBackgroundColour(Wx::wxWHITE);
    EVT_PAINT($self->{canvas}, \&repaint);
    EVT_MOUSE_EVENTS($self->{canvas}, \&mouse_event);
    
    $self->{objects_brush} = Wx::Brush->new(Wx::Colour->new(210,210,210), &Wx::wxSOLID);
    $self->{selected_brush} = Wx::Brush->new(Wx::Colour->new(255,128,128), &Wx::wxSOLID);
    $self->{transparent_brush} = Wx::Brush->new(Wx::Colour->new(0,0,0), &Wx::wxTRANSPARENT);
    $self->{grid_pen} = Wx::Pen->new(Wx::Colour->new(230,230,230), 1, &Wx::wxSOLID);
    $self->{skirt_pen} = Wx::Pen->new(Wx::Colour->new(150,150,150), 1, &Wx::wxSOLID);
    
    $self->{list} = Wx::ListView->new($self, -1, [-1, -1], [-1, 180], &Wx::wxLC_SINGLE_SEL | &Wx::wxLC_REPORT | &Wx::wxBORDER_DEFAULT);
    $self->{list}->InsertColumn(0, "Name", &Wx::wxLIST_FORMAT_LEFT, 300);
    $self->{list}->InsertColumn(1, "Copies", &Wx::wxLIST_FORMAT_CENTER, 50);
    $self->{list}->InsertColumn(2, "Scale", &Wx::wxLIST_FORMAT_CENTER, 50);
    EVT_LIST_ITEM_SELECTED($self, $self->{list}, \&list_item_selected);
    EVT_LIST_ITEM_DESELECTED($self, $self->{list}, \&list_item_deselected);
    
    $self->{btn_load} = Wx::Button->new($self, -1, "Add…");
    $self->{btn_remove} = Wx::Button->new($self, -1, "Delete");
    $self->{btn_increase} = Wx::Button->new($self, -1, "+1 copy");
    $self->{btn_decrease} = Wx::Button->new($self, -1, "-1 copy");
    $self->{btn_rotate45cw} = Wx::Button->new($self, -1, "Rotate by 45° (cw)");
    $self->{btn_rotate45ccw} = Wx::Button->new($self, -1, "Rotate by 45° (ccw)");
    $self->{btn_rotate} = Wx::Button->new($self, -1, "Rotate…");
    $self->{btn_reset} = Wx::Button->new($self, -1, "Delete All");
    $self->{btn_arrange} = Wx::Button->new($self, -1, "Autoarrange");
    $self->{btn_changescale} = Wx::Button->new($self, -1, "Change Scale…");
    $self->{btn_split} = Wx::Button->new($self, -1, "Split");
    $self->{btn_export_gcode} = Wx::Button->new($self, -1, "Export G-code…");
    $self->{btn_export_gcode}->SetDefault;
    $self->{btn_export_stl} = Wx::Button->new($self, -1, "Export STL…");
    $self->selection_changed(0);
    $self->object_list_changed;
    EVT_BUTTON($self, $self->{btn_load}, \&load);
    EVT_BUTTON($self, $self->{btn_remove}, \&remove);
    EVT_BUTTON($self, $self->{btn_increase}, \&increase);
    EVT_BUTTON($self, $self->{btn_decrease}, \&decrease);
    EVT_BUTTON($self, $self->{btn_rotate45cw}, sub { $_[0]->rotate(-45) });
    EVT_BUTTON($self, $self->{btn_rotate45ccw}, sub { $_[0]->rotate(45) });
    EVT_BUTTON($self, $self->{btn_reset}, \&reset);
    EVT_BUTTON($self, $self->{btn_arrange}, \&arrange);
    EVT_BUTTON($self, $self->{btn_changescale}, \&changescale);
    EVT_BUTTON($self, $self->{btn_rotate}, sub { $_[0]->rotate(undef) });
    EVT_BUTTON($self, $self->{btn_split}, \&split_object);
    EVT_BUTTON($self, $self->{btn_export_gcode}, \&export_gcode);
    EVT_BUTTON($self, $self->{btn_export_stl}, \&export_stl);
    
    $_->SetDropTarget(Slic3r::GUI::Dashboard::DropTarget->new($self))
        for $self, $self->{canvas}, $self->{list};
    
    EVT_COMMAND($self, -1, $THUMBNAIL_DONE_EVENT, sub {
        my ($self, $event) = @_;
        my ($obj_idx, $thumbnail) = @{$event->GetData};
        $self->{thumbnails}[$obj_idx] = $thumbnail;
        $self->make_thumbnail2;
    });
    
    $self->update_bed_size;
    $self->{print} = Slic3r::Print->new;
    $self->{thumbnails} = [];       # polygons, each one aligned to 0,0
    $self->{scale} = [];
    $self->{object_previews} = [];  # [ obj_idx, copy_idx, positioned polygon ]
    $self->{selected_objects} = [];
    $self->recenter;
    
    {
        my @col1 = qw(load remove reset arrange export_gcode export_stl);
        my @col2 =  qw(increase decrease rotate45cw rotate45ccw rotate changescale split);
        my $buttons = Wx::GridBagSizer->new();
        for (my $row = 0; $row < scalar(@col1); $row++) {
            $buttons->Add($self->{"btn_$col1[$row]"}, Wx::GBPosition->new($row, 0), Wx::GBSpan->new(1, 1), wxEXPAND | wxALL);
        }
        
        for (my $row = 0; $row < scalar(@col2); $row++) {
            $buttons->Add($self->{"btn_$col2[$row]"}, Wx::GBPosition->new($row, 1), Wx::GBSpan->new(1, 1), wxEXPAND | wxALL);
        }
        
        my $vertical_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $vertical_sizer->Add($self->{list}, 0, wxEXPAND | wxALL);
        $vertical_sizer->Add($buttons);
        
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($self->{canvas}, 0, wxALL, 10);
        $sizer->Add($vertical_sizer, 1, wxEXPAND | wxALL, 10);
        $sizer->SetSizeHints($self);
        $self->SetSizer($sizer);
    }
    return $self;
}

sub load {
    my $self = shift;
    
    my $dir = $Slic3r::GUI::SkeinPanel::last_skein_dir || $Slic3r::GUI::SkeinPanel::last_config_dir || "";
    my $dialog = Wx::FileDialog->new($self, 'Choose a STL or AMF file:', $dir, "", $Slic3r::GUI::SkeinPanel::model_wildcard, wxFD_OPEN);
    if ($dialog->ShowModal != wxID_OK) {
        $dialog->Destroy;
        return;
    }
    my $input_file = $dialog->GetPaths;
    $dialog->Destroy;
    return $self->load_file($input_file);
}

sub load_file {
    my $self = shift;
    my ($input_file) = @_;
    
    $Slic3r::GUI::SkeinPanel::last_input_file = $input_file;
    
    my $process_dialog = Wx::ProgressDialog->new('Loading...', "Processing input file...", 100, $self, 0);
    $process_dialog->Pulse;
    local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
    $self->{print}->add_object_from_file($input_file);
    my $obj_idx = $#{$self->{print}->objects};
    $process_dialog->Destroy;
    
    $self->object_loaded($obj_idx);
}

sub object_loaded {
    my $self = shift;
    my ($obj_idx, %params) = @_;
    
    my $object = $self->{print}->objects->[$obj_idx];
    $self->{list}->InsertStringItem($obj_idx, basename($object->input_file));
    $self->{list}->SetItem($obj_idx, 1, "1");
    $self->{list}->SetItem($obj_idx, 2, "100%");
    push @{$self->{scale}}, 1;
    
    $self->make_thumbnail($obj_idx);
    $self->arrange unless $params{no_arrange};
    $self->{list}->Update;
    $self->{list}->Select($obj_idx, 1);
    $self->object_list_changed;
}

sub remove {
    my $self = shift;
    
    foreach my $pobj (@{$self->{selected_objects}}) {
        my ($obj_idx, $copy_idx) = ($pobj->[0], $pobj->[1]);
        $self->{print}->copies->[$obj_idx][$copy_idx] = undef;
    }
    
    my @objects_to_remove = ();
    for my $obj_idx (0 .. $#{$self->{print}->objects}) {
        my $copies = $self->{print}->copies->[$obj_idx];
        
        # filter out removed copies
        @$copies = grep defined $_, @$copies;
        
        # update copies count in list
        $self->{list}->SetItem($obj_idx, 1, scalar @$copies);
        
        # if no copies are left, remove the object itself
        push @objects_to_remove, $obj_idx if !@$copies;
    }
    for my $obj_idx (sort { $b <=> $a } @objects_to_remove) {
        splice @{$self->{print}->objects}, $obj_idx, 1;
        splice @{$self->{print}->copies}, $obj_idx, 1;
        splice @{$self->{thumbnails}}, $obj_idx, 1;
        splice @{$self->{scale}}, $obj_idx, 1;
        $self->{list}->DeleteItem($obj_idx);
    }
    
    $self->{selected_objects} = [];
    $self->selection_changed(0);
    $self->object_list_changed;
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub reset {
    my $self = shift;
    
    @{$self->{print}->objects} = ();
    @{$self->{print}->copies} = ();
    @{$self->{thumbnails}} = ();
    @{$self->{scale}} = ();
    $self->{list}->DeleteAllItems;
    
    $self->{selected_objects} = [];
    $self->selection_changed(0);
    $self->object_list_changed;
    $self->{canvas}->Refresh;
}

sub increase {
    my $self = shift;
    
    my $obj_idx = $self->selected_object_idx;
    my $copies = $self->{print}->copies->[$obj_idx];
    push @$copies, [ $copies->[-1]->[X] + scale 10, $copies->[-1]->[Y] + scale 10 ];
    $self->{list}->SetItem($obj_idx, 1, scalar @$copies);
    $self->arrange;
}

sub decrease {
    my $self = shift;
    
    my $obj_idx = $self->selected_object_idx;
    $self->{selected_objects} = [ +(grep { $_->[0] == $obj_idx } @{$self->{object_previews}})[-1] ];
    $self->remove;
    
    if ($self->{print}->objects->[$obj_idx]) {
        $self->{list}->Select($obj_idx, 0);
        $self->{list}->Select($obj_idx, 1);
    }
}

sub rotate {
    my $self = shift;
    my ($angle) = @_;
    
    my $obj_idx = $self->selected_object_idx;
    my $object = $self->{print}->objects->[$obj_idx];
    
    if (!defined $angle) {
        $angle = Wx::GetNumberFromUser("", "Enter the rotation angle:", "Rotate", 0, -364, 364, $self);
        return if !$angle || $angle == -1;
    }
    
    # rotate, realign to 0,0 and update size
    $object->mesh->rotate($angle);
    $object->mesh->align_to_origin;
    my @size = $object->mesh->size;
    $object->x_length($size[X]);
    $object->y_length($size[Y]);
    
    $self->make_thumbnail($obj_idx);
    $self->arrange;
}

sub arrange {
    my $self = shift;
    
    eval {
        $self->{print}->arrange_objects;
    };
    Slic3r::GUI::warning_catcher($self)->($@) if $@;
    
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub changescale {
    my $self = shift;
    
    my $obj_idx = $self->selected_object_idx;
    my $scale = $self->{scale}[$obj_idx];
    $scale = Wx::GetNumberFromUser("", "Enter the scale % for the selected object:", "Scale", $scale*100, 0, 1000, $self);
    return if !$scale || $scale == -1;
    
    my $object = $self->{print}->objects->[$obj_idx];
    my $mesh = $object->mesh;
    $mesh->scale($scale/100 / $self->{scale}[$obj_idx]);
    $object->mesh->align_to_origin;
    my @size = $object->mesh->size;
    $object->x_length($size[X]);
    $object->y_length($size[Y]);
    
    $self->{scale}[$obj_idx] = $scale/100;
    $self->{list}->SetItem($obj_idx, 2, "$scale%");
    
    $self->make_thumbnail($obj_idx);
    $self->arrange;
}

sub split_object {
    my $self = shift;
    
    my $obj_idx = $self->selected_object_idx;
    my $current_object = $self->{print}->objects->[$obj_idx];
    my $mesh = $current_object->mesh->clone;
    $mesh->scale($Slic3r::scaling_factor);
    
    foreach my $mesh ($mesh->split_mesh) {
        my $object = $self->{print}->add_object_from_mesh($mesh);
        $object->input_file($current_object->input_file);
        $self->object_loaded($#{$self->{print}->objects}, no_arrange => 1);
    }
    
    $self->{list}->Select($obj_idx, 1);
    $self->remove;
    $self->arrange;
}

sub export_gcode {
    my $self = shift;
    
    my $process_dialog;
    eval {
        # validate configuration
        Slic3r::Config->validate;
        
        my $print = $self->{print};
        
        # select output file
        my $output_file = $main::opt{output};
        {
            $output_file = $print->expanded_output_filepath($output_file);
            my $dlg = Wx::FileDialog->new($self, 'Save G-code file as:', dirname($output_file),
                basename($output_file), $Slic3r::GUI::SkeinPanel::gcode_wildcard, wxFD_SAVE);
            if ($dlg->ShowModal != wxID_OK) {
                $dlg->Destroy;
                return;
            }
            $output_file = $Slic3r::GUI::SkeinPanel::last_output_file = $dlg->GetPath;
            $dlg->Destroy;
        }
        
        # show processbar dialog
        $process_dialog = Wx::ProgressDialog->new('Slicing...', "Processing input file...", 
            100, $self, 0);
        $process_dialog->Pulse;
        
        {
            my @warnings = ();
            local $SIG{__WARN__} = sub { push @warnings, $_[0] };
            my %params = (
                output_file => $output_file,
                status_cb   => sub {
                    my ($percent, $message) = @_;
                    if (&Wx::wxVERSION_STRING =~ / 2\.(8\.|9\.[2-9])/) {
                        $process_dialog->Update($percent, "$message...");
                    }
                },
                keep_meshes => 1,
            );
            if ($params{export_svg}) {
                $print->export_svg(%params);
            } else {
                $print->export_gcode(%params);
            }
            Slic3r::GUI::warning_catcher($self)->($_) for @warnings;
        }
        $process_dialog->Destroy;
        undef $process_dialog;
        
        my $message = "Your files were successfully sliced";
        $message .= sprintf " in %d minutes and %.3f seconds",
            int($print->processing_time/60),
            $print->processing_time - int($print->processing_time/60)*60
                if $print->processing_time;
        $message .= ".";
        eval {
            $self->{growler}->notify(Event => 'SKEIN_DONE', Title => 'Slicing Done!', Message => $message)
                if ($self->{growler});
        };
        Wx::MessageDialog->new($self, $message, 'Done!', 
            wxOK | wxICON_INFORMATION)->ShowModal;
    };
    Slic3r::GUI::catch_error($self, sub { $process_dialog->Destroy if $process_dialog });
}

sub export_stl {
    my $self = shift;
    
    my $print = $self->{print};
        
    # select output file
    my $output_file = $main::opt{output};
    {
        $output_file = $print->expanded_output_filepath($output_file);
        $output_file =~ s/\.gcode$/.stl/i;
        my $dlg = Wx::FileDialog->new($self, 'Save STL file as:', dirname($output_file),
            basename($output_file), $Slic3r::GUI::SkeinPanel::model_wildcard, wxFD_SAVE);
        if ($dlg->ShowModal != wxID_OK) {
            $dlg->Destroy;
            return;
        }
        $output_file = $Slic3r::GUI::SkeinPanel::last_output_file = $dlg->GetPath;
        $dlg->Destroy;
    }
    
    my $mesh = Slic3r::TriangleMesh->new(facets => [], vertices => []);
    for my $obj_idx (0 .. $#{$print->objects}) {
        for my $copy (@{$print->copies->[$obj_idx]}) {
            my $cloned_mesh = $print->objects->[$obj_idx]->mesh->clone;
            $cloned_mesh->move(@$copy);
            my $vertices_offset = scalar @{$mesh->vertices};
            push @{$mesh->vertices}, @{$cloned_mesh->vertices};
            push @{$mesh->facets}, map [ $_->[0], map $vertices_offset + $_, @$_[1,2,3] ], @{$cloned_mesh->facets};
        }
    }
    $mesh->scale($Slic3r::scaling_factor);
    $mesh->align_to_origin;
    
    Slic3r::Format::STL->write_file($output_file, $mesh, 1);
}

sub make_thumbnail {
    my $self = shift;
    my ($obj_idx) = @_;
    
    my $cb = sub {
        my $object = $self->{print}->objects->[$obj_idx];
        my @points = map [ @$_[X,Y] ], @{$object->mesh->vertices};
        my $convex_hull = Slic3r::Polygon->new(convex_hull(\@points));
        for (@$convex_hull) {
            @$_ = map $self->to_pixel($_), @$_;
        }
        $convex_hull->simplify(0.3);
        $self->{thumbnails}->[$obj_idx] = $convex_hull;  # ignored in multithread environment
        
        if ($have_threads) {
            Wx::PostEvent($self, Wx::PlThreadEvent->new(-1, $THUMBNAIL_DONE_EVENT, shared_clone([ $obj_idx, $convex_hull ])));
            threads->exit;
        } else {
            $self->make_thumbnail2;
        }
    };
    
    $have_threads ? threads->create($cb) : $cb->();
}

sub make_thumbnail2 {
    my $self = shift;
    $self->recenter;
    $self->{canvas}->Refresh;
}

sub recenter {
    my $self = shift;
    
    # calculate displacement needed to center the print
    my @print_bb = $self->{print}->bounding_box;
    @print_bb = (0,0,0,0) if !defined $print_bb[0];
    $self->{shift} = [
        ($self->{canvas}->GetSize->GetWidth  - ($self->to_pixel($print_bb[X2] + $print_bb[X1]))) / 2,
        ($self->{canvas}->GetSize->GetHeight - ($self->to_pixel($print_bb[Y2] + $print_bb[Y1]))) / 2,
    ];
}

sub update_bed_size {
    my $self = shift;
    
    # supposing the preview canvas is square, calculate the scaling factor
    # to constrain print bed area inside preview
    my $canvas_side = $self->{canvas}->GetSize->GetWidth;
    my $bed_largest_side = $Slic3r::bed_size->[X] > $Slic3r::bed_size->[Y]
        ? $Slic3r::bed_size->[Y] : $Slic3r::bed_size->[X];
    my $old_scaling_factor = $self->{scaling_factor};
    $self->{scaling_factor} = $canvas_side / $bed_largest_side;
    if (defined $old_scaling_factor && $self->{scaling_factor} != $old_scaling_factor) {
        $self->make_thumbnail($_) for 0..$#{$self->{thumbnails}};
    }
}

sub repaint {
    my ($self, $event) = @_;
    my $parent = $self->GetParent;
    my $print = $parent->{print};
    
    my $dc = Wx::PaintDC->new($self);
    my $size = $self->GetSize;
    my @size = ($size->GetWidth, $size->GetHeight);
    
    # calculate scaling factor for preview
    $parent->update_bed_size;
    
    # draw grid
    $dc->SetPen($parent->{grid_pen});
    my $step = 10 * $parent->{scaling_factor};
    for (my $x = $step; $x <= $size[X]; $x += $step) {
        $dc->DrawLine($x, 0, $x, $size[Y]);
    }
    for (my $y = $step; $y <= $size[Y]; $y += $step) {
        $dc->DrawLine(0, $y, $size[X], $y);
    }
    
    # draw frame
    $dc->SetPen(Wx::wxBLACK_PEN);
    $dc->SetBrush($parent->{transparent_brush});
    $dc->DrawRectangle(0, 0, @size);
    
    # draw text if plate is empty
    if (!@{$print->objects}) {
        $dc->SetTextForeground(Wx::Colour->new(150,50,50));
        $dc->DrawLabel("Drag your objects here", Wx::Rect->new(0, 0, $self->GetSize->GetWidth, $self->GetSize->GetHeight), &Wx::wxALIGN_CENTER_HORIZONTAL | &Wx::wxALIGN_CENTER_VERTICAL);
    }
    
    # draw thumbnails
    $dc->SetPen(Wx::wxBLACK_PEN);
    @{$parent->{object_previews}} = ();
    for my $obj_idx (0 .. $#{$print->objects}) {
        next unless $parent->{thumbnails}[$obj_idx];
        for my $copy_idx (0 .. $#{$print->copies->[$obj_idx]}) {
            my $copy = $print->copies->[$obj_idx][$copy_idx];
            push @{$parent->{object_previews}}, [ $obj_idx, $copy_idx, $parent->{thumbnails}[$obj_idx]->clone ];
            $parent->{object_previews}->[-1][2]->translate(map $parent->to_pixel($copy->[$_]) + $parent->{shift}[$_], (X,Y));
            
            if (grep { $_->[0] == $obj_idx } @{$parent->{selected_objects}}) {
                $dc->SetBrush($parent->{selected_brush});
            } else {
                $dc->SetBrush($parent->{objects_brush});
            }
            $dc->DrawPolygon($parent->_y($parent->{object_previews}->[-1][2]), 0, 0);
        }
    }
    
    # draw skirt
    if (@{$parent->{object_previews}} && $Slic3r::skirts) {
        my $convex_hull = Slic3r::Polygon->new(convex_hull([ map @{$_->[2]}, @{$parent->{object_previews}} ]));
        $convex_hull = +($convex_hull->offset($Slic3r::skirt_distance * $parent->{scaling_factor}, 1, JT_ROUND))[0];
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
    } elsif ($event->Dragging) {
        return if !$self->{drag_start_pos}; # concurrency problems
        for my $obj ($self->{drag_object}) {
            my $copy = $print->copies->[ $obj->[0] ]->[ $obj->[1] ];
            $copy->[$_] = $parent->to_scaled($pos->[$_] - $self->{drag_start_pos}[$_] - $parent->{shift}[$_]) for X,Y;
            $parent->Refresh;
        }
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
        for qw(reset arrange export_gcode export_stl);
}

sub selection_changed {
    my $self = shift;
    my ($have_sel) = @_;
    
    my $method = $have_sel ? 'Enable' : 'Disable';
    $self->{"btn_$_"}->$method
        for qw(remove increase decrease rotate45cw rotate45ccw rotate changescale split);
}

sub selected_object_idx {
    my $self = shift;
    return $self->{selected_objects}[0] ? $self->{selected_objects}[0][0] : $self->{list}->GetFirstSelected;
}

sub to_pixel {
    my $self = shift;
    return unscale $_[0] * $self->{scaling_factor};
}

sub to_scaled {
    my $self = shift;
    return scale $_[0] / $self->{scaling_factor};
}

sub _y {
    my $self = shift;
    my ($points) = @_;
    my $height = $self->{canvas}->GetSize->GetHeight;
    return [ map [ $_->[X], $height - $_->[Y] ], @$points ];
}

package Slic3r::GUI::Dashboard::DropTarget;

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
    
    # only accept STL and AMF files
    return 0 if grep !/\.(?:stl|amf(?:\.xml)?)$/i, @$filenames;
    
    $self->{window}->load_file($_) for @$filenames;
}

1;
