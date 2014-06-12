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

sub apply_config {
    my ($self, $config) = @_;
    
    # options with single value
    my @opt_keys = grep !$Slic3r::Config::Options->{$_}{multiline}, @{$config->get_keys};
    $self->_single_set($_, $config->serialize($_)) for @opt_keys;

    # options with multiple values
    foreach my $opt_key (@opt_keys) {
        my $value = $config->$opt_key;
        next unless ref($value) eq 'ARRAY';
        # TODO: this is a workaroud for XS string param handling
        # https://rt.cpan.org/Public/Bug/Display.html?id=94110
        "$_" for @$value;
        $self->_multiple_set("${opt_key}_" . $_, $value->[$_]."") for 0..$#$value;
        $self->_multiple_set($opt_key, $value->[0]."");
        if ($Slic3r::Config::Options->{$opt_key}{type} eq 'point') {
            $self->_multiple_set("${opt_key}_X", $value->[0]."");
            $self->_multiple_set("${opt_key}_Y", $value->[1]."");
        }
    }
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
