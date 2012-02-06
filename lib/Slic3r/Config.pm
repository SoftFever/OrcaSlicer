package Slic3r::Config;
use strict;
use warnings;
use utf8;

use constant PI => 4 * atan2(1, 1);

our $Options = {

    # miscellaneous options
    'notes' => {
        label   => 'Configuraton notes',
        cli		=> 'notes=s',
        type    => 's',
        multiline => 1,
        width   => 350,
        height  => 300,
        serialize   => sub { join '\n', split /\R/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
    },

    # output options
    'output_filename_format' => {
        label   => 'Output filename format',
        cli     => 'output-filename-format=s',
        type    => 's',
    },

    # printer options
    'nozzle_diameter' => {
        label   => 'Nozzle diameter',
        cli     => 'nozzle-diameter=f',
        type    => 'f',
        important => 1,
    },
    'print_center' => {
        label   => 'Print center',
        cli     => 'print-center=s',
        type    => 'point',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'use_relative_e_distances' => {
        label   => 'Use relative E distances',
        cli     => 'use-relative-e-distances',
        type    => 'bool',
    },
    'extrusion_axis' => {
        label   => 'Extrusion axis',
        cli     => 'extrusion-axis',
        type    => 's',
    },
    'z_offset' => {
        label   => 'Z offset',
        cli     => 'z-offset=f',
        type    => 'f',
    },
    'gcode_arcs' => {
        label   => 'Use native GCODE arcs',
        cli     => 'gcode-arcs',
        type    => 'bool',
    },
    'g0' => {
        label   => 'Use g0 for travel moves',
        cli     => 'g0',
        type    => 'bool',
    },
    'gcode_comments' => {
        label   => 'Verbose GCODE (comments)',
        cli     => 'gcode-comments',
        type    => 'bool',
    },
    
    # filament options
    'filament_diameter' => {
        label   => 'Diameter (mm)',
        cli     => 'filament-diameter=f',
        type    => 'f',
        important => 1,
    },
    'extrusion_multiplier' => {
        label   => 'Extrusion multiplier',
        cli     => 'extrusion-multiplier=f',
        type    => 'f',
        aliases => [qw(filament_packing_density)],
    },
    'temperature' => {
        label   => 'Temperature (°C)',
        cli     => 'temperature=i',
        type    => 'i',
        important => 1,
    },
    
    # speed options
    'travel_speed' => {
        label   => 'Travel (mm/s)',
        cli     => 'travel-speed=i',
        type    => 'f',
        aliases => [qw(travel_feed_rate)],
    },
    'perimeter_speed' => {
        label   => 'Perimeters (mm/s)',
        cli     => 'perimeter-speed=i',
        type    => 'f',
        aliases => [qw(perimeter_feed_rate)],
    },
    'small_perimeter_speed' => {
        label   => 'Small perimeters (mm/s)',
        cli     => 'small-perimeter-speed=i',
        type    => 'f',
    },
    'infill_speed' => {
        label   => 'Infill (mm/s)',
        cli     => 'infill-speed=i',
        type    => 'f',
        aliases => [qw(print_feed_rate infill_feed_rate)],
    },
    'solid_infill_speed' => {
        label   => 'Solid infill (mm/s)',
        cli     => 'solid-infill-speed=i',
        type    => 'f',
        aliases => [qw(solid_infill_feed_rate)],
    },
    'bridge_speed' => {
        label   => 'Bridges (mm/s)',
        cli     => 'bridge-speed=i',
        type    => 'f',
        aliases => [qw(bridge_feed_rate)],
    },
    'bottom_layer_speed_ratio' => {
        label   => 'Bottom layer speed ratio',
        cli     => 'bottom-layer-speed-ratio=f',
        type    => 'f',
    },
    
    # accuracy options
    'layer_height' => {
        label   => 'Layer height (mm)',
        cli     => 'layer-height=f',
        type    => 'f',
    },
    'first_layer_height_ratio' => {
        label   => 'First layer height ratio',
        cli     => 'first-layer-height-ratio=f',
        type    => 'f',
    },
    'infill_every_layers' => {
        label   => 'Infill every N layers',
        cli     => 'infill-every-layers=i',
        type    => 'i',
    },
    
    # flow options
    'extrusion_width_ratio' => {
        label   => 'Extrusion width (ratio over layer height; leave zero to calculate automatically)',
        cli     => 'extrusion-width-ratio=f',
        type    => 'f',
    },
    'bridge_flow_ratio' => {
        label   => 'Bridge flow ratio',
        cli     => 'bridge-flow-ratio=f',
        type    => 'f',
    },
    
    # print options
    'perimeters' => {
        label   => 'Perimeters',
        cli     => 'perimeters=i',
        type    => 'i',
        aliases => [qw(perimeter_offsets)],
    },
    'solid_layers' => {
        label   => 'Solid layers',
        cli     => 'solid-layers=i',
        type    => 'i',
    },
    'fill_pattern' => {
        label   => 'Fill pattern',
        cli     => 'fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear line concentric hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear line concentric), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
    },
    'solid_fill_pattern' => {
        label   => 'Solid fill pattern',
        cli     => 'solid-fill-pattern=s',
        type    => 'select',
        values  => [qw(rectilinear concentric hilbertcurve archimedeanchords octagramspiral)],
        labels  => [qw(rectilinear concentric), 'hilbertcurve (slow)', 'archimedeanchords (slow)', 'octagramspiral (slow)'],
    },
    'fill_density' => {
        label   => 'Fill density',
        cli     => 'fill-density=f',
        type    => 'f',
    },
    'fill_angle' => {
        label   => 'Fill angle (°)',
        cli     => 'fill-angle=i',
        type    => 'i',
    },
    'start_gcode' => {
        label   => 'Start GCODE',
        cli     => 'start-gcode=s',
        type    => 's',
        multiline => 1,
        width   => 350,
        height  => 150,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
    },
    'end_gcode' => {
        label   => 'End GCODE',
        cli     => 'end-gcode=s',
        type    => 's',
        multiline => 1,
        width   => 350,
        height  => 150,
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
    },
    
    # retraction options
    'retract_length' => {
        label   => 'Length (mm)',
        cli     => 'retract-length=f',
        type    => 'f',
    },
    'retract_speed' => {
        label   => 'Speed (mm/s)',
        cli     => 'retract-speed=i',
        type    => 'i',
    },
    'retract_restart_extra' => {
        label   => 'Extra length on restart (mm)',
        cli     => 'retract-restart-extra=f',
        type    => 'f',
    },
    'retract_before_travel' => {
        label   => 'Minimum travel after retraction (mm)',
        cli     => 'retract-before-travel=f',
        type    => 'f',
    },
    'retract_lift' => {
        label   => 'Lift Z (mm)',
        cli     => 'retract-lift=f',
        type    => 'f',
    },
    
    # skirt options
    'skirts' => {
        label   => 'Loops',
        cli     => 'skirts=i',
        type    => 'i',
    },
    'skirt_distance' => {
        label   => 'Distance from object (mm)',
        cli     => 'skirt-distance=i',
        type    => 'i',
    },
    'skirt_height' => {
        label   => 'Skirt height (layers)',
        cli     => 'skirt-height=i',
        type    => 'i',
    },
    
    # transform options
    'scale' => {
        label   => 'Scale',
        cli     => 'scale=f',
        type    => 'f',
    },
    'rotate' => {
        label   => 'Rotate (°)',
        cli     => 'rotate=i',
        type    => 'i',
    },
    'duplicate_x' => {
        label   => 'Copies along X',
        cli     => 'duplicate-x=i',
        type    => 'i',
        aliases => [qw(multiply_x)],
    },
    'duplicate_y' => {
        label   => 'Copies along Y',
        cli     => 'duplicate-y=i',
        type    => 'i',
        aliases => [qw(multiply_y)],
    },
    'duplicate_distance' => {
        label   => 'Distance between copies',
        cli     => 'duplicate-distance=i',
        type    => 'i',
        aliases => [qw(multiply_distance)],
    },
};

sub get {
    my $class = @_ == 2 ? shift : undef;
    my ($opt_key) = @_;
    no strict 'refs';
    return ${"Slic3r::$opt_key"};
}

sub set {
    my $class = @_ == 3 ? shift : undef;
    my ($opt_key, $value) = @_;
    no strict 'refs';
    ${"Slic3r::$opt_key"} = $value;
}

sub serialize {
    my $class = @_ == 2 ? shift : undef;
    my ($opt_key) = @_;
    return $Options->{$opt_key}{serialize}
        ? $Options->{$opt_key}{serialize}->(get($opt_key))
        : get($opt_key);
}

sub save {
    my $class = shift;
    my ($file) = @_;
    
    open my $fh, '>', $file;
    foreach my $opt (sort keys %$Options) {
        my $value = get($opt);
        $value = $Options->{$opt}{serialize}->($value) if $Options->{$opt}{serialize};
        printf $fh "%s = %s\n", $opt, $value;
    }
    close $fh;
}

sub load {
    my $class = shift;
    my ($file) = @_;
    
    local $/ = "\n";
    open my $fh, '<', $file;
    while (<$fh>) {
        s/\R+$//;
        next if /^\s+/;
        next if /^$/;
        next if /^\s*#/;
        /^(\w+) = (.*)/ or die "Unreadable configuration file (invalid data at line $.)\n";
        my $key = $1;
        if (!exists $Options->{$key}) {
            $key = +(grep { $Options->{$_}{aliases} && grep $_ eq $key, @{$Options->{$_}{aliases}} }
                keys %$Options)[0] or warn "Unknown option $1 at line $.\n";
        }
        next unless $key;
        my $opt = $Options->{$key};
        set($key, $opt->{deserialize} ? $opt->{deserialize}->($2) : $2);
    }
    close $fh;
}

sub validate_cli {
    my $class = shift;
    my ($opt) = @_;
    
    for (qw(start end)) {
        if (defined $opt->{$_."_gcode"}) {
            if ($opt->{$_."_gcode"} eq "") {
                set($_."_gcode", "");
            } else {
                die "Invalid value for --${_}-gcode: file does not exist"
                    if !-e $opt->{$_."_gcode"};
                open my $fh, "<", $opt->{$_."_gcode"};
                $opt->{$_."_gcode"} = do { local $/; <$fh> };
                close $fh;
            }
        }
    }
}

sub validate {
    my $class = shift;

    # --layer-height
    die "Invalid value for --layer-height\n"
        if $Slic3r::layer_height <= 0;
    die "--layer-height must be a multiple of print resolution\n"
        if $Slic3r::layer_height / $Slic3r::resolution % 1 != 0;
    
    # --first-layer-height-ratio
    die "Invalid value for --first-layer-height-ratio\n"
        if $Slic3r::first_layer_height_ratio < 0;
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if $Slic3r::filament_diameter < 1;
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if $Slic3r::nozzle_diameter < 0;
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if $Slic3r::layer_height > $Slic3r::nozzle_diameter;
    die "First layer height can't be greater than --nozzle-diameter\n"
        if ($Slic3r::layer_height * $Slic3r::first_layer_height_ratio) > $Slic3r::nozzle_diameter;
    
    if ($Slic3r::extrusion_width_ratio) {
        $Slic3r::flow_width = $Slic3r::layer_height * $Slic3r::extrusion_width_ratio;
    } else {
        # here we calculate a sane default by matching the flow speed (at the nozzle)
        # and the feed rate
        my $volume = ($Slic3r::nozzle_diameter**2) * PI/4;
        my $shape_threshold = $Slic3r::nozzle_diameter * $Slic3r::layer_height
            + ($Slic3r::layer_height**2) * PI/4;
        if ($volume >= $shape_threshold) {
            # rectangle with semicircles at the ends
            $Slic3r::flow_width = (($Slic3r::nozzle_diameter**2) * PI + ($Slic3r::layer_height**2) * (4 - PI)) / (4 * $Slic3r::layer_height);
        } else {
            # rectangle with squished semicircles at the ends
            $Slic3r::flow_width = $Slic3r::nozzle_diameter * ($Slic3r::nozzle_diameter/$Slic3r::layer_height - 4/PI + 1);
        }
        
        my $min_flow_width = $Slic3r::nozzle_diameter * 1.05;
        my $max_flow_width = $Slic3r::nozzle_diameter * 1.4;
        $Slic3r::flow_width = $max_flow_width if $Slic3r::flow_width > $max_flow_width;
        $Slic3r::flow_width = $min_flow_width if $Slic3r::flow_width < $min_flow_width;
    }
    
    if ($Slic3r::flow_width >= ($Slic3r::nozzle_diameter + $Slic3r::layer_height)) {
        # rectangle with semicircles at the ends
        $Slic3r::min_flow_spacing = $Slic3r::flow_width - $Slic3r::layer_height * (1 - PI/4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $Slic3r::min_flow_spacing = $Slic3r::flow_width * (1 - PI/4) + $Slic3r::nozzle_diameter * PI/4;
    }
    $Slic3r::flow_spacing = $Slic3r::flow_width - $Slic3r::overlap_factor * ($Slic3r::flow_width - $Slic3r::min_flow_spacing);
    
    Slic3r::debugf "Flow width = $Slic3r::flow_width\n";
    Slic3r::debugf "Flow spacing = $Slic3r::flow_spacing\n";
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $Slic3r::perimeters < 1;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n"
        if $Slic3r::solid_layers < 0;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $Slic3r::print_center 
            && (!$Slic3r::print_center || $Slic3r::print_center !~ /^\d+,\d+$/);
    $Slic3r::print_center = [ split /,/, $Slic3r::print_center ]
        if !ref $Slic3r::print_center;
    
    # --fill-pattern
    die "Invalid value for --fill-pattern\n"
        if !exists $Slic3r::Fill::FillTypes{$Slic3r::fill_pattern};
    
    # --solid-fill-pattern
    die "Invalid value for --solid-fill-pattern\n"
        if !exists $Slic3r::Fill::FillTypes{$Slic3r::solid_fill_pattern};
    
    # --fill-density
    die "Invalid value for --fill-density\n"
        if $Slic3r::fill_density < 0 || $Slic3r::fill_density > 1;
    
    # --infill-every-layers
    die "Invalid value for --infill-every-layers\n"
        if $Slic3r::infill_every_layers !~ /^\d+$/ || $Slic3r::infill_every_layers < 1;
    die "Maximum infill thickness can't exceed nozzle diameter\n"
        if $Slic3r::infill_every_layers * $Slic3r::layer_height > $Slic3r::nozzle_diameter;
    
    # --scale
    die "Invalid value for --scale\n"
        if $Slic3r::scale <= 0;
    
    # --duplicate-x
    die "Invalid value for --duplicate-x\n"
        if $Slic3r::duplicate_x < 1;
    
    # --duplicate-y
    die "Invalid value for --duplicate-y\n"
        if $Slic3r::duplicate_y < 1;
    
    # --duplicate-distance
    die "Invalid value for --duplicate-distance\n"
        if $Slic3r::duplicate_distance < 1;
    
    # --skirt-height
    die "Invalid value for --skirt-height\n"
        if $Slic3r::skirt_height < 0;
    
    # legacy with existing config files
    $Slic3r::small_perimeter_speed ||= $Slic3r::perimeter_speed;
    $Slic3r::bridge_speed ||= $Slic3r::infill_speed;
    $Slic3r::solid_infill_speed ||= $Slic3r::infill_speed;
}

1;
