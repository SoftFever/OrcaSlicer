package Slic3r::GCode::PlaceholderParser;
use Moo;

has '_single'   => (is => 'ro', default => sub { {} });
has '_multiple' => (is => 'ro', default => sub { {} });

sub BUILD {
    my ($self) = @_;
    
    my $s = $self->_single;
    
    # environment variables
    $s->{$_} = $ENV{$_} for grep /^SLIC3R_/, keys %ENV;
    
    $self->update_timestamp;
}

sub update_timestamp {
    my ($self) = @_;
    
    my $s = $self->_single;
    my @lt = localtime; $lt[5] += 1900; $lt[4] += 1;
    $s->{timestamp} = sprintf '%04d%02d%02d-%02d%02d%02d', @lt[5,4,3,2,1,0];
    $s->{year}      = $lt[5];
    $s->{month}     = $lt[4];
    $s->{day}       = $lt[3];
    $s->{hour}      = $lt[2];
    $s->{minute}    = $lt[1];
    $s->{second}    = $lt[0];
    $s->{version}   = $Slic3r::VERSION;
}

sub apply_config {
    my ($self, $config) = @_;
    
    # options with single value
    my $s = $self->_single;
    my @opt_keys = grep !$Slic3r::Config::Options->{$_}{multiline}, @{$config->get_keys};
    $s->{$_} = $config->serialize($_) for @opt_keys;
    
    # options with multiple values
    my $m = $self->_multiple;
    foreach my $opt_key (@opt_keys) {
        my $value = $config->$opt_key;
        next unless ref($value) eq 'ARRAY';
        $m->{"${opt_key}_" . $_} = $value->[$_] for 0..$#$value;
        $m->{$opt_key} = $value->[0];
        if ($Slic3r::Config::Options->{$opt_key}{type} eq 'point') {
            $m->{"${opt_key}_X"} = $value->[0];
            $m->{"${opt_key}_Y"} = $value->[1];
        }
    }
}

sub set {
    my ($self, $key, $val) = @_;
    $self->_single->{$key} = $val;
}

sub process {
    my ($self, $string, $extra) = @_;
    
    # extra variables have priority over the stored ones
    if ($extra) {
        my $regex = join '|', keys %$extra;
        $string =~ s/\[($regex)\]/$extra->{$1}/eg;
    }
    {
        my $regex = join '|', keys %{$self->_single};
        $string =~ s/\[($regex)\]/$self->_single->{$1}/eg;
    }
    {
        my $regex = join '|', keys %{$self->_multiple};
        $string =~ s/\[($regex)\]/$self->_multiple->{$1}/egx;
        
        # unhandled indices are populated using the first value
        $string =~ s/\[($regex)_\d+\]/$self->_multiple->{$1}/egx;
    }
    
    return $string;
}

1;
