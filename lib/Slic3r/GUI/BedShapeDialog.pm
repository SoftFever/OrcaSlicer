package Slic3r::GUI::BedShapeDialog;
use strict;
use warnings;
use utf8;

use List::Util qw(min max);
use Slic3r::Geometry qw(PI X Y unscale);
use Wx qw(:dialog :id :misc :sizer :choicebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, $default) = @_;
    my $self = $class->SUPER::new($parent, -1, "Bed Shape", wxDefaultPosition, [350,700], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    
    $self->{panel} = my $panel = Slic3r::GUI::BedShapePanel->new($self, $default);
    
    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($panel, 1, wxEXPAND);
    $main_sizer->Add($self->CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
    
    $self->SetSizer($main_sizer);
    $self->SetMinSize($self->GetSize);
    $main_sizer->SetSizeHints($self);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

sub GetValue {
    my ($self) = @_;
    return $self->{panel}->GetValue;
}

package Slic3r::GUI::BedShapePanel;

use List::Util qw(min max sum first);
use Slic3r::Geometry qw(PI X Y scale unscale scaled_epsilon deg2rad);
use Slic3r::Geometry::Clipper qw(intersection_pl);
use Wx qw(:font :id :misc :sizer :choicebook :filedialog :pen :brush wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_CHOICEBOOK_PAGE_CHANGED EVT_BUTTON EVT_PAINT);
use base 'Wx::Panel';

use constant SHAPE_RECTANGULAR  => 0;
use constant SHAPE_CIRCULAR     => 1;
use constant SHAPE_CUSTOM       => 2;

sub new {
    my $class = shift;
    my ($parent, $default) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->on_change(undef);
    
    my $box = Wx::StaticBox->new($self, -1, "Shape");
    my $sbsizer = Wx::StaticBoxSizer->new($box, wxVERTICAL);
    
    # shape options
    $self->{shape_options_book} = Wx::Choicebook->new($self, -1, wxDefaultPosition, [300,-1], wxCHB_TOP);
    $sbsizer->Add($self->{shape_options_book});
    
    $self->{optgroups} = [];
    {
        my $optgroup = $self->_init_shape_options_page('Rectangular');
        $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'rect_size',
            type        => 'point',
            label       => 'Size',
            tooltip     => 'Size in X and Y of the rectangular plate.',
            default     => [200,200],
        ));
        $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'rect_origin',
            type        => 'point',
            label       => 'Origin',
            tooltip     => 'Distance of the 0,0 G-code coordinate from the front left corner of the rectangle.',
            default     => [0,0],
        ));
    }
    {
        my $optgroup = $self->_init_shape_options_page('Circular');
        $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'diameter',
            type        => 'f',
            label       => 'Diameter',
            tooltip     => 'Diameter of the print bed. It is assumed that origin (0,0) is located in the center.',
            sidetext    => 'mm',
            default     => 200,
        ));
    }
    {
        my $optgroup = $self->_init_shape_options_page('Custom');
        $optgroup->append_line(Slic3r::GUI::OptionsGroup::Line->new(
            full_width  => 1,
            widget      => sub {
                my ($parent) = @_;
                
                my $btn = Wx::Button->new($parent, -1, "Load shape from STL...", wxDefaultPosition, wxDefaultSize);
                EVT_BUTTON($self, $btn, sub { $self->_load_stl });
                return $btn;
            }
        ));
    }
    
    EVT_CHOICEBOOK_PAGE_CHANGED($self, -1, sub {
        $self->_update_shape;
    });
    
    # right pane with preview canvas
    my $canvas = $self->{canvas} = Wx::Panel->new($self, -1, wxDefaultPosition, [250,-1], wxTAB_TRAVERSAL);
    EVT_PAINT($canvas, sub {
        $self->_repaint_canvas;
    });
    
    # main sizer
    my $top_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $top_sizer->Add($sbsizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
    $top_sizer->Add($canvas, 1, wxEXPAND | wxALL, 10) if $canvas;
    
    $self->SetSizerAndFit($top_sizer);
    
    $self->_set_shape($default);
    $self->_update_preview;
    
    return $self;
}

sub on_change {
    my ($self, $cb) = @_;
    $self->{on_change} = $cb // sub {};
}

