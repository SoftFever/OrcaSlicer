package Slic3r::GUI::Plater::3D;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Wx qw(:misc :pen :brush :sizer :font :cursor :keycode wxTAB_TRAVERSAL);
use base qw(Slic3r::GUI::3DScene Class::Accessor);

sub new {
    my $class = shift;
    my ($parent, $objects, $model, $print, $config) = @_;
    
    my $self = $class->SUPER::new($parent);
    Slic3r::GUI::_3DScene::enable_picking($self, 1);
    Slic3r::GUI::_3DScene::enable_moving($self, 1);
    Slic3r::GUI::_3DScene::set_select_by($self, 'object');
    Slic3r::GUI::_3DScene::set_drag_by($self, 'instance');
    Slic3r::GUI::_3DScene::set_model($self, $model);
    Slic3r::GUI::_3DScene::set_print($self, $print);
    Slic3r::GUI::_3DScene::set_config($self, $config);
    
    return $self;
}

1;
