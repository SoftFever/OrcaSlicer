package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first max);

# cemetery of old config settings
our @Ignore = qw(duplicate_x duplicate_y multiply_x multiply_y support_material_tool acceleration
    adjust_overhang_flow standby_temperature scale rotate duplicate duplicate_grid
    rotate scale duplicate_grid start_perimeters_at_concave_points start_perimeters_at_non_overhang
    randomize_start seal_position bed_size print_center g0);

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

sub new_from_defaults {
    my $class = shift;
    my (@opt_keys) = @_;
    
    my $self = $class->new;
    my $defaults = Slic3r::Config::Full->new;
    if (@opt_keys) {
        $self->set($_, $defaults->get($_)) for @opt_keys;
    } else {
        $self->apply_static($defaults);
    }
    return $self;
}

sub new_from_cli {
    my $class = shift;
    my %args = @_;
    
    delete $args{$_} for grep !defined $args{$_}, keys %args;
    
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

sub load {
    my $class = shift;
    my ($file) = @_;
    
    my $ini = __PACKAGE__->read_ini($file);
    return $class->load_ini_hash($ini->{_});
}

sub load_ini_hash {
    my $class = shift;
    my ($ini_hash) = @_;
    
    my $config = $class->new;
    foreach my $opt_key (keys %$ini_hash) {
        ($opt_key, my $value) = _handle_legacy($opt_key, $ini_hash->{$opt_key});
        next if !defined $opt_key;
        $config->set_deserialize($opt_key, $value);
    }
    return $config;
}

sub clone {
    my $self = shift;
    
    my $new = (ref $self)->new;
    $new->apply($self);
    return $new;
}

sub get_value {
    my $self = shift;
    my ($opt_key) = @_;
    
    return $Options->{$opt_key}{ratio_over}
        ? $self->get_abs_value($opt_key)
        : $self->get($opt_key);
}

sub _handle_legacy {
    my ($opt_key, $value) = @_;
    
    # handle legacy options
    if ($opt_key =~ /^(extrusion_width|bottom_layer_speed|first_layer_height)_ratio$/) {
        $opt_key = $1;
        $opt_key =~ s/^bottom_layer_speed$/first_layer_speed/;
        $value = $value =~ /^\d+(?:\.\d+)?$/ && $value != 0 ? ($value*100) . "%" : 0;
    }
    if ($opt_key eq 'threads' && !$Slic3r::have_threads) {
        $value = 1;
    }
    if ($opt_key eq 'gcode_flavor' && $value eq 'makerbot') {
        $value = 'makerware';
    }
    if ($opt_key eq 'fill_density' && defined($value) && $value !~ /%/ && $value <= 1) {
        # fill_density was turned into a percent value
        $value *= 100;
        $value = "$value";  # force update of the PV value, workaround for bug https://rt.cpan.org/Ticket/Display.html?id=94110
    }
    if ($opt_key eq 'randomize_start' && $value) {
        $opt_key = 'seam_position';
        $value = 'random';
    }
    if ($opt_key eq 'bed_size' && $value) {
        $opt_key = 'bed_shape';
        my ($x, $y) = split /,/, $value;
        $value = "0x0,${x}x0,${x}x${y},0x${y}";
    }
    return () if first { $_ eq $opt_key } @Ignore;
    
    # For historical reasons, the world's full of configs having these very low values;
    # to avoid unexpected behavior we need to ignore them.  Banning these two hard-coded
    # values is a dirty hack and will need to be removed sometime in the future, but it
    # will avoid lots of complaints for now.
    if ($opt_key eq 'perimeter_acceleration' && $value == '25') {
        $value = 0;
    }
    if ($opt_key eq 'infill_acceleration' && $value == '50') {
        $value = 0;
    }
    
    if (!exists $Options->{$opt_key}) {
        my @keys = grep { $Options->{$_}{aliases} && grep $_ eq $opt_key, @{$Options->{$_}{aliases}} } keys %$Options;
        if (!@keys) {
            warn "Unknown option $opt_key\n";
            return ();
        }
        $opt_key = $keys[0];
    }
    
    return ($opt_key, $value);
}

sub as_ini {
    my ($self) = @_;
    
    my $ini = { _ => {} };
    foreach my $opt_key (sort @{$self->get_keys}) {
        next if $Options->{$opt_key}{shortcut};
        $ini->{_}{$opt_key} = $self->serialize($opt_key);
    }
    return $ini;
}

sub save {
    my $self = shift;
    my ($file) = @_;
    
    __PACKAGE__->write_ini($file, $self->as_ini);
}

sub setenv {
    my $self = shift;
    
    foreach my $opt_key (@{$self->get_keys}) {
        $ENV{"SLIC3R_" . uc $opt_key} = $self->serialize($opt_key);
    }
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
    
    die "--use-firmware-retraction is only supported by Marlin firmware\n"
        if $self->use_firmware_retraction && $self->gcode_flavor ne 'reprap' && $self->gcode_flavor ne 'machinekit';
    
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
                map $self->get_abs_value_over("${_}_extrusion_width", $self->layer_height),
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

# min object distance is max(duplicate_distance, clearance_radius)
sub min_object_distance {
    my $self = shift;
    
    return ($self->complete_objects && $self->extruder_clearance_radius > $self->duplicate_distance)
        ? $self->extruder_clearance_radius
        : $self->duplicate_distance;
}

# CLASS METHODS:

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

package Slic3r::Config::GCode;
use parent 'Slic3r::Config';

package Slic3r::Config::Print;
use parent 'Slic3r::Config';

package Slic3r::Config::PrintObject;
use parent 'Slic3r::Config';

package Slic3r::Config::PrintRegion;
use parent 'Slic3r::Config';

package Slic3r::Config::Full;
use parent 'Slic3r::Config';

1;
