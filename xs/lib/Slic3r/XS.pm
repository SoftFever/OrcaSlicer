package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

package Slic3r::Point::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

package Slic3r::ExPolygon::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

sub clone { (ref $_[0])->_clone($_[0]) }

# to handle legacy code
sub rotate {
    my $self = shift;
    my ($angle, $center) = @_;
    
    $center = Slic3r::Point::XS->new(@$center) if ref($center) ne 'Slic3r::Point::XS';
    $self->_rotate($angle, $center);
}

package Slic3r::ExPolygon::Collection;
use overload
    '@{}' => sub { $_[0]->arrayref };

sub clone { (ref $_[0])->_clone($_[0]) }

1;