sub _set_shape {
    my ($self, $points) = @_;
    
    $self->{bed_shape} = $points;
    
    # is this a rectangle?
    if (@$points == 4) {
        my $polygon = Slic3r::Polygon->new_scale(@$points);
        my $lines = $polygon->lines;
        if ($lines->[0]->parallel_to_line($lines->[2]) && $lines->[1]->parallel_to_line($lines->[3])) {
            # okay, it's a rectangle
            
            # find origin
            # the || 0 hack prevents "-0" which might confuse the user
            my $x_min = min(map $_->[X], @$points) || 0;
            my $x_max = max(map $_->[X], @$points) || 0;
            my $y_min = min(map $_->[Y], @$points) || 0;
            my $y_max = max(map $_->[Y], @$points) || 0;
            my $origin = [-$x_min, -$y_min];
            
            $self->{shape_options_book}->SetSelection(SHAPE_RECTANGULAR);
            my $optgroup = $self->{optgroups}[SHAPE_RECTANGULAR];
            $optgroup->set_value('rect_size', [ $x_max-$x_min, $y_max-$y_min ]);
            $optgroup->set_value('rect_origin', $origin);
            $self->_update_shape;
            return;
        }
    }
    
    # is this a circle?
    {
        my $polygon = Slic3r::Polygon->new_scale(@$points);
        my $center = $polygon->bounding_box->center;
        my @vertex_distances = map $center->distance_to($_), @$polygon;
        my $avg_dist = sum(@vertex_distances)/@vertex_distances;
        if (!defined first { abs($_ - $avg_dist) > 10*scaled_epsilon } @vertex_distances) {
            # all vertices are equidistant to center
            $self->{shape_options_book}->SetSelection(SHAPE_CIRCULAR);
            my $optgroup = $self->{optgroups}[SHAPE_CIRCULAR];
            $optgroup->set_value('diameter', sprintf("%.0f", unscale($avg_dist*2)));
            $self->_update_shape;
            return;
        }
    }
    
    $self->{shape_options_book}->SetSelection(SHAPE_CUSTOM);
    $self->_update_shape;
}

sub _update_shape {
    my ($self) = @_;
    
    my $page_idx = $self->{shape_options_book}->GetSelection;
    if ($page_idx == SHAPE_RECTANGULAR) {
        my $rect_size = $self->{optgroups}[SHAPE_RECTANGULAR]->get_value('rect_size');
        my $rect_origin = $self->{optgroups}[SHAPE_RECTANGULAR]->get_value('rect_origin');
        my ($x, $y) = @$rect_size;
        return if !$x || !$y;  # empty strings
        my ($x0, $y0) = (0,0);
        my ($x1, $y1) = ($x,$y);
        {
            my ($dx, $dy) = @$rect_origin;
            return if $dx eq '' || $dy eq '';  # empty strings
            $x0 -= $dx;
            $x1 -= $dx;
            $y0 -= $dy;
            $y1 -= $dy;
        }
        $self->{bed_shape} = [
            [$x0,$y0],
            [$x1,$y0],
            [$x1,$y1],
            [$x0,$y1],
        ];
    } elsif ($page_idx == SHAPE_CIRCULAR) {
        my $diameter = $self->{optgroups}[SHAPE_CIRCULAR]->get_value('diameter');
        return if !$diameter;
        my $r = $diameter/2;
        my $twopi = 2*PI;
        my $edges = 60;
        my $polygon = Slic3r::Polygon->new_scale(
            map [ $r * cos $_, $r * sin $_ ],
                map { $twopi/$edges*$_ } 1..$edges
        );
        $self->{bed_shape} = [
            map [ unscale($_->x), unscale($_->y) ], @$polygon  #))
        ];
    }
    
    $self->{on_change}->();
    $self->_update_preview;
}

sub _update_preview {
    my ($self) = @_;
    $self->{canvas}->Refresh if $self->{canvas};
}

