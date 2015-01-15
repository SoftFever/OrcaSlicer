package Slic3r::Point;
use strict;
use warnings;

sub new_scale {
    my $class = shift;
    return $class->new(map Slic3r::Geometry::scale($_), @_);
}

sub dump_perl {
    my $self = shift;
    return sprintf "[%s,%s]", @$self;
}

package Slic3r::Pointf;
use strict;
use warnings;

sub new_unscale {
    my $class = shift;
    return $class->new(map Slic3r::Geometry::unscale($_), @_);
}

package Slic3r::Pointf3;
use strict;
use warnings;

sub new_unscale {
    my $class = shift;
    return $class->new(map Slic3r::Geometry::unscale($_), @_);
}

1;
