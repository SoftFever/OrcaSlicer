#!/usr/bin/perl

use strict;
use warnings;

use Config;
use File::Spec;

my %prereqs = qw(
    Boost::Geometry::Utils          0.15
    Encode::Locale                  0
    File::Basename                  0
    File::Spec                      0
    Getopt::Long                    0
    Math::Clipper                   1.22
    Math::ConvexHull::MonotoneChain 0.01
    Math::Geometry::Voronoi         1.3
    Math::PlanePath                 53
    Moo                             0.091009
    Scalar::Util                    0
    Storable                        0
    Test::More                      0
    IO::Scalar                      0
    Time::HiRes                     0
);
my %recommends = qw(
    Class::XSAccessor               0
    Growl::GNTP                     0.15
    XML::SAX::ExpatXS               0
    Wx                              0.9901
);

my $cpanm;
if (defined $ENV{CPANM} && -x $ENV{CPANM}) {
    $cpanm = $ENV{CPANM};
} elsif (-x (my $c = File::Spec->catfile($Config{installscript}, 'cpanm'))) {
    $cpanm = $c;
} elsif ($^O =~ /^(?:darwin|linux)$/ && system(qw(which cpanm)) == 0) {
    $cpanm = 'cpanm';
}
die <<'EOF'
cpanm was not found. Please install it before running this script.

There are several ways to install cpanm, try one of these:

    apt-get install cpanminus
    curl -L http://cpanmin.us | perl - --sudo App::cpanminus
    cpan App::cpanminus

If it is installed in a non-standard location you can do:
    
    CPANM=/path/to/cpanm perl Build.PL

EOF
    if !$cpanm;

my %modules = (%prereqs, %recommends);
foreach my $module (sort keys %modules) {
    my $version = $modules{$module};
    system $cpanm, '--sudo', "$module~$version";
}

if (eval "use App::Prove; 1") {
    App::Prove->new->run;
}

__END__
