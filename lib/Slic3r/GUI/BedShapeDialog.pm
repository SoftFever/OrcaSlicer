package Slic3r::GUI::BedShapeDialog;
use strict;
use warnings;
use utf8;

use List::Util qw(min max);
use Slic3r::Geometry qw(PI X Y unscale);
use Wx qw(:dialog :id :misc :sizer :choicebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON EVT_CHOICE);
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

use List::Util qw(min max);
use Slic3r::Geometry qw(PI X Y unscale);
use Wx qw(:dialog :id :misc :sizer :choicebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON EVT_CHOICE);
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
    $self->_init_shape_options_page('Rectangular', [
        {
            opt_key     => 'rect_size',
            type        => 'point',
            label       => 'Size',
            tooltip     => 'Size in X and Y of the rectangular plate.',
            default     => [200,200],
        },
        {
            opt_key     => 'rect_origin',
            type        => 'select',
            label       => 'Origin',
            tooltip     => 'Position of the 0,0 point.',
            labels      => ['Front left corner','Center'],
            values      => ['corner','center'],
            default     => 'corner',
        },
    ]);
    
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
    }
    
    $self->{on_change}->();
    $self->_update_preview;
}

sub _update_preview {
    my ($self) = @_;
    
    
}

sub _init_shape_options_page {
    my ($self, $title, $options) = @_;
    
    my $panel = Wx::Panel->new($self->{shape_options_book});
    push @{$self->{optgroups}}, my $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent      => $panel,
        title       => 'Settings',
        options     => $options,
        on_change   => sub {
            my ($opt_key, $value) = @_;
            $self->{"_$opt_key"} = $value;
            $self->_update_shape;
        },
        label_width => 100,
    );
    $panel->SetSizerAndFit($optgroup->sizer);
    $self->{shape_options_book}->AddPage($panel, $title);
}

sub GetValue {
    my ($self) = @_;
    return $self->{bed_shape};
}

1;
