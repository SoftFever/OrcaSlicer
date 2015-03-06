use Test::More tests => 29;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use List::Util qw(first);
use Slic3r;
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(PI scale unscale);
use Slic3r::Geometry::Clipper qw(union_ex diff union offset);
use Slic3r::Surface ':types';
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('fill_density', 0);
    $config->set('perimeters', 3);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('cooling', 0);                     # to prevent speeds from being altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds from being altered
    
    {
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my $has_cw_loops = 0;
        my $cur_loop;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $has_cw_loops = 1 if Slic3r::Polygon->new(@$cur_loop)->is_clockwise;
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
    }
    
    {
        $config->set('external_perimeter_speed', 68);
        my $print = Slic3r::Test::init_print(
            'cube_with_hole',
            config      => $config,
            duplicate   => 2,  # we test two copies to make sure ExtrusionLoop objects are not modified in-place (the second object would not detect cw loops and thus would calculate wrong inwards moves)
        );
        my $has_cw_loops = my $has_outwards_move = 0;
        my $cur_loop;
        my %external_loops = ();  # print_z => count of external loops
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $has_cw_loops = 1 if Slic3r::Polygon->new_scale(@$cur_loop)->is_clockwise;
                    if ($self->F == $config->external_perimeter_speed*60) {
                        my $move_dest = Slic3r::Point->new_scale(@$info{qw(new_X new_Y)});
                        
                        # reset counter for second object
                        $external_loops{$self->Z} = 0
                            if defined($external_loops{$self->Z}) && $external_loops{$self->Z} == 2;
                        
                        $external_loops{$self->Z}++;
                        my $loop_contains_point = Slic3r::Polygon->new_scale(@$cur_loop)->contains_point($move_dest);
                        $has_outwards_move = 1
                            if (!$loop_contains_point && $external_loops{$self->Z} == 2)  # contour should include destination
                             || ($loop_contains_point && $external_loops{$self->Z} == 1); # hole should not
                    }
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
        ok !$has_outwards_move, 'move inwards after completing external loop';
    }
    
    {
        $config->set('perimeters', 1);
        $config->set('perimeter_speed', 77);
        $config->set('external_perimeter_speed', 66);
        $config->set('bridge_speed', 99);
        $config->set('cooling', 1);
        $config->set('fan_below_layer_time', 0);
        $config->set('slowdown_below_layer_time', 0);
        $config->set('bridge_fan_speed', 100);
        $config->set('bridge_flow_ratio', 33);  # arbitrary value
        $config->set('overhangs', 1);
        my $print = Slic3r::Test::init_print('overhang', config => $config);
        my %layer_speeds = ();  # print Z => [ speeds ]
        my $fan_speed = 0;
        my $bridge_mm_per_mm = ($config->nozzle_diameter->[0]**2) / ($config->filament_diameter->[0]**2) * $config->bridge_flow_ratio;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            $fan_speed = 0 if $cmd eq 'M107';
            $fan_speed = $args->{S} if $cmd eq 'M106';
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $layer_speeds{$self->Z} ||= {};
                $layer_speeds{$self->Z}{my $feedrate = $args->{F} // $self->F} = 1;
                
                fail 'wrong speed found'
                    if $feedrate != $config->perimeter_speed*60
                        && $feedrate != $config->external_perimeter_speed*60
                        && $feedrate != $config->bridge_speed*60;
                
                if ($feedrate == $config->bridge_speed*60) {
                    fail 'printing overhang but fan is not enabled or running at wrong speed'
                        if $fan_speed != 255;
                    my $mm_per_mm = $info->{dist_E} / $info->{dist_XY};
                    fail 'wrong bridge flow' if abs($mm_per_mm - $bridge_mm_per_mm) > 0.01;
                } else {
                    fail 'fan is running when not supposed to'
                        if $fan_speed > 0;
                }
            }
        });
        is scalar(grep { keys %$_ > 1 } values %layer_speeds), 1,
            'only overhang layer has more than one speed';
    }
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 3);
    $config->set('layer_height', 0.4);
    $config->set('first_layer_height', 0.35);
    $config->set('extra_perimeters', 1);
    $config->set('cooling', 0);                     # to prevent speeds from being altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds from being altered
    $config->set('perimeter_speed', 99);
    $config->set('external_perimeter_speed', 99);
    $config->set('small_perimeter_speed', 99);
    $config->set('thin_walls', 0);
    
    my $print = Slic3r::Test::init_print('ipadstand', config => $config);
    my %perimeters = ();  # z => number of loops
    my $in_loop = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY} > 0 && ($args->{F} // $self->F) == $config->perimeter_speed*60) {
            $perimeters{$self->Z}++ if !$in_loop;
            $in_loop = 1;
        } else {
            $in_loop = 0;
        }
    });
    ok !(grep { $_ % $config->perimeters } values %perimeters), 'no superfluous extra perimeters';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('nozzle_diameter', [0.4]);
    $config->set('perimeters', 2);
    $config->set('perimeter_extrusion_width', 0.4);
    $config->set('infill_extrusion_width', 0.53);
    $config->set('solid_infill_extrusion_width', 0.53);
    
    # we just need a pre-filled Print object
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    # override a layer's slices
    my $expolygon = Slic3r::ExPolygon->new([[-71974463,-139999376],[-71731792,-139987456],[-71706544,-139985616],[-71682119,-139982639],[-71441248,-139946912],[-71417487,-139942895],[-71379384,-139933984],[-71141800,-139874480],[-71105247,-139862895],[-70873544,-139779984],[-70838592,-139765856],[-70614943,-139660064],[-70581783,-139643567],[-70368368,-139515680],[-70323751,-139487872],[-70122160,-139338352],[-70082399,-139306639],[-69894800,-139136624],[-69878679,-139121327],[-69707992,-138933008],[-69668575,-138887343],[-69518775,-138685359],[-69484336,-138631632],[-69356423,-138418207],[-69250040,-138193296],[-69220920,-138128976],[-69137992,-137897168],[-69126095,-137860255],[-69066568,-137622608],[-69057104,-137582511],[-69053079,-137558751],[-69017352,-137317872],[-69014392,-137293456],[-69012543,-137268207],[-68999369,-137000000],[-63999999,-137000000],[-63705947,-136985551],[-63654984,-136977984],[-63414731,-136942351],[-63364756,-136929840],[-63129151,-136870815],[-62851950,-136771631],[-62585807,-136645743],[-62377483,-136520895],[-62333291,-136494415],[-62291908,-136463728],[-62096819,-136319023],[-62058644,-136284432],[-61878676,-136121328],[-61680968,-135903184],[-61650275,-135861807],[-61505591,-135666719],[-61354239,-135414191],[-61332211,-135367615],[-61228359,-135148063],[-61129179,-134870847],[-61057639,-134585262],[-61014451,-134294047],[-61000000,-134000000],[-61000000,-107999999],[-61014451,-107705944],[-61057639,-107414736],[-61129179,-107129152],[-61228359,-106851953],[-61354239,-106585808],[-61505591,-106333288],[-61680967,-106096816],[-61878675,-105878680],[-62096820,-105680967],[-62138204,-105650279],[-62333292,-105505591],[-62585808,-105354239],[-62632384,-105332207],[-62851951,-105228360],[-62900463,-105211008],[-63129152,-105129183],[-63414731,-105057640],[-63705947,-105014448],[-63999999,-105000000],[-68999369,-105000000],[-69012543,-104731792],[-69014392,-104706544],[-69017352,-104682119],[-69053079,-104441248],[-69057104,-104417487],[-69066008,-104379383],[-69125528,-104141799],[-69137111,-104105248],[-69220007,-103873544],[-69234136,-103838591],[-69339920,-103614943],[-69356415,-103581784],[-69484328,-103368367],[-69512143,-103323752],[-69661647,-103122160],[-69693352,-103082399],[-69863383,-102894800],[-69878680,-102878679],[-70066999,-102707992],[-70112656,-102668576],[-70314648,-102518775],[-70368367,-102484336],[-70581783,-102356424],[-70806711,-102250040],[-70871040,-102220919],[-71102823,-102137992],[-71139752,-102126095],[-71377383,-102066568],[-71417487,-102057104],[-71441248,-102053079],[-71682119,-102017352],[-71706535,-102014392],[-71731784,-102012543],[-71974456,-102000624],[-71999999,-102000000],[-104000000,-102000000],[-104025536,-102000624],[-104268207,-102012543],[-104293455,-102014392],[-104317880,-102017352],[-104558751,-102053079],[-104582512,-102057104],[-104620616,-102066008],[-104858200,-102125528],[-104894751,-102137111],[-105126455,-102220007],[-105161408,-102234136],[-105385056,-102339920],[-105418215,-102356415],[-105631632,-102484328],[-105676247,-102512143],[-105877839,-102661647],[-105917600,-102693352],[-106105199,-102863383],[-106121320,-102878680],[-106292007,-103066999],[-106331424,-103112656],[-106481224,-103314648],[-106515663,-103368367],[-106643575,-103581783],[-106749959,-103806711],[-106779080,-103871040],[-106862007,-104102823],[-106873904,-104139752],[-106933431,-104377383],[-106942896,-104417487],[-106946920,-104441248],[-106982648,-104682119],[-106985607,-104706535],[-106987456,-104731784],[-107000630,-105000000],[-112000000,-105000000],[-112294056,-105014448],[-112585264,-105057640],[-112870848,-105129184],[-112919359,-105146535],[-113148048,-105228360],[-113194624,-105250392],[-113414191,-105354239],[-113666711,-105505591],[-113708095,-105536279],[-113903183,-105680967],[-114121320,-105878679],[-114319032,-106096816],[-114349720,-106138200],[-114494408,-106333288],[-114645760,-106585808],[-114667792,-106632384],[-114771640,-106851952],[-114788991,-106900463],[-114870815,-107129151],[-114942359,-107414735],[-114985551,-107705943],[-115000000,-107999999],[-115000000,-134000000],[-114985551,-134294048],[-114942359,-134585263],[-114870816,-134870847],[-114853464,-134919359],[-114771639,-135148064],[-114645759,-135414192],[-114494407,-135666720],[-114319031,-135903184],[-114121320,-136121327],[-114083144,-136155919],[-113903184,-136319023],[-113861799,-136349712],[-113666711,-136494416],[-113458383,-136619264],[-113414192,-136645743],[-113148049,-136771631],[-112870848,-136870815],[-112820872,-136883327],[-112585264,-136942351],[-112534303,-136949920],[-112294056,-136985551],[-112000000,-137000000],[-107000630,-137000000],[-106987456,-137268207],[-106985608,-137293440],[-106982647,-137317872],[-106946920,-137558751],[-106942896,-137582511],[-106933991,-137620624],[-106874471,-137858208],[-106862888,-137894751],[-106779992,-138126463],[-106765863,-138161424],[-106660080,-138385055],[-106643584,-138418223],[-106515671,-138631648],[-106487855,-138676256],[-106338352,-138877839],[-106306647,-138917600],[-106136616,-139105199],[-106121320,-139121328],[-105933000,-139291999],[-105887344,-139331407],[-105685351,-139481232],[-105631632,-139515663],[-105418216,-139643567],[-105193288,-139749951],[-105128959,-139779072],[-104897175,-139862016],[-104860247,-139873904],[-104622616,-139933423],[-104582511,-139942896],[-104558751,-139946912],[-104317880,-139982656],[-104293463,-139985616],[-104268216,-139987456],[-104025544,-139999376],[-104000000,-140000000],[-71999999,-140000000]],[[-105000000,-138000000],[-105000000,-104000000],[-71000000,-104000000],[-71000000,-138000000]],[[-69000000,-132000000],[-69000000,-110000000],[-64991180,-110000000],[-64991180,-132000000]],[[-111008824,-132000000],[-111008824,-110000000],[-107000000,-110000000],[-107000000,-132000000]]);
    my $object = $print->print->objects->[0];
    $object->slice;
    my $layer = $object->get_layer(1);
    my $layerm = $layer->regions->[0];
    $layerm->slices->clear;
    $layerm->slices->append(Slic3r::Surface->new(surface_type => S_TYPE_INTERNAL, expolygon => $expolygon));
    
    # make perimeters
    $layer->make_perimeters;
    
    # compute the covered area
    my $pflow = $layerm->flow(FLOW_ROLE_PERIMETER);
    my $iflow = $layerm->flow(FLOW_ROLE_INFILL);
    my $covered_by_perimeters = union_ex([
        (map @{$_->polygon->split_at_first_point->grow($pflow->scaled_width/2)}, map @$_, @{$layerm->perimeters}),
    ]);
    my $covered_by_infill = union_ex([
        (map $_->p, @{$layerm->fill_surfaces}),
        (map @{$_->polyline->grow($iflow->scaled_width/2)}, @{$layerm->thin_fills}),
    ]);
    
    # compute the non covered area
    my $non_covered = diff(
        [ map @{$_->expolygon}, @{$layerm->slices} ],
        [ map @$_, (@$covered_by_perimeters, @$covered_by_infill) ],
    );
    
    if (0) {
        printf "max non covered = %f\n", List::Util::max(map unscale unscale $_->area, @$non_covered);
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "gaps.svg",
            expolygons          => [ map $_->expolygon, @{$layerm->slices} ],
            red_expolygons      => union_ex([ map @$_, (@$covered_by_perimeters, @$covered_by_infill) ]),
            green_expolygons    => union_ex($non_covered),
        );
    }
    
    ok !(defined first { $_->area > ($iflow->scaled_width**2) } @$non_covered), 'no gap between perimeters and infill';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 3);
    $config->set('layer_height', 0.4);
    $config->set('bridge_speed', 99);
    $config->set('fill_density', 0);                # to prevent bridging over sparse infill
    $config->set('overhangs', 1);
    $config->set('cooling', 0);                     # to prevent speeds from being altered
    $config->set('first_layer_speed', '100%');      # to prevent speeds from being altered
    
    my $test = sub {
        my ($print) = @_;
        my %z_with_bridges = ();  # z => 1
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $z_with_bridges{$self->Z} = 1 if ($args->{F} // $self->F) == $config->bridge_speed*60;
            }
        });
        return scalar keys %z_with_bridges;
    };
    ok $test->(Slic3r::Test::init_print('V', config => $config)) == 1,
        'no overhangs printed with bridge speed';  # except for the first internal solid layers above void
    ok $test->(Slic3r::Test::init_print('V', config => $config, scale_xyz => [3,1,1])) > 1,
        'overhangs printed with bridge speed';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('seam_position', 'random');
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'successful generation of G-code with seam_position = random';
}

