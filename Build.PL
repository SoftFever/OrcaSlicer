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
    Math::ConvexHull::MonotoneChain 0.01
    Math::Geometry::Voronoi         1.3
    Math::PlanePath                 53
    Moo                             1.003001
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
if (defined $ARGV[0] && $ARGV[0] eq '--gui') {
    %prereqs = qw(
    Wx                              0.9918
    );
    %recommends = qw(
    Wx::GLCanvas                    0
    OpenGL                          0
    );
}

my $missing_prereqs = 0;
if ($ENV{SLIC3R_NO_AUTO}) {
    foreach my $module (sort keys %prereqs) {
        my $version = $prereqs{$module};
        next if eval "use $module $version; 1";
        $missing_prereqs = 1 if exists $prereqs{$module};
        print "Missing prerequisite $module $version\n";
    }
    foreach my $module (sort keys %recommends) {
        my $version = $recommends{$module};
        next if eval "use $module $version; 1";
        print "Missing optional $module $version\n";
    }
} else {
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
    
    # make sure our cpanm is updated (old ones don't support the ~ syntax)
    system $cpanm, 'App::cpanminus';
    
    # install the Windows-compatible Math::Libm
    if ($^O eq 'MSWin32' && !eval "use Math::Libm; 1") {
        system $cpanm, 'https://github.com/alexrj/Math-Libm/tarball/master';
    }
    
    my %modules = (%prereqs, %recommends);
    foreach my $module (sort keys %modules) {
        my $version = $modules{$module};
        my $res = system $cpanm, "$module~$version";
        if ($res != 0) {
            if (exists $prereqs{$module}) {
                $missing_prereqs = 1;
            } else {
                printf "Don't worry, this module is optional.\n";
            }
        }
    }
    
    # temporarily require this dev version until this upstream bug
    # is resolved: https://rt.cpan.org/Ticket/Display.html?id=86367
    system $cpanm, 'SMUELLER/ExtUtils-ParseXS-3.18_04.tar.gz';
    
    # clean xs directory before reinstalling, to make sure Build is called
    # with current perl binary
    if (-e './xs/Build') {
        if ($^O eq 'MSWin32') {
            system 'cd', 'xs';
            system 'Build', 'distclean';
            system 'cd', '..';
        } else {
            system './xs/Build', 'distclean';
        }
    }
    system $cpanm, '--reinstall', './xs';
}

if (eval "use App::Prove; 1" && !$missing_prereqs) {
    App::Prove->new->run;
}

exit 1 if $missing_prereqs;

__END__
