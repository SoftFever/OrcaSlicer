package Slic3r::Surface::Collection;
use Moo;

has 'surfaces' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Surface]',
    default => sub { [] },
);

1;
