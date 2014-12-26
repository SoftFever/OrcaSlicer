package Slic3r::GCode::VibrationLimit;
use Moo;

extends 'Slic3r::GCode::Reader';

has '_min_time' => (is => 'lazy');
has '_last_dir' => (is => 'ro', default => sub { [0,0] });
has '_dir_time' => (is => 'ro', default => sub { [0,0] });

# inspired by http://hydraraptor.blogspot.it/2010/12/frequency-limit.html

use List::Util qw(max);

sub _build__min_time {
    my ($self) = @_;
    return 1 / ($self->config->vibration_limit * 60);  # in minutes
}

sub process {
    my $self = shift;
    my ($gcode) = @_;
    
    my $new_gcode = "";
    $self->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && $info->{dist_XY} > 0) {
            my $point = Slic3r::Pointf->new($args->{X} // $reader->X, $args->{Y} // $reader->Y);
            my @dir = (
                ($point->x <=> $reader->X),
                ($point->y <=> $reader->Y),   #$
            );
            my $time = $info->{dist_XY} / ($args->{F} // $reader->F);  # in minutes
            
            if ($time > 0) {
                my @pause = ();
                foreach my $axis (0..$#dir) {
                    if ($dir[$axis] != 0 && $self->_last_dir->[$axis] != $dir[$axis]) {
                        if ($self->_last_dir->[$axis] != 0) {
                            # this axis is changing direction: check whether we need to pause
                            if ($self->_dir_time->[$axis] < $self->_min_time) {
                                push @pause, ($self->_min_time - $self->_dir_time->[$axis]);
                            }
                        }
                        $self->_last_dir->[$axis] = $dir[$axis];
                        $self->_dir_time->[$axis] = 0;
                    }
                    $self->_dir_time->[$axis] += $time;
                }
                
                if (@pause) {
                    $new_gcode .= sprintf "G4 P%d\n", max(@pause) * 60 * 1000;
                }
            }
        }
        
        $new_gcode .= $info->{raw} . "\n";
    });
    
    return $new_gcode;
}

1;
