# Bed shape dialog

package Slic3r::GUI::2DBed;
use strict;
use warnings;

use List::Util qw(min max);
use Slic3r::Geometry qw(X Y unscale deg2rad);
use Slic3r::Geometry::Clipper qw(intersection_pl);
use Wx qw(:misc :pen :brush :font :systemsettings wxTAB_TRAVERSAL wxSOLID);
use Wx::Event qw(EVT_PAINT EVT_ERASE_BACKGROUND EVT_MOUSE_EVENTS EVT_SIZE);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(bed_shape interactive pos _scale_factor _shift on_move _painted));

sub new {
    my ($class, $parent, $bed_shape) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, [250,-1], wxTAB_TRAVERSAL);
    $self->{user_drawn_background} = $^O ne 'darwin';
    $self->bed_shape($bed_shape // []);
    EVT_PAINT($self, \&_repaint);
    EVT_ERASE_BACKGROUND($self, sub {}) if $self->{user_drawn_background};
    EVT_MOUSE_EVENTS($self, \&_mouse_event);
    EVT_SIZE($self, sub { $self->Refresh; });
    return $self;
}

sub _repaint {
    my ($self, $event) = @_;
    
    my $dc = Wx::AutoBufferedPaintDC->new($self);
    my ($cw, $ch) = $self->GetSizeWH;
    return if $cw == 0;  # when canvas is not rendered yet, size is 0,0

    if ($self->{user_drawn_background}) {
        # On all systems the AutoBufferedPaintDC() achieves double buffering.
        # On MacOS the background is erased, on Windows the background is not erased 
        # and on Linux/GTK the background is erased to gray color.
        # Fill DC with the background on Windows & Linux/GTK.
        my $color = Wx::SystemSettings::GetSystemColour(wxSYS_COLOUR_3DLIGHT);
        $dc->SetPen(Wx::Pen->new($color, 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new($color, wxSOLID));
        my $rect = $self->GetUpdateRegion()->GetBox();
        $dc->DrawRectangle($rect->GetLeft(), $rect->GetTop(), $rect->GetWidth(), $rect->GetHeight());
    }
    
    # turn $cw and $ch from sizes to max coordinates
    $cw--;
    $ch--;
    
    my $cbb = Slic3r::Geometry::BoundingBoxf->new_from_points([
        Slic3r::Pointf->new(0, 0),
        Slic3r::Pointf->new($cw, $ch),
    ]);
    
    # leave space for origin point
    $cbb->set_x_min($cbb->x_min + 4);
    $cbb->set_x_max($cbb->x_max - 4);
    $cbb->set_y_max($cbb->y_max - 4);
    
    # leave space for origin label
    $cbb->set_y_max($cbb->y_max - 13);
    
    # read new size
    ($cw, $ch) = @{$cbb->size};
    my $ccenter = $cbb->center;
    
    # get bounding box of bed shape in G-code coordinates
    my $bed_shape = $self->bed_shape;
    my $bed_polygon = Slic3r::Polygon->new_scale(@$bed_shape);
    my $bb = Slic3r::Geometry::BoundingBoxf->new_from_points($bed_shape);
    $bb->merge_point(Slic3r::Pointf->new(0,0));  # origin needs to be in the visible area
    my ($bw, $bh) = @{$bb->size};
    my $bcenter = $bb->center;
    
    # calculate the scaling factor for fitting bed shape in canvas area
    my $sfactor = min($cw/$bw, $ch/$bh);
    my $shift = Slic3r::Pointf->new(
        $ccenter->x - $bcenter->x * $sfactor,
        $ccenter->y - $bcenter->y * $sfactor,  #-
    );
    $self->_scale_factor($sfactor);
    $self->_shift(Slic3r::Pointf->new(
        $shift->x + $cbb->x_min,
        $shift->y - ($cbb->y_max-$self->GetSize->GetHeight), #++
    ));
    
    # draw bed fill
    {
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,0,0), 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(255,255,255), wxSOLID));
        $dc->DrawPolygon([ map $self->to_pixels($_), @$bed_shape ], 0, 0);
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
        $dc->DrawLine(map @{$self->to_pixels([map unscale($_), @$_])}, @$_[0,-1]) for @polylines;
    }
    
    # draw bed contour
    {
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(0,0,0), 1, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(255,255,255), wxTRANSPARENT));
        $dc->DrawPolygon([ map $self->to_pixels($_), @$bed_shape ], 0, 0);
    }
    
    my $origin_px = $self->to_pixels(Slic3r::Pointf->new(0,0));
    
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
    
    # draw current position
    if (defined $self->pos) {
        my $pos_px = $self->to_pixels($self->pos);
        $dc->SetPen(Wx::Pen->new(Wx::Colour->new(200,0,0), 2, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(Wx::Colour->new(200,0,0), wxTRANSPARENT));
        $dc->DrawCircle(@$pos_px, 5);
        
        $dc->DrawLine($pos_px->[X]-15, $pos_px->[Y], $pos_px->[X]+15, $pos_px->[Y]);
        $dc->DrawLine($pos_px->[X], $pos_px->[Y]-15, $pos_px->[X], $pos_px->[Y]+15);
    }

    $self->_painted(1);
}

sub _mouse_event {
    my ($self, $event) = @_;
    
    return if !$self->interactive;
    return if !$self->_painted;
    
    my $pos = $event->GetPosition;
    my $point = $self->to_units([ $pos->x, $pos->y ]);  #]]
    if ($event->LeftDown || $event->Dragging) {
        $self->on_move->($point) if $self->on_move;
        $self->Refresh;
    }
}

# convert G-code coordinates into pixels
sub to_pixels {
    my ($self, $point) = @_;
    
    my $p = Slic3r::Pointf->new(@$point);
    $p->scale($self->_scale_factor);
    $p->translate(@{$self->_shift});
    return [$p->x, $self->GetSize->GetHeight - $p->y]; #]]
}

# convert pixels into G-code coordinates
sub to_units {
    my ($self, $point) = @_;
    
    my $p = Slic3r::Pointf->new(
        $point->[X],
        $self->GetSize->GetHeight - $point->[Y],
    );
    $p->translate(@{$self->_shift->negative});
    $p->scale(1/$self->_scale_factor);
    return $p;
}

sub set_pos {
    my ($self, $pos) = @_;
    
    $self->pos($pos);
    $self->Refresh;
}

1;
