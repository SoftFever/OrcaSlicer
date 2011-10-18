package Slic3r::Config;
use strict;
use warnings;
use utf8;

use constant PI => 4 * atan2(1, 1);

our $Options = {

    # printer options
    'nozzle_diameter' => {
        label   => 'Nozzle diameter',
        type    => 'f',
    },
    'print_center' => {
        label   => 'Print center',
        type    => 'point',
        serialize   => sub { join ',', @{$_[0]} },
        deserialize => sub { [ split /,/, $_[0] ] },
    },
    'use_relative_e_distances' => {
        label   => 'Use relative E distances',
        type    => 'bool',
    },
    'z_offset' => {
        label   => 'Z offset',
        type    => 'f',
    },
    
    # filament options
    'filament_diameter' => {
        label   => 'Diameter (mm)',
        type    => 'f',
    },
    'filament_packing_density' => {
        label   => 'Packing density',
        type    => 'f',
    },
    'temperature' => {
        label   => 'Temperature (°C)',
        type    => 'i',
    },
    
    # speed options
    'print_feed_rate' => {
        label   => 'Print feed rate (mm/s)',
        type    => 'f',
    },
    'travel_feed_rate' => {
        label   => 'Travel feed rate (mm/s)',
        type    => 'f',
    },
    'perimeter_feed_rate' => {
        label   => 'Perimeter feed rate (mm/s)',
        type    => 'f',
    },
    'bottom_layer_speed_ratio' => {
        label   => 'Bottom layer ratio',
        type    => 'f',
    },
    
    # accuracy options
    'layer_height' => {
        label   => 'Layer height (mm)',
        type    => 'f',
    },
    'infill_every_layers' => {
        label   => 'Infill every N layers',
        type    => 'i',
    },
    
    # print options
    'perimeter_offsets' => {
        label   => 'Perimeters',
        type    => 'i',
    },
    'solid_layers' => {
        label   => 'Solid layers',
        type    => 'i',
    },
    'fill_density' => {
        label   => 'Fill density',
        type    => 'f',
    },
    'fill_angle' => {
        label   => 'Fill angle (°)',
        type    => 'i',
    },
    'start_gcode' => {
        label   => 'Start GCODE',
        type    => 's',
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
    },
    'end_gcode' => {
        label   => 'End GCODE',
        type    => 's',
        serialize   => sub { join '\n', split /\R+/, $_[0] },
        deserialize => sub { join "\n", split /\\n/, $_[0] },
    },
    
    # retraction options
    'retract_length' => {
        label   => 'Length (mm)',
        type    => 'f',
    },
    'retract_speed' => {
        label   => 'Speed (mm/s)',
        type    => 'i',
    },
    'retract_restart_extra' => {
        label   => 'Extra length on restart (mm)',
        type    => 'f',
    },
    'retract_before_travel' => {
        label   => 'Minimum travel after retraction (mm)',
        type    => 'f',
    },
    
    # skirt options
    'skirts' => {
        label   => 'Loops',
        type    => 'i',
    },
    'skirt_distance' => {
        label   => 'Distance from object (mm)',
        type    => 'i',
    },
    
    # transform options
    'scale' => {
        label   => 'Scale',
        type    => 'f',
    },
    'rotate' => {
        label   => 'Rotate (°)',
        type    => 'i',
    },
    'multiply_x' => {
        label   => 'Multiply along X',
        type    => 'i',
    },
    'multiply_y' => {
        label   => 'Multiply along Y',
        type    => 'i',
    },
    'multiply_distance' => {
        label   => 'Multiply distance',
        type    => 'i',
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
    
    open my $fh, '<', $file;
    while (<$fh>) {
        next if /^\s*#/;
        /^(\w+) = (.+)/ or die "Unreadable configuration file (invalid data at line $.)\n";
        my $opt = $Options->{$1} or die "Unknown option $1 at like $.\n";
        set($1, $opt->{deserialize} ? $opt->{deserialize}->($2) : $2);
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
                set($_."_gcode", do { local $/; <$fh> });
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
    
    # --filament-diameter
    die "Invalid value for --filament-diameter\n"
        if $Slic3r::filament_diameter < 1;
    
    # --nozzle-diameter
    die "Invalid value for --nozzle-diameter\n"
        if $Slic3r::nozzle_diameter < 0;
    die "--layer-height can't be greater than --nozzle-diameter\n"
        if $Slic3r::layer_height > $Slic3r::nozzle_diameter;
    $Slic3r::flow_width = ($Slic3r::nozzle_diameter**2) 
        * $Slic3r::thickness_ratio * PI / (4 * $Slic3r::layer_height);
    
    my $max_flow_width = $Slic3r::layer_height + $Slic3r::nozzle_diameter;
    if ($Slic3r::flow_width > $max_flow_width) {
        $Slic3r::thickness_ratio = $max_flow_width / $Slic3r::flow_width;
        $Slic3r::flow_width = $max_flow_width;
    }
    
    Slic3r::debugf "Flow width = $Slic3r::flow_width\n";
    
    # --perimeters
    die "Invalid value for --perimeters\n"
        if $Slic3r::perimeter_offsets < 1;
    
    # --solid-layers
    die "Invalid value for --solid-layers\n"
        if $Slic3r::solid_layers < 1;
    
    # --print-center
    die "Invalid value for --print-center\n"
        if !ref $Slic3r::print_center 
            && (!$Slic3r::print_center || $Slic3r::print_center !~ /^\d+,\d+$/);
    $Slic3r::print_center = [ split /,/, $Slic3r::print_center ]
        if !ref $Slic3r::print_center;
    
    # --fill-type
    die "Invalid value for --fill-type\n"
        if !exists $Slic3r::Fill::FillTypes{$Slic3r::fill_type};
    
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
    
    # --multiply-x
    die "Invalid value for --multiply-x\n"
        if $Slic3r::multiply_x < 1;
    
    # --multiply-y
    die "Invalid value for --multiply-y\n"
        if $Slic3r::multiply_y < 1;
    
    # --multiply-distance
    die "Invalid value for --multiply-distance\n"
        if $Slic3r::multiply_distance < 1;
}

1;