sub _repaint_canvas {
    my ($self) = @_;
    
    my $canvas = $self->{canvas};
    my $dc = Wx::PaintDC->new($canvas);
    my ($cw, $ch) = $canvas->GetSizeWH;
    return if $cw == 0;  # when canvas is not rendered yet, size is 0,0
    
    # turn $cw and $ch from sizes to max coordinates
    $cw--;
    $ch--;
    
    my $cbb = Slic3r::Geometry::BoundingBoxf->new_from_points([
        Slic3r::Pointf->new(0, 0),
        Slic3r::Pointf->new($cw, $ch),
    ]);
    
    # leave space for origin point
    $cbb->set_x_min($cbb->x_min + 2);
    $cbb->set_y_max($cbb->y_max - 2);
    
    # leave space for origin label
    $cbb->set_y_max($cbb->y_max - 10);
    
    # read new size
    ($cw, $ch) = @{$cbb->size};
    my $ccenter = $cbb->center;
    
    # get bounding box of bed shape in G-code coordinates
    my $bed_shape = $self->{bed_shape};
    my $bed_polygon = Slic3r::Polygon->new_scale(@$bed_shape);
    my $bb = Slic3r::Geometry::BoundingBoxf->new_from_points($bed_shape);
    $bb->merge_point(Slic3r::Pointf->new(0,0));  # origin needs to be in the visible area
    my ($bw, $bh) = @{$bb->size};
    my $bcenter = $bb->center;
    
    # calculate the scaling factor for fitting bed shape in canvas area
    my $sfactor = min($cw/$bw, $ch/$bh);
    my $shift = [
        $ccenter->x - $bcenter->x * $sfactor,
        $ccenter->y - $bcenter->y * $sfactor,  #-
    ];
    
    # prepare function to convert G-code coordinates into pixels
    my $to_pixel = sub {
        my ($point) = @_;
        
        my $p = Slic3r::Pointf->new(@$point);
        $p->scale($sfactor);
        $p->translate(@$shift);
        return [ $p->x + $cbb->x_min, $ch-$p->y + $cbb->y_min ];  #++
    };
    
    # draw bed fill
    {
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,0,0), 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(255,255,255), wxSOLID));
        $dc->DrawPolygon([ map $to_pixel->($_), @$bed_shape ], 0, 0);
    }
    
    # draw grid
    {
        my $step = 10;  # 1cm grid
        my @polylines = ();
        for (my $x = $bb->x_min - ($bb->x_min % $step) + $step; $x < $bb->x_max; $x += $step) {
            push @polylines, Slic3r::Polyline->new_scale([$x, $bb->y_min], [$x, $bb->y_max]);
        }
        for (my $y = $bb->y_min - ($bb->y_min % $step) + $step; $y < $bb->y_max; $y += $step) {
            push @polylines, Slic3r::Polyline->new_scale([$bb->x_min, $y], [$bb->x_max, $y]);
        }
        @polylines = @{intersection_pl(\@polylines, [$bed_polygon])};
        
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(230,230,230), 1, wxSOLID));
        $dc->DrawLine(map @{$to_pixel->([map unscale($_), @$_])}, @$_[0,-1]) for @polylines;
    }
    
    # draw bed contour
    {
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,0,0), 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(255,255,255), wxTRANSPARENT));
        $dc->DrawPolygon([ map $to_pixel->($_), @$bed_shape ], 0, 0);
    }
    
    my $origin_px = $to_pixel->(Slic3r::Pointf->new(0,0));
    
    # draw axes
    {
        my $axes_len = 50;
        my $arrow_len = 6;
        my $arrow_angle = deg2rad(45);
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(255,0,0), 2, wxSOLID));  # red
        my $x_end = Slic3r::Pointf->new($origin_px->[X] + $axes_len, $origin_px->[Y]);
        $dc->DrawLine(@$origin_px, @$x_end);
        foreach my $angle (-$arrow_angle, +$arrow_angle) {
            my $end = $x_end->clone;
            $end->translate(-$arrow_len, 0);
            $end->rotate($angle, $x_end);
            $dc->DrawLine(@$x_end, @$end);
        }
        
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,255,0), 2, wxSOLID));  # green
        my $y_end = Slic3r::Pointf->new($origin_px->[X], $origin_px->[Y] - $axes_len);
        $dc->DrawLine(@$origin_px, @$y_end);
        foreach my $angle (-$arrow_angle, +$arrow_angle) {
            my $end = $y_end->clone;
            $end->translate(0, +$arrow_len);
            $end->rotate($angle, $y_end);
            $dc->DrawLine(@$y_end, @$end);
        }
    }
    
    # draw origin
    {
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,0,0), 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(0,0,0), wxSOLID));
        $dc->DrawCircle(@$origin_px, 3);
        
        $dc->SetTextForeground(Wx::Colour->new(0,0,0));
        $dc->SetFont(Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawText("(0,0)", $origin_px->[X] + 1, $origin_px->[Y] + 2);
    }
}

sub _init_shape_options_page {
    my ($self, $title) = @_;
    
    my $panel = Wx::Panel->new($self->{shape_options_book});
    my $optgroup;
    push @{$self->{optgroups}}, $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent      => $panel,
        title       => 'Settings',
        label_width => 100,
        on_change   => sub {
            my ($opt_id) = @_;
            #$self->{"_$opt_id"} = $optgroup->get_value($opt_id);
            $self->_update_shape;
        },
    );
    $panel->SetSizerAndFit($optgroup->sizer);
    $self->{shape_options_book}->AddPage($panel, $title);
    
    return $optgroup;
}

sub _load_stl {
    my ($self) = @_;
    
    my $dialog = Wx::FileDialog->new($self, 'Choose a file to import bed shape from (STL/OBJ/AMF):', "", "", &Slic3r::GUI::MODEL_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if ($dialog->ShowModal != wxID_OK) {
        $dialog->Destroy;
        return;
    }
    my $input_file = Slic3r::decode_path($dialog->GetPaths);
    $dialog->Destroy;
    
    my $model = Slic3r::Model->read_from_file($input_file);
    my $mesh = $model->raw_mesh;
    my $expolygons = $mesh->horizontal_projection;
    
    if (@$expolygons == 0) {
        Slic3r::GUI::show_error($self, "The selected file contains no geometry.");
        return;
    }
    if (@$expolygons > 1) {
        Slic3r::GUI::show_error($self, "The selected file contains several disjoint areas. This is not supported.");
        return;
    }
    
    my $polygon = $expolygons->[0]->contour;
    $self->{bed_shape} = [ map [ unscale($_->x), unscale($_->y) ], @$polygon ];  #))
}

sub GetValue {
    my ($self) = @_;
    return $self->{bed_shape};
}

1;
