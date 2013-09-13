#!/usr/bin/perl -w

use strict;
use warnings;

use Module::Build::WithXSpp;

my $build = Module::Build::WithXSpp->new(
    module_name     => 'Slic3r::XS',
    dist_abstract   => 'XS code for Slic3r',
    build_requires => {qw(
        ExtUtils::ParseXS           3.18
        ExtUtils::Typemap           1.00
        ExtUtils::Typemaps::Default 1.03
        ExtUtils::XSpp              0.17
        Module::Build               0.3601
        Test::More                  0
    )},
    configure_requires => {qw(
        ExtUtils::CppGuess          0.07
        Module::Build               0.38
        Module::Build::WithXSpp     0.13
    )},
    # _GLIBCXX_USE_C99 : to get the long long type for g++
    # HAS_BOOL         : stops Perl/lib/CORE/handy.h from doing "#  define bool char" for MSVC
    # NOGDI            : prevents inclusion of wingdi.h which defines functions Polygon() and Polyline() in global namespace
    extra_compiler_flags => [qw(-D_GLIBCXX_USE_C99 -DHAS_BOOL -DNOGDI -DSLIC3RXS), ($ENV{SLIC3R_DEBUG} ? ' -DSLIC3R_DEBUG -g' : '')],
    
    # Provides extra C typemaps that are auto-merged
    extra_typemap_modules => {
        'ExtUtils::Typemaps::Default' => '1.03',
    },
    
    # for MSVC builds
    early_includes => [qw(
        cstring
        cstdlib
        ostream
    )]
);

$build->create_build_script;

__END__
