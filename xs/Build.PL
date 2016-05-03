#!/usr/bin/perl -w

use strict;
use warnings;

use Devel::CheckLib;
use ExtUtils::CppGuess;
use Module::Build::WithXSpp;

# _GLIBCXX_USE_C99 : to get the long long type for g++
# HAS_BOOL         : stops Perl/lib/CORE/handy.h from doing "#  define bool char" for MSVC
# NOGDI            : prevents inclusion of wingdi.h which defines functions Polygon() and Polyline() in global namespace
# BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X: https://svn.boost.org/trac/boost/ticket/5339
my @cflags = qw(-D_GLIBCXX_USE_C99 -DHAS_BOOL -DNOGDI -DSLIC3RXS -DBOOST_ASIO_DISABLE_KQUEUE);
my @ldflags = ();
if ($^O eq 'darwin') {
    push @ldflags, qw(-framework IOKit -framework CoreFoundation);
}

my @INC  = qw(-Isrc/libslic3r);
my @LIBS = qw(-Lsrc/libslic3r);

# search for Boost in a number of places
my @boost_include = my @boost_libs = ();
if (defined $ENV{BOOST_DIR}) {
    if (-d "$ENV{BOOST_DIR}/include") {
        push @boost_include, $ENV{BOOST_DIR} . '/include';
    } else {
        push @boost_include, $ENV{BOOST_DIR};
    }
    push @boost_libs, $ENV{BOOST_DIR};
} else {
    push @boost_include, grep { -d $_ }
        qw(/opt/local/include /usr/local/include /opt/include),
        qw(/usr/include C:\Boost\include);
    push @boost_libs, grep { -d $_ }
        qw(/opt/local/lib /usr/local/lib /opt/lib /usr/lib),
        qw(C:\Boost\lib /lib);
    
    if ($^O eq 'MSWin32') {
        for my $path (glob('C:\dev\boost*'), glob ('C:\boost*'), glob ('d:\src\boost*')) {
            push @boost_include, $path;
            push @boost_libs, $path . "/stage/lib";
        }
    }
}

# In order to generate the -l switches we need to know how Boost libraries are named
my $have_boost = 0;
my @boost_libraries = qw(system thread);  # we need these

# check without explicit lib path (works on Linux)
$have_boost = 1
    if check_lib(
        lib     => [ map "boost_${_}", @boost_libraries ],
    );

if (!$ENV{SLIC3R_STATIC} && $have_boost) {
    push @LIBS, map "-lboost_${_}", @boost_libraries;
} else {
    foreach my $path (@boost_libs) {
        my @files = glob "$path/libboost_system*";
        next if !@files;
    
        if ($files[0] =~ /libboost_system([^.]+)/) {
            my $suffix = $1;
            check_lib(
                lib     => [ map "boost_${_}${suffix}", @boost_libraries ],
                INC     => join(' ', map "-I$_", @INC,  @boost_include),
                LIBS    => "-L$path",
            ) or next;
        
            push @INC, (map " -I$_", @boost_include);  # TODO: only use the one related to the chosen lib path
            if ($ENV{SLIC3R_STATIC}) {
                push @LIBS, map "$path/libboost_$_$suffix.a", @boost_libraries;
            } else {
                push @LIBS, " -L$path", (map " -lboost_$_$suffix", @boost_libraries);
            }
            $have_boost = 1;
            last;
        }
    }
}
push @cflags, '-DBOOST_LIBS' if $have_boost;
die <<'EOF' if !$have_boost;
Slic3r requires the Boost libraries. Please make sure they are installed.

If they are installed, this script should be able to locate them in several
standard locations. If this is not the case, you might want to supply their 
path through the BOOST_DIR environment variable:

    BOOST_DIR=/path/to/boost perl Build.PL

EOF

if ($ENV{SLIC3R_DEBUG}) {
    # only on newer GCCs: -ftemplate-backtrace-limit=0
    push @cflags, qw(-DSLIC3R_DEBUG -g);
}
if (ExtUtils::CppGuess->new->is_gcc) {
    # check whether we're dealing with a buggy GCC version
    # see https://github.com/alexrj/Slic3r/issues/1965
    if (`cc --version` =~ / 4\.7\.[012]/) {
        # Workaround suggested by Boost devs:
        # https://svn.boost.org/trac/boost/ticket/8695
        push @cflags, qw(-fno-inline-small-functions);
    }
}

my $build = Module::Build::WithXSpp->new(
    module_name     => 'Slic3r::XS',
    dist_abstract   => 'XS code for Slic3r',
    build_requires => {qw(
        ExtUtils::ParseXS           3.18
        ExtUtils::Typemaps          1.00
        ExtUtils::Typemaps::Default 1.05
        ExtUtils::XSpp              0.17
        Module::Build               0.3601
        Test::More                  0
    )},
    configure_requires => {qw(
        ExtUtils::CppGuess          0.07
        Module::Build               0.38
        Module::Build::WithXSpp     0.13
    )},
    extra_compiler_flags => [ @INC, @cflags ],
    extra_linker_flags => [ @LIBS, @ldflags ],
    
    # Provides extra C typemaps that are auto-merged
    extra_typemap_modules => {
        'ExtUtils::Typemaps::Basic' => '1.05',
    },
    
    # for MSVC builds
    early_includes => [qw(
        cstring
        cstdlib
        ostream
        sstream
        libslic3r/GCodeSender.hpp
    )]
);

$build->create_build_script;

__END__
