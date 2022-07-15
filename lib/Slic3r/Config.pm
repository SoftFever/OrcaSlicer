# Extends C++ class Slic3r::DynamicPrintConfig
# This perl class does not keep any perl class variables,
# all the storage is handled by the underlying C++ code.
package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first max);

# C++ Slic3r::PrintConfigDef exported as a Perl hash of hashes.
# The C++ counterpart is a constant singleton.
our $Options = print_config_def();

# Generate accessors.
{
    no strict 'refs';
    for my $opt_key (keys %$Options) {
        *{$opt_key} = sub { 
            #print "Slic3r::Config::accessor $opt_key\n"; 
            $_[0]->get($opt_key)
        };
    }
}

# From command line parameters, used by slic3r.pl
sub new_from_cli {
    my $class = shift;
    my %args = @_;
    
    # Delete hash keys with undefined value.
    delete $args{$_} for grep !defined $args{$_}, keys %args;
    
    # Replace the start_gcode, end_gcode ... hash values
    # with the content of the files they reference.
    for (qw(start end layer toolchange)) {
        my $opt_key = "${_}_gcode";
        if ($args{$opt_key}) {
            if (-e $args{$opt_key}) {
                Slic3r::open(\my $fh, "<", $args{$opt_key})
                    or die "Failed to open $args{$opt_key}\n";
                binmode $fh, ':utf8';
                $args{$opt_key} = do { local $/; <$fh> };
                close $fh;
            }
        }
    }

    my $self = $class->new;
    foreach my $opt_key (keys %args) {
        my $opt_def = $Options->{$opt_key};
        
        # we use set_deserialize() for bool options since GetOpt::Long doesn't handle 
        # arrays of boolean values
        if ($opt_key =~ /^(?:bed_shape|duplicate_grid|extruder_offset)$/ || $opt_def->{type} eq 'bool') {
            $self->set_deserialize($opt_key, $args{$opt_key});
        } elsif (my $shortcut = $opt_def->{shortcut}) {
            $self->set($_, $args{$opt_key}) for @$shortcut;
        } else {
            $self->set($opt_key, $args{$opt_key});
        }
    }
    
    return $self;
}

package Slic3r::Config::Static;
use parent 'Slic3r::Config';

sub Slic3r::Config::GCode::new { Slic3r::Config::Static::new_GCodeConfig }
sub Slic3r::Config::Print::new { Slic3r::Config::Static::new_PrintConfig }
sub Slic3r::Config::PrintObject::new { Slic3r::Config::Static::new_PrintObjectConfig }
sub Slic3r::Config::PrintRegion::new { Slic3r::Config::Static::new_PrintRegionConfig }
sub Slic3r::Config::Full::new { Slic3r::Config::Static::new_FullPrintConfig }

1;
