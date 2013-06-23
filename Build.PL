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
);

# removed:
# Wx 0.9901

my @try = (
    $ENV{CPANM} // (),
    File::Spec->catfile($Config{sitebin}, 'cpanm'),
    File::Spec->catfile($Config{installscript}, 'cpanm'),
);

my $cpanm;
foreach my $path (@try) {
    if (-e $path) {  # don't use -x because it fails on Windows
        $cpanm = $path;
        last;
    }
}
if (!$cpanm) {
    if ($^O =~ /^(?:darwin|linux)$/ && system(qw(which cpanm)) == 0) {
        $cpanm = 'cpanm';
    }
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
    if ($ENV{SLIC3R_NO_AUTO}) {
        print "Missing prerequisite $module $version\n"
            if !eval "use $module $version; 1";
    } else {
        system $cpanm, "$module~$version";
    }
}
exit if $ENV{SLIC3R_NO_AUTO};

if (eval "use App::Prove; 1") {
    App::Prove->new->run;
}

__END__
