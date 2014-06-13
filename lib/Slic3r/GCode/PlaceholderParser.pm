package Slic3r::GCode::PlaceholderParser;
use strict;
use warnings;

sub new {
    # TODO: move this code to C++ constructor, remove this method
    my ($class) = @_;
    my $self = $class->_new;
    $self->apply_env_variables;
    $self->update_timestamp;
    return $self;
}

sub apply_env_variables {
    my ($self) = @_;
    $self->_single_set($_, $ENV{$_}) for grep /^SLIC3R_/, keys %ENV;
}

sub update_timestamp {
    my ($self) = @_;

    my @lt = localtime; $lt[5] += 1900; $lt[4] += 1;
    $self->_single_set('timestamp', sprintf '%04d%02d%02d-%02d%02d%02d', @lt[5,4,3,2,1,0]);
    $self->_single_set('year',      "$lt[5]");
    $self->_single_set('month',     "$lt[4]");
    $self->_single_set('day',       "$lt[3]");
    $self->_single_set('hour',      "$lt[2]");
    $self->_single_set('minute',    "$lt[1]");
    $self->_single_set('second',    "$lt[0]");
    $self->_single_set('version',   $Slic3r::VERSION);
}

# TODO: or this could be an alias
sub set {
    my ($self, $key, $val) = @_;
    $self->_single_set($key, $val);
}

sub process {
    my ($self, $string, $extra) = @_;
    
    # extra variables have priority over the stored ones
    if ($extra) {
        my $regex = join '|', keys %$extra;
        $string =~ s/\[($regex)\]/$extra->{$1}/eg;
    }
    {
        my $regex = join '|', @{$self->_single_keys};
        $string =~ s/\[($regex)\]/$self->_single_get("$1")/eg;
    }
    {
        my $regex = join '|', @{$self->_multiple_keys};
        $string =~ s/\[($regex)\]/$self->_multiple_get("$1")/egx;
        
        # unhandled indices are populated using the first value
        $string =~ s/\[($regex)_\d+\]/$self->_multiple_get("$1")/egx;
    }
    
    return $string;
}

1;
