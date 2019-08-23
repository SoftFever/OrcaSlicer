#!/usr/bin/perl

print "This script is currently used for installing Perl dependenices for running\n";
print "the libslic3r unit / integration tests through Perl prove.\n";
print "If you don't plan to run the unit / integration tests, you don't need to\n";
print "install these dependencies to build and run PrusaSlicer.\n";

use strict;
use warnings;

use Config;
use File::Spec;

my %prereqs = qw(
    Devel::CheckLib                 0
    ExtUtils::MakeMaker             6.80
    ExtUtils::ParseXS               3.22
    ExtUtils::XSpp                  0
    ExtUtils::XSpp::Cmd             0
    ExtUtils::CppGuess              0
    ExtUtils::Typemaps              0
    ExtUtils::Typemaps::Basic       0
    File::Basename                  0
    File::Spec                      0
    Getopt::Long                    0
    Module::Build::WithXSpp         0.14
    Moo                             1.003001
    POSIX                           0
    Scalar::Util                    0
    Test::More                      0
    IO::Scalar                      0
    Time::HiRes                     0
);
my %recommends = qw(
    Class::XSAccessor               0
    Test::Harness                   0
);

my $sudo    = grep { $_ eq '--sudo' } @ARGV;
my $nolocal = grep { $_ eq '--nolocal' }  @ARGV;

my @missing_prereqs = ();
if ($ENV{SLIC3R_NO_AUTO}) {
    foreach my $module (sort keys %prereqs) {
        my $version = $prereqs{$module};
        next if eval "use $module $version; 1";
        push @missing_prereqs, $module if exists $prereqs{$module};
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
    my @cpanm_args = ();
    push @cpanm_args, "--sudo" if $sudo;

    # install local::lib without --local-lib otherwise it's not usable afterwards
    if (!eval "use local::lib qw(local-lib); 1") {
        my $res = system $cpanm, @cpanm_args, 'local::lib';
        warn "Warning: local::lib is required. You might need to run the `cpanm --sudo local::lib` command in order to install it.\n"
            if $res != 0;
    }
    
    push @cpanm_args, ('--local-lib', 'local-lib') if ! $nolocal;

    # make sure our cpanm is updated (old ones don't support the ~ syntax)
    system $cpanm, @cpanm_args, 'App::cpanminus';
    
    my %modules = (%prereqs, %recommends);
    foreach my $module (sort keys %modules) {
        my $version = $modules{$module};
        my @cmd = ($cpanm, @cpanm_args);
        
        # temporary workaround for upstream bug in test
        push @cmd, '--notest'
            if $module =~ /^(?:OpenGL|Test::Harness)$/;
        
        push @cmd, "$module~$version";

        my $res = system @cmd;
        if ($res != 0) {
            if (exists $prereqs{$module}) {
                push @missing_prereqs, $module;
            } else {
                printf "Don't worry, this module is optional.\n";
            }
        }
    }
}

print "\n";
print "In the next step, you need to build the PrusaSlicer C++ library.\n";
print "1) Create a build directory and change to it\n";
print "2) run cmake .. -DCMAKE_BUILD_TYPE=Release\n";
print "3) run make\n";
print "4) to execute the automatic tests, run ctest --verbose\n";
__END__
