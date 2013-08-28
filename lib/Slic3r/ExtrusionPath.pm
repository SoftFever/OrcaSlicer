package Slic3r::ExtrusionPath;
use Moo;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(EXTR_ROLE_PERIMETER EXTR_ROLE_EXTERNAL_PERIMETER 
    EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER EXTR_ROLE_OVERHANG_PERIMETER
    EXTR_ROLE_FILL EXTR_ROLE_SOLIDFILL EXTR_ROLE_TOPSOLIDFILL EXTR_ROLE_BRIDGE 
    EXTR_ROLE_INTERNALBRIDGE EXTR_ROLE_SKIRT EXTR_ROLE_SUPPORTMATERIAL EXTR_ROLE_GAPFILL);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI X Y epsilon deg2rad rotate_points);

# the underlying Slic3r::Polyline objects holds the geometry
has 'polyline' => (
    is          => 'rw',
    required    => 1,
    handles     => [qw(merge_continuous_lines lines length reverse clip_end)],
);

# height is the vertical thickness of the extrusion expressed in mm
has 'height'       => (is => 'rw');
has 'flow_spacing' => (is => 'rw', required => 1);
has 'role'         => (is => 'rw', required => 1);

use constant EXTR_ROLE_PERIMETER                    => 0;
use constant EXTR_ROLE_EXTERNAL_PERIMETER           => 1;
use constant EXTR_ROLE_OVERHANG_PERIMETER           => 2;
use constant EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER   => 3;
use constant EXTR_ROLE_FILL                         => 4;
use constant EXTR_ROLE_SOLIDFILL                    => 5;
use constant EXTR_ROLE_TOPSOLIDFILL                 => 6;
use constant EXTR_ROLE_BRIDGE                       => 7;
use constant EXTR_ROLE_INTERNALBRIDGE               => 8;
use constant EXTR_ROLE_SKIRT                        => 9;
use constant EXTR_ROLE_SUPPORTMATERIAL              => 10;
use constant EXTR_ROLE_GAPFILL                      => 11;

use constant PACK_FMT => 'ffca*';

# class or object method
sub pack {
    my $self = shift;
    my %args = @_;
    
    if (ref $self) {
        %args = map { $_ => $self->$_ } qw(height flow_spacing role polyline);
    }
    
    my $o = \ pack PACK_FMT,
        $args{height}       // -1,
        $args{flow_spacing} || -1,
        $args{role}         // (die "Missing mandatory attribute 'role'"), #/
        $args{polyline}->serialize;
    
    bless $o, 'Slic3r::ExtrusionPath::Packed';
    return $o;
}

# no-op, this allows to use both packed and non-packed objects in Collections
sub unpack { $_[0] }

sub clone {
    my $self = shift;
    my %p = @_;
    
    $p{polyline} ||= $self->polyline->clone;
    return (ref $self)->new(
        (map { $_ => $self->$_ } qw(polyline height flow_spacing role)),
        %p,
    );
}

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    return map $self->clone(polyline => $_),
        $self->polyline->clip_with_expolygon($expolygon);
}

sub intersect_expolygons {
    my $self = shift;
    my ($expolygons) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection($expolygons, [$self->polyline])};
}

sub subtract_expolygons {
    my $self = shift;
    my ($expolygons) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_linestring_multi_polygon_difference([$self->polyline], $expolygons)};
}

sub simplify {
    my $self = shift;
    $self->polyline($self->polyline->simplify(@_));
}

sub points {
    my $self = shift;
    return $self->polyline;
}

sub first_point {
    my $self = shift;
    return $self->polyline->[0];
}

sub last_point {
    my $self = shift;
    return $self->polyline->[-1];
}

sub is_perimeter {
    my $self = shift;
    return $self->role == EXTR_ROLE_PERIMETER
        || $self->role == EXTR_ROLE_EXTERNAL_PERIMETER
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER
        || $self->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER;
}

sub is_fill {
    my $self = shift;
    return $self->role == EXTR_ROLE_FILL
        || $self->role == EXTR_ROLE_SOLIDFILL
        || $self->role == EXTR_ROLE_TOPSOLIDFILL;
}

sub is_bridge {
    my $self = shift;
    return $self->role == EXTR_ROLE_BRIDGE
        || $self->role == EXTR_ROLE_INTERNALBRIDGE
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER;
}

sub split_at_acute_angles {
    my $self = shift;
    
    # calculate angle limit
    my $angle_limit = abs(Slic3r::Geometry::deg2rad(40));
    my @points = @{$self->p};
    
    my @paths = ();
    
    # take first two points
    my @p = splice @points, 0, 2;
    
    # loop until we have one spare point
    while (my $p3 = shift @points) {
        my $angle = abs(Slic3r::Geometry::angle3points($p[-1], $p[-2], $p3));
        $angle = 2*PI - $angle if $angle > PI;
        
        if ($angle < $angle_limit) {
            # if the angle between $p[-2], $p[-1], $p3 is too acute
            # then consider $p3 only as a starting point of a new
            # path and stop the current one as it is
            push @paths, $self->clone(polyline => Slic3r::Polyline->new(@p));
            @p = ($p3);
            push @p, grep $_, shift @points or last;
        } else {
            push @p, $p3;
        }
    }
    push @paths, $self->clone(polyline => Slic3r::Polyline->new(@p))
        if @p > 1;
    
    return @paths;
}

package Slic3r::ExtrusionPath::Packed;
sub unpack {
    my $self = shift;
    
    my ($height, $flow_spacing, $role, $polyline_s)
        = unpack Slic3r::ExtrusionPath::PACK_FMT, $$self;
    
    return Slic3r::ExtrusionPath->new(
        height          => ($height == -1) ? undef : $height,
        flow_spacing    => ($flow_spacing == -1) ? undef : $flow_spacing,
        role            => $role,
        polyline        => Slic3r::Polyline->deserialize($polyline_s),
    );
}

1;
