package Slic3r::GCode::PlaceholderParser;
use strict;
use warnings;

sub new {
    # TODO: move this code to C++ constructor, remove this method
    my ($class) = @_;
    
    my $self = $class->_new;
    $self->apply_env_variables;
    return $self;
}

sub apply_env_variables {
    my ($self) = @_;
    $self->_single_set($_, $ENV{$_}) for grep /^SLIC3R_/, keys %ENV;
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
