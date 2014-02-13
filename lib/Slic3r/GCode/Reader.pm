package Slic3r::GCode::Reader;
use Moo;

has 'X' => (is => 'rw', default => sub {0});
has 'Y' => (is => 'rw', default => sub {0});
has 'Z' => (is => 'rw', default => sub {0});
has 'E' => (is => 'rw', default => sub {0});
has 'F' => (is => 'rw', default => sub {0});

our $Verbose = 0;
my @AXES = qw(X Y Z E);

sub clone {
    my $self = shift;
    return (ref $self)->new(
        map { $_ => $self->$_ } (@AXES, 'F'),
    );
}

sub parse {
    my $self = shift;
    my ($gcode, $cb) = @_;
    
    foreach my $raw_line (split /\R+/, $gcode) {
        print "$raw_line\n" if $Verbose || $ENV{SLIC3R_TESTS_GCODE};
        my $line = $raw_line;
        $line =~ s/\s*;(.*)//; # strip comment
        my %info = (comment => $1, raw => $raw_line);
        
        # parse command
        my ($command, @args) = split /\s+/, $line;
        $command //= '';
        my %args = map { /([A-Z])(.*)/; ($1 => $2) } @args;
        
        # check motion
        if ($command =~ /^G[01]$/) {
            foreach my $axis (@AXES) {
                if (exists $args{$axis}) {
                    $info{"dist_$axis"} = $args{$axis} - $self->$axis;
                    $info{"new_$axis"}  = $args{$axis};
                } else {
                    $info{"dist_$axis"} = 0;
                    $info{"new_$axis"}  = $self->$axis;
                }
            }
            $info{dist_XY} = Slic3r::Geometry::unscale(Slic3r::Line->new_scale([0,0], [@info{qw(dist_X dist_Y)}])->length);
            if (exists $args{E}) {
                if ($info{dist_E} > 0) {
                    $info{extruding} = 1;
                } elsif ($info{dist_E} < 0) {
                    $info{retracting} = 1
                }
            } else {
                $info{travel} = 1;
            }
        }
        
        # run callback
        $cb->($self, $command, \%args, \%info);
        
        # update coordinates
        if ($command =~ /^(?:G[01]|G92)$/) {
            for my $axis (@AXES, 'F') {
                $self->$axis($args{$axis}) if exists $args{$axis};
            }
        }
        
        # TODO: update temperatures
    }
}

1;
