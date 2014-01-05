package Slic3r::Config;
use strict;
use warnings;
use utf8;

use List::Util qw(first);

# cemetery of old config settings
our @Ignore = qw(duplicate_x duplicate_y multiply_x multiply_y support_material_tool acceleration
    adjust_overhang_flow standby_temperature scale rotate duplicate duplicate_grid);

our $Options = print_config_def();

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
        if ($opt_key =~ /^(?:print_center|bed_size|duplicate_grid|extruder_offset|retract_layer_change|wipe)$/) {
            $self->set_deserialize($opt_key, $args{$opt_key});
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
    my $config = $class->new;
    foreach my $opt_key (keys %{$ini->{_}}) {
        ($opt_key, my $value) = _handle_legacy($opt_key, $ini->{_}{$opt_key});
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
    return ($opt_key, $value) if first { $_ eq $opt_key } @Ignore;
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

sub set_ifndef {
    my $self = shift;
    my ($opt_key, $value, $deserialize) = @_;
    
    if (!$self->has($opt_key)) {
        if ($deserialize) {
            $self->set_deserialize($opt_key, $value);
        } else {
            $self->set($opt_key, $value);
        }
    }
}

sub save {
    my $self = shift;
    my ($file) = @_;
    
    my $ini = { _ => {} };
    foreach my $opt_key (sort @{$self->get_keys}) {
        next if $Options->{$opt_key}{shortcut};
        $ini->{_}{$opt_key} = $self->serialize($opt_key);
    }
    __PACKAGE__->write_ini($file, $ini);
}

sub setenv {
    my $self = shift;
    
    foreach my $opt_key (sort keys %$Options) {
        $ENV{"SLIC3R_" . uc $opt_key} = $self->serialize($opt_key);
    }
}

sub equals {
    my ($self, $other) = @_;
    return @{ $self->diff($other) } == 0;
}

# this will *ignore* options not present in both configs
sub diff {
    my ($self, $other) = @_;
    
    my @diff = ();
    foreach my $opt_key (sort @{$self->get_keys}) {
        push @diff, $opt_key
            if $other->has($opt_key) && $other->serialize($opt_key) ne $self->serialize($opt_key);
    }
    return [@diff];
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
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if grep $_ < 1, @{$self->filament_diameter};
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if grep $_ < 0, @{$self->nozzle_diameter};
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if grep $self->layer_height > $_, @{$self->nozzle_diameter};
    die "First layer height can't be greater than --nozzle-diameter\n"
        if grep $self->get_value('first_layer_height') > $_, @{$self->nozzle_diameter};
    
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
        if $self->use_firmware_retraction && $self->gcode_flavor ne 'reprap';
    
    die "--use-firmware-retraction is not compatible with --wipe\n"
        if $self->use_firmware_retraction && first {$_} @{$self->wipe};
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $self->print_center 
            && (!$self->print_center || $self->print_center !~ /^\d+,\d+$/);
    
    # --fill-pattern
    die "Invalid value for --fill-pattern\n"
        if !first { $_ eq $self->fill_pattern } @{$Options->{fill_pattern}{values}};
    
    # --solid-fill-pattern
    die "Invalid value for --solid-fill-pattern\n"
        if !first { $_ eq $self->solid_fill_pattern } @{$Options->{solid_fill_pattern}{values}};
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $self->fill_density < 0 || $self->fill_density > 1;
    die "The selected fill pattern is not supposed to work at 100% density\n"
        if $self->fill_density == 1
            && !first { $_ eq $self->fill_pattern } @{$Options->{solid_fill_pattern}{values}};
    
    # --infill-every-layers
    die "Invalid value for --infill-every-layers\n"
        if $self->infill_every_layers !~ /^\d+$/ || $self->infill_every_layers < 1;
    
    # --bed-size
    die "Invalid value for --bed-size\n"
        if !ref $self->bed_size 
            && (!$self->bed_size || $self->bed_size !~ /^\d+,\d+$/);
    
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
        
        die "Spiral vase mode is not compatible with non-zero fill density\n"
            if $self->fill_density > 0;
        
        die "Spiral vase mode is not compatible with top solid layers\n"
            if $self->top_solid_layers > 0;
        
        die "Spiral vase mode is not compatible with support material\n"
            if $self->support_material || $self->support_material_enforce_layers > 0;
        
        # This should be enforce automatically only on spiral layers and
        # done on the others
        die "Spiral vase mode is not compatible with retraction on layer change\n"
            if defined first { $_ } @{ $self->retract_layer_change };
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
            if ($type eq 'i' || $type eq 'f') {
                die "Invalid value for $opt_key\n"
                    if ($type eq 'i' && $value !~ /^-?\d+$/)
                    || ($type eq 'f' && $value !~ /^-?(?:\d+|\d*\.\d+)$/)
                    || (defined $opt->{min} && $value < $opt->{min})
                    || (defined $opt->{max} && $value > $opt->{max});
            } elsif ($type eq 's' && $opt->{type} eq 'select') {
                die "Invalid value for $opt_key\n"
                    unless first { $_ eq $value } @{ $opt->{values} };
            }
        }
    }
}

