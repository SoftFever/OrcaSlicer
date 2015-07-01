package Slic3r::GCode::PlaceholderParser;
use strict;
use warnings;

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
