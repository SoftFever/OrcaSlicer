# Extends C++ class Slic3r::DynamicPrintConfig
# This perl class does not keep any perl class variables,
# all the storage is handled by the underlying C++ code.
package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first max);

# cemetery of old config settings
our @Ignore = qw(duplicate_x duplicate_y multiply_x multiply_y support_material_tool acceleration
    adjust_overhang_flow standby_temperature scale rotate duplicate duplicate_grid
    rotate scale duplicate_grid start_perimeters_at_concave_points start_perimeters_at_non_overhang
    randomize_start seal_position bed_size print_center g0 vibration_limit gcode_arcs pressure_advance);

# C++ Slic3r::PrintConfigDef exported as a Perl hash of hashes.
# The C++ counterpart is a constant singleton.
our $Options = print_config_def();

# overwrite the hard-coded readonly value (this information is not available in XS)
$Options->{threads}{readonly} = !$Slic3r::have_threads;

# generate accessors
{
    no strict 'refs';
    for my $opt_key (keys %$Options) {
        *{$opt_key} = sub { $_[0]->get($opt_key) };
    }
}

# Fill in the underlying C++ Slic3r::DynamicPrintConfig with the content of the defaults
# provided by the C++ class Slic3r::FullPrintConfig.
# Used by the UI.
sub new_from_defaults {
    my ($class, @opt_keys) = @_;
    my $self = $class->new;
    # Instantiating the C++ class Slic3r::FullPrintConfig.
    my $defaults = Slic3r::Config::Full->new;
    if (@opt_keys) {
        $self->set($_, $defaults->get($_))
            for grep $defaults->has($_), @opt_keys;
    } else {
        $self->apply_static($defaults);
    }
    return $self;
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

sub merge {
    my $class = shift;
    my $config = $class->new;
    $config->apply($_) for @_;
    return $config;
}

# Load a flat ini file without a category into the underlying C++ Slic3r::DynamicConfig class,
# convert legacy configuration names.
sub load {
    my ($class, $file) = @_;
    # Instead of using the /i modifier for case-insensitive matching, the case insensitivity is expressed
    # explicitely to avoid having to bundle the UTF8 Perl library.
    if ($file =~ /\.[gG][cC][oO][dD][eE]/ || $file =~ /\.[gG]/) {
        my $config = $class->new;
        $config->_load_from_gcode($file);
        return $config;
    } else {
        my $config = $class->new;
        $config->_load($file);
        return $config;
    }
}

# Deserialize a perl hash into the underlying C++ Slic3r::DynamicConfig class,
# convert legacy configuration names.
# Used to load a config bundle.
sub load_ini_hash {
    my ($class, $ini_hash) = @_;    
    my $config = $class->new;
    $config->set_deserialize($_, $ini_hash->{$_}) for keys %$ini_hash;
    return $config;
}

sub clone {
    my $self = shift;
    my $new = (ref $self)->new;
    $new->apply($self);
    return $new;
}

sub get_value {
    my ($self, $opt_key) = @_;
    return $Options->{$opt_key}{ratio_over}
        ? $self->get_abs_value($opt_key)
        : $self->get($opt_key);
}

# Create a hash of hashes from the underlying C++ Slic3r::DynamicPrintConfig.
# The first hash key is '_' meaning no category.
# Used to create a config bundle.
sub as_ini {
    my ($self) = @_;
    my $ini = { _ => {} };
    foreach my $opt_key (sort @{$self->get_keys}) {
        next if $Options->{$opt_key}{shortcut};
        $ini->{_}{$opt_key} = $self->serialize($opt_key);
    }
    return $ini;
}

# this method is idempotent by design and only applies to ::DynamicConfig or ::Full
# objects because it performs cross checks
sub validate {
    my $self = shift;
    
    # -j, --threads
    die "Invalid value for --threads\n"
        if $self->threads < 1;

    # --layer-height
    die "Invalid value for --layer-height\n"
        if $self->layer_height <= 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $self->layer_height / &Slic3r::SCALING_FACTOR % 1 != 0;
    
    # --first-layer-height
    die "Invalid value for --first-layer-height\n"
        if $self->first_layer_height !~ /^(?:\d*(?:\.\d+)?)%?$/;
    die "Invalid value for --first-layer-height\n"
        if $self->get_value('first_layer_height') <= 0;
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if grep $_ < 1, @{$self->filament_diameter};
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if grep $_ < 0, @{$self->nozzle_diameter};
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $self->perimeters < 0;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n" if defined $self->solid_layers && $self->solid_layers < 0;
    die "Invalid value for --top-solid-layers\n"    if $self->top_solid_layers      < 0;
    die "Invalid value for --bottom-solid-layers\n" if $self->bottom_solid_layers   < 0;
    
    # --gcode-flavor
    die "Invalid value for --gcode-flavor\n"
        if !first { $_ eq $self->gcode_flavor } @{$Options->{gcode_flavor}{values}};
    
    die "--use-firmware-retraction is only supported by Marlin, Smoothie, Repetier and Machinekit firmware\n"
        if $self->use_firmware_retraction && $self->gcode_flavor ne 'smoothie' 
        && $self->gcode_flavor ne 'reprap' 
        && $self->gcode_flavor ne 'machinekit' 
        && $self->gcode_flavor ne 'repetier';
    
    die "--use-firmware-retraction is not compatible with --wipe\n"
        if $self->use_firmware_retraction && first {$_} @{$self->wipe};
    
    # --fill-pattern
    die "Invalid value for --fill-pattern\n"
        if !first { $_ eq $self->fill_pattern } @{$Options->{fill_pattern}{values}};
    
    # --external-fill-pattern
    die "Invalid value for --external-fill-pattern\n"
        if !first { $_ eq $self->external_fill_pattern } @{$Options->{external_fill_pattern}{values}};
    
    # --fill-density
    die "The selected fill pattern is not supposed to work at 100% density\n"
        if $self->fill_density == 100
            && !first { $_ eq $self->fill_pattern } @{$Options->{external_fill_pattern}{values}};
    
    # --infill-every-layers
    die "Invalid value for --infill-every-layers\n"
        if $self->infill_every_layers !~ /^\d+$/ || $self->infill_every_layers < 1;
    
    # --skirt-height
    die "Invalid value for --skirt-height\n"
        if $self->skirt_height < -1;  # -1 means as tall as the object
    
    # --bridge-flow-ratio
    die "Invalid value for --bridge-flow-ratio\n"
        if $self->bridge_flow_ratio <= 0;
    
    # extruder clearance
    die "Invalid value for --extruder-clearance-radius\n"
        if $self->extruder_clearance_radius <= 0;
    die "Invalid value for --extruder-clearance-height\n"
        if $self->extruder_clearance_height <= 0;
    
    # --extrusion-multiplier
    die "Invalid value for --extrusion-multiplier\n"
        if defined first { $_ <= 0 } @{$self->extrusion_multiplier};
    
    # --default-acceleration
    die "Invalid zero value for --default-acceleration when using other acceleration settings\n"
        if ($self->perimeter_acceleration || $self->infill_acceleration || $self->bridge_acceleration || $self->first_layer_acceleration)
            && !$self->default_acceleration;
    
    # --spiral-vase
    if ($self->spiral_vase) {
        # Note that we might want to have more than one perimeter on the bottom
        # solid layers.
        die "Can't make more than one perimeter when spiral vase mode is enabled\n"
            if $self->perimeters > 1;
        
        die "Can't make less than one perimeter when spiral vase mode is enabled\n"
            if $self->perimeters < 1;
        
        die "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0\n"
            if $self->fill_density > 0;
        
        die "Spiral vase mode is not compatible with top solid layers\n"
            if $self->top_solid_layers > 0;
        
        die "Spiral vase mode is not compatible with support material\n"
            if $self->support_material || $self->support_material_enforce_layers > 0;
    }
    
    # extrusion widths
    {
        my $max_nozzle_diameter = max(@{ $self->nozzle_diameter });
        die "Invalid extrusion width (too large)\n"
            if defined first { $_ > 10 * $max_nozzle_diameter }
                map $self->get_abs_value_over("${_}_extrusion_width", $max_nozzle_diameter),
                qw(perimeter infill solid_infill top_infill support_material first_layer);
    }
    
    # general validation, quick and dirty
    foreach my $opt_key (@{$self->get_keys}) {
        my $opt = $Options->{$opt_key};
        next unless defined $self->$opt_key;
        next unless defined $opt->{cli} && $opt->{cli} =~ /=(.+)$/;
        my $type = $1;
        my @values = ();
        if ($type =~ s/\@$//) {
            die "Invalid value for $opt_key\n" if ref($self->$opt_key) ne 'ARRAY';
            @values = @{ $self->$opt_key };
        } else {
            @values = ($self->$opt_key);
        }
        foreach my $value (@values) {
            if ($type eq 'i' || $type eq 'f' || $opt->{type} eq 'percent') {
                $value =~ s/%$// if $opt->{type} eq 'percent';
                die "Invalid value for $opt_key\n"
                    if ($type eq 'i' && $value !~ /^-?\d+$/)
                    || (($type eq 'f' || $opt->{type} eq 'percent') && $value !~ /^-?(?:\d+|\d*\.\d+)$/)
                    || (defined $opt->{min} && $value < $opt->{min})
                    || (defined $opt->{max} && $value > $opt->{max});
            } elsif ($type eq 's' && $opt->{type} eq 'select') {
                die "Invalid value for $opt_key\n"
                    unless first { $_ eq $value } @{ $opt->{values} };
            }
        }
    }
    
    return 1;
}

# CLASS METHODS:

# Write a "Windows" style ini file with categories enclosed in squre brackets.
# Used by config-bundle-to-config.pl and to save slic3r.ini.
sub write_ini {
    my $class = shift;
    my ($file, $ini) = @_;
    
    Slic3r::open(\my $fh, '>', $file);
    binmode $fh, ':utf8';
    my $localtime = localtime;
    printf $fh "# generated by Slic3r $Slic3r::VERSION on %s\n", "$localtime";
    # make sure the _ category is the first one written
    foreach my $category (sort { ($a eq '_') ? -1 : ($a cmp $b) } keys %$ini) {
        printf $fh "\n[%s]\n", $category if $category ne '_';
        foreach my $key (sort keys %{$ini->{$category}}) {
            printf $fh "%s = %s\n", $key, $ini->{$category}{$key};
        }
    }
    close $fh;
}

# Parse a "Windows" style ini file with categories enclosed in squre brackets.
# Returns a hash of hashes over strings.
# {category}{name}=value
# Non-categorized entries are stored under a category '_'.
# Used by config-bundle-to-config.pl and to read slic3r.ini.
sub read_ini {
    my $class = shift;
    my ($file) = @_;
    
    local $/ = "\n";
    Slic3r::open(\my $fh, '<', $file)
        or die "Unable to open $file: $!\n";
    binmode $fh, ':utf8';
    
    my $ini = { _ => {} };
    my $category = '_';
    while (<$fh>) {
        s/\R+$//;
        next if /^\s+/;
        next if /^$/;
        next if /^\s*#/;
        if (/^\[(.+?)\]$/) {
            $category = $1;
            next;
        }
        /^(\w+) *= *(.*)/ or die "Unreadable configuration file (invalid data at line $.)\n";
        $ini->{$category}{$1} = $2;
    }
    close $fh;
    
    return $ini;
}

package Slic3r::Config::Static;
use parent 'Slic3r::Config';

sub Slic3r::Config::GCode::new { Slic3r::Config::Static::new_GCodeConfig }
sub Slic3r::Config::Print::new { Slic3r::Config::Static::new_PrintConfig }
sub Slic3r::Config::PrintObject::new { Slic3r::Config::Static::new_PrintObjectConfig }
sub Slic3r::Config::PrintRegion::new { Slic3r::Config::Static::new_PrintRegionConfig }
sub Slic3r::Config::Full::new { Slic3r::Config::Static::new_FullPrintConfig }

1;
