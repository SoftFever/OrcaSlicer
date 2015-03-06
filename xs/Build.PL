#!/usr/bin/perl -w

use strict;
use warnings;

use Devel::CheckLib;
use ExtUtils::CppGuess;
use Module::Build::WithXSpp;

# _GLIBCXX_USE_C99 : to get the long long type for g++
# HAS_BOOL         : stops Perl/lib/CORE/handy.h from doing "#  define bool char" for MSVC
# NOGDI            : prevents inclusion of wingdi.h which defines functions Polygon() and Polyline() in global namespace
my @cflags = qw(-D_GLIBCXX_USE_C99 -DHAS_BOOL -DNOGDI -DSLIC3RXS);

my @INC  = qw();
my @LIBS = qw();

if (defined $ENV{BOOST_DIR}) {
    if (-d "$ENV{BOOST_DIR}/include") {
        push @INC, '-I' . $ENV{BOOST_DIR} . '/include';
    } else {
        push @INC, '-I' . $ENV{BOOST_DIR};
    }
    push @LIBS, '-L' . $ENV{BOOST_DIR};
} else {
    push @INC, map "-I$_", grep { -d $_ }
        qw(/opt/local/include /usr/local/include /opt/include),
        qw(/usr/include C:/Boost/include);
    push @LIBS, map "-L$_", grep { -d $_ }
        qw(/opt/local/lib /usr/local/lib /opt/lib /usr/lib),
        qw(C:/Boost/lib /lib);
}

push @INC, '-Iinclude';

my @boost_libs = qw(thread system);
for my $pattern ('boost_%s', 'boost_%s-mt') {
    check_lib(
        lib     => sprintf($pattern, 'system'),
        INC     => join(' ', @INC),
        LIBS    => join(' ', @LIBS),
    ) or next;
    push @LIBS, map sprintf("-l$pattern", $_), @boost_libs;
    push @cflags, '-DBOOST_LIBS';
    last;
}
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
    extra_linker_flags => \@LIBS,
    
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
    )]
);

$build->create_build_script;

__END__
