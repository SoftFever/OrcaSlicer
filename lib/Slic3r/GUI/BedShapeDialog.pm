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
use Slic3r::Geometry qw(PI X Y unscale scaled_epsilon);
use Wx qw(:dialog :id :misc :sizer :choicebook :filedialog wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_CHOICEBOOK_PAGE_CHANGED EVT_BUTTON);
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
            type        => 'select',
            label       => 'Origin',
            tooltip     => 'Position of the 0,0 point.',
            labels      => ['Front left corner','Center'],
            values      => ['corner','center'],
            default     => 'corner',
        ));
        $optgroup->on_change->($_) for qw(rect_size rect_origin);  # set defaults
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
        $optgroup->on_change->($_) for qw(diameter);  # set defaults
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
    my $canvas;
    
    # main sizer
    my $top_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $top_sizer->Add($sbsizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
    $top_sizer->Add($canvas, 1, wxEXPAND | wxALL, 0) if $canvas;
    
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
            # let's check whether origin is at a known point
            my $x_min = min(map $_->[X], @$points);
            my $x_max = max(map $_->[X], @$points);
            my $y_min = min(map $_->[Y], @$points);
            my $y_max = max(map $_->[Y], @$points);
            my $origin;
            if ($x_min == 0 && $y_min == 0) {
                $origin = 'corner';
            } elsif (($x_min + $x_max)/2 == 0 && ($y_min + $y_max)/2 == 0) {
                $origin = 'center';
            }
            if (defined $origin) {
                $self->{shape_options_book}->SetSelection(SHAPE_RECTANGULAR);
                my $optgroup = $self->{optgroups}[SHAPE_RECTANGULAR];
                $optgroup->set_value('rect_size', [ $x_max-$x_min, $y_max-$y_min ]);
                $optgroup->set_value('rect_origin', $origin);
                return;
            }
        }
    }
    
    # is this a circle?
    {
        my $polygon = Slic3r::Polygon->new_scale(@$points);
        my $center = $polygon->bounding_box->center;
        my @vertex_distances = map $center->distance_to($_), @$polygon;
        my $avg_dist = sum(@vertex_distances)/@vertex_distances;
        if (!defined first { abs($_ - $avg_dist) > scaled_epsilon } @vertex_distances) {
            # all vertices are equidistant to center
            $self->{shape_options_book}->SetSelection(SHAPE_CIRCULAR);
            my $optgroup = $self->{optgroups}[SHAPE_CIRCULAR];
            $optgroup->set_value('diameter', sprintf("%.0f", unscale($avg_dist*2)));
            return;
        }
    }
    
    $self->{shape_options_book}->SetSelection(SHAPE_CUSTOM);
}

sub _update_shape {
    my ($self) = @_;
    
    my $page_idx = $self->{shape_options_book}->GetSelection;
    if ($page_idx == SHAPE_RECTANGULAR) {
        return if grep !defined($self->{"_$_"}), qw(rect_size rect_origin);  # not loaded yet
        my ($x, $y) = @{$self->{_rect_size}};
        my ($x0, $y0) = (0,0);
        my ($x1, $y1) = ($x,$y);
        if ($self->{_rect_origin} eq 'center') {
            $x0 -= $x/2;
            $x1 -= $x/2;
            $y0 -= $y/2;
            $y1 -= $y/2;
        }
        $self->{bed_shape} = [
            [$x0,$y0],
            [$x1,$y0],
            [$x1,$y1],
            [$x0,$y1],
        ];
    } elsif ($page_idx == SHAPE_CIRCULAR) {
        return if grep !defined($self->{"_$_"}), qw(diameter);  # not loaded yet
        my $r = $self->{_diameter}/2;
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
            $self->{"_$opt_id"} = $optgroup->get_value($opt_id);
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
    my $input_file = $dialog->GetPaths;
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