{
    my $test = sub {
        my ($model_name) = @_;
        my $config = Slic3r::Config->new_from_defaults;
        $config->set('seam_position', 'aligned');
        $config->set('skirts', 0);
        $config->set('perimeters', 1);
        $config->set('fill_density', 0);
        $config->set('top_solid_layers', 0);
        $config->set('bottom_solid_layers', 0);
        $config->set('retract_layer_change', [0]);
    
        my $was_extruding = 0;
        my @seam_points = ();
        my $print = Slic3r::Test::init_print($model_name, config => $config);
        Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
    
            if ($info->{extruding}) {
                if (!$was_extruding) {
                    push @seam_points, Slic3r::Point->new_scale($self->X, $self->Y);
                }
                $was_extruding = 1;
            } else {
                $was_extruding = 0;
            }
        });
        my @dist = map unscale($_), map $seam_points[$_]->distance_to($seam_points[$_+1]), 0..($#seam_points-1);
        ok !(defined first { $_ > 3 } @dist), 'seam is aligned';
    };
    $test->('20mm_cube');
    $test->('small_dorito');
}

{
    my $flow = Slic3r::Flow->new(
        width           => 1,
        height          => 1,
        nozzle_diameter => 1,
    );
    
    my $config = Slic3r::Config->new;
    my $test = sub {
        my ($expolygons, %expected) = @_;
        
        my $slices = Slic3r::Surface::Collection->new;
        $slices->append(Slic3r::Surface->new(
            surface_type => S_TYPE_INTERNAL,
            expolygon => $_,
        )) for @$expolygons;
    
        my $g = Slic3r::Layer::PerimeterGenerator->new(
            # input:
            layer_height    => 1,
            slices          => $slices,
            flow            => $flow,
        );
        $g->config->apply_dynamic($config);
        $g->process;
        
        is scalar(@{$g->loops}),
            scalar(@$expolygons), 'expected number of collections';
        ok !defined(first { !$_->isa('Slic3r::ExtrusionPath::Collection') } @{$g->loops}),
            'everything is returned as collections';
        is scalar(map @$_, @{$g->loops}),
            $expected{total}, 'expected number of loops';
        is scalar(grep $_->role == EXTR_ROLE_EXTERNAL_PERIMETER, map @$_, map @$_, @{$g->loops}),
            $expected{external}, 'expected number of external loops';
        is scalar(grep $_->role == EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER, map @$_, @{$g->loops}),
            $expected{cinternal}, 'expected number of internal contour loops';
        is scalar(grep $_->polygon->is_counter_clockwise, map @$_, @{$g->loops}),
            $expected{ccw}, 'expected number of ccw loops';
        
        return $g;
    };
    
    $config->set('perimeters', 3);
    $test->(
        [
            Slic3r::ExPolygon->new(
                Slic3r::Polygon->new_scale([0,0], [100,0], [100,100], [0,100]),
            ),
        ],
        total       => 3,
        external    => 1,
        cinternal   => 1,
        ccw         => 3,
    );
    $test->(
        [
            Slic3r::ExPolygon->new(
                Slic3r::Polygon->new_scale([0,0], [100,0], [100,100], [0,100]),
                Slic3r::Polygon->new_scale([40,40], [40,60], [60,60], [60,40]),
            ),
        ],
        total       => 6,
        external    => 2,
        cinternal   => 1,
        ccw         => 3,
    );
    $test->(
        [
            Slic3r::ExPolygon->new(
                Slic3r::Polygon->new_scale([0,0], [200,0], [200,200], [0,200]),
                Slic3r::Polygon->new_scale([20,20], [20,180], [180,180], [180,20]),
            ),
            # nested:
            Slic3r::ExPolygon->new(
                Slic3r::Polygon->new_scale([50,50], [150,50], [150,150], [50,150]),
                Slic3r::Polygon->new_scale([80,80], [80,120], [120,120], [120,80]),
            ),
        ],
        total       => 4*3,
        external    => 4,
        cinternal   => 2,
        ccw         => 2*3,
    );
}

__END__