sub replace_options {
    my $self = shift;
    my ($string, $more_variables) = @_;
    
    $more_variables ||= {};
    $more_variables->{$_} = $ENV{$_} for grep /^SLIC3R_/, keys %ENV;
    {
        my $variables_regex = join '|', keys %$more_variables;
        $string =~ s/\[($variables_regex)\]/$more_variables->{$1}/eg;
    }
    
    my @lt = localtime; $lt[5] += 1900; $lt[4] += 1;
    $string =~ s/\[timestamp\]/sprintf '%04d%02d%02d-%02d%02d%02d', @lt[5,4,3,2,1,0]/egx;
    $string =~ s/\[year\]/$lt[5]/eg;
    $string =~ s/\[month\]/$lt[4]/eg;
    $string =~ s/\[day\]/$lt[3]/eg;
    $string =~ s/\[hour\]/$lt[2]/eg;
    $string =~ s/\[minute\]/$lt[1]/eg;
    $string =~ s/\[second\]/$lt[0]/eg;
    $string =~ s/\[version\]/$Slic3r::VERSION/eg;
    
    # build a regexp to match the available options
    my @options = grep !$Slic3r::Config::Options->{$_}{multiline},
        grep $self->has($_),
        keys %{$Slic3r::Config::Options};
    my $options_regex = join '|', @options;
    
    # use that regexp to search and replace option names with option values
    $string =~ s/\[($options_regex)\]/$self->serialize($1)/eg;
    foreach my $opt_key (grep ref $self->$_ eq 'ARRAY', @options) {
        my $value = $self->$opt_key;
        $string =~ s/\[${opt_key}_${_}\]/$value->[$_]/eg for 0 .. $#$value;
        if ($Options->{$opt_key}{type} eq 'point') {
            $string =~ s/\[${opt_key}_X\]/$value->[0]/eg;
            $string =~ s/\[${opt_key}_Y\]/$value->[1]/eg;
        }
    }
    return $string;
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
    foreach my $category (sort keys %$ini) {
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
    Slic3r::open(\my $fh, '<', $file);
    binmode $fh, ':utf8';
    
    my $ini = { _ => {} };
    my $category = '_';
    while (<$fh>) {
        s/\R+$//;
        next if /^\s+/;
        next if /^$/;
        next if /^\s*#/;
        if (/^\[(\w+)\]$/) {
            $category = $1;
            next;
        }
        /^(\w+) = (.*)/ or die "Unreadable configuration file (invalid data at line $.)\n";
        $ini->{$category}{$1} = $2;
    }
    close $fh;
    
    return $ini;
}

package Slic3r::Config::Print;
use parent 'Slic3r::Config';

package Slic3r::Config::PrintObject;
use parent 'Slic3r::Config';

package Slic3r::Config::PrintRegion;
use parent 'Slic3r::Config';

package Slic3r::Config::Full;
use parent 'Slic3r::Config';

1;
