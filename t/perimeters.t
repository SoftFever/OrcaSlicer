use Test::More tests => 7;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(PI scale);
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
        $config->set('duplicate', 2);  # we test two copies to make sure ExtrusionLoop objects are not modified in-place (the second object would not detect cw loops and thus would calculate wrong inwards moves)
        my $print = Slic3r::Test::init_print('cube_with_hole', config => $config);
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
                        $has_outwards_move = 1
                            if !Slic3r::Polygon->new_scale(@$cur_loop)->encloses_point($move_dest)
                                ? ($external_loops{$self->Z} == 2)  # contour should include destination
                                : ($external_loops{$self->Z} == 1); # hole should not
                    }
                    $cur_loop = undef;
                }
            }
        });
        ok !$has_cw_loops, 'all perimeters extruded ccw';
        ok !$has_outwards_move, 'move inwards after completing external loop';
    }
    
    {
        $config->set('start_perimeters_at_concave_points', 1);
        my $print = Slic3r::Test::init_print('L', config => $config);
        my $loop_starts_from_convex_point = 0;
        my $cur_loop;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                $cur_loop ||= [ [$self->X, $self->Y] ];
                push @$cur_loop, [ @$info{qw(new_X new_Y)} ];
            } else {
                if ($cur_loop) {
                    $loop_starts_from_convex_point = 1
                        if Slic3r::Geometry::angle3points(@$cur_loop[0,-1,1]) >= PI;
                    $cur_loop = undef;
                }
            }
        });
        ok !$loop_starts_from_convex_point, 'avoid starting from convex points';
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
    $config->set('perimeters', 2);
    $config->set('perimeter_extrusion_width', 0.4);
    
    # we just need a pre-filled Print object
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    $print->init_extruders;
    
    # override a layer's slices
    my $expolygon = Slic3r::ExPolygon->new([[43000000,0],[43025538,621],[43268205,12545],[43293449,14390],[43317871,17354],[43558753,53080],[43582517,57102],[43620621,66007],[43858200,125524],[43894754,137108],[44126456,220007],[44161408,234134],[44385058,339915],[44418218,356416],[44631631,484329],[44676243,512138],[44877834,661647],[44917594,693352],[45105199,863384],[45121323,878678],[45292005,1067001],[45331421,1112662],[45481223,1314648],[45515660,1368372],[45643575,1581787],[45749961,1806713],[45779081,1871040],[45862012,2102826],[45873905,2139747],[45933434,2377381],[45942897,2417481],[45946921,2441244],[45982644,2682128],[45985606,2706543],[45987451,2731792],[46000632,3000000],[50999999,3000000],[51242592,3011921],[51294051,3014450],[51345014,3022008],[51585267,3057639],[51635243,3070158],[51870848,3129180],[51919357,3146535],[52148048,3228360],[52194623,3250389],[52414191,3354239],[52622517,3479104],[52666708,3505589],[52708089,3536280],[52903180,3680970],[52941354,3715568],[53121323,3878679],[53155922,3916854],[53319031,4096819],[53349722,4138202],[53494408,4333290],[53619272,4541617],[53645760,4585809],[53667788,4632383],[53771640,4851949],[53788995,4900458],[53870820,5129149],[53883338,5179125],[53942360,5414730],[53949916,5465692],[53985548,5705950],[53997469,5948540],[54000000,6000000],[54000000,32000000],[53988076,32242592],[53985548,32294051],[53977989,32345015],[53942360,32585269],[53929840,32635245],[53870819,32870850],[53853463,32919358],[53771639,33148048],[53749610,33194623],[53645760,33414191],[53520893,33622517],[53494407,33666708],[53463717,33708089],[53319031,33903180],[53284433,33941353],[53121323,34121320],[53083147,34155919],[52903180,34319031],[52861796,34349722],[52666708,34494408],[52458382,34619272],[52414192,34645759],[52367615,34667789],[52148048,34771640],[52099538,34788995],[51870847,34870820],[51820870,34883339],[51585267,34942360],[51534305,34949916],[51294052,34985547],[51051459,34997469],[50999999,35000000],[46000632,35000000],[45987450,35268206],[45985607,35293449],[45982642,35317872],[45946919,35558753],[45942897,35582516],[45933991,35620621],[45874476,35858201],[45862890,35894754],[45779990,36126456],[45765864,36161407],[45660083,36385058],[45643581,36418219],[45515668,36631630],[45487858,36676244],[45338352,36877834],[45306647,36917594],[45136618,37105198],[45121324,37121323],[44932998,37292005],[44887337,37331422],[44685348,37481223],[44631625,37515660],[44418214,37643574],[44193284,37749961],[44128955,37779082],[43897172,37862011],[43860250,37873905],[43622616,37933434],[43582516,37942897],[43558752,37946921],[43317871,37982644],[43293456,37985606],[43268208,37987450],[43025539,37999377],[42999999,38000000],[11000000,38000000],[10974460,37999377],[10731793,37987450],[10706552,37985607],[10682127,37982642],[10441244,37946920],[10417480,37942896],[10379374,37933990],[10141796,37874476],[10105243,37862890],[9873541,37779990],[9838588,37765863],[9614941,37660082],[9581782,37643582],[9368365,37515667],[9323752,37487858],[9122165,37338352],[9082405,37306647],[8894803,37136618],[8878678,37121323],[8707995,36932999],[8668577,36887336],[8518773,36685348],[8484336,36631625],[8356425,36418214],[8250036,36193284],[8220917,36128956],[8137988,35897172],[8126094,35860250],[8066563,35622615],[8057101,35582516],[8053078,35558753],[8017352,35317871],[8014390,35293456],[8012545,35268207],[7999365,35000000],[2999999,35000000],[2757408,34988077],[2705948,34985547],[2654986,34977990],[2414730,34942360],[2302727,34914301],[2302727,34914302],[2129149,34870819],[2080639,34853462],[1851949,34771640],[1805374,34749610],[1585808,34645759],[1377480,34520893],[1333289,34494407],[1291907,34463716],[1096820,34319032],[1058646,34284432],[878678,34121319],[844080,34083145],[680969,33903180],[650276,33861796],[505589,33666707],[380725,33458381],[354240,33414192],[332211,33367616],[228358,33148047],[211002,33099537],[129180,32870850],[116660,32820872],[57640,32585270],[50080,32534305],[14450,32294051],[2528,32051459],[0,31999999],[0,6000000],[11920,5757408],[14450,5705949],[22008,5654985],[57639,5414730],[70159,5364752],[129180,5129150],[146537,5080639],[228358,4851949],[236877,4833939],[354239,4585809],[479104,4377479],[505590,4333289],[536280,4291907],[680969,4096819],[715567,4058645],[878679,3878679],[916854,3844080],[1096819,3680970],[1138202,3650277],[1333290,3505590],[1541617,3380726],[1585809,3354239],[1632384,3332210],[1851948,3228360],[1900459,3211002],[2129149,3129180],[2179125,3116660],[2414730,3057640],[2465693,3050080],[2705949,3014449],[2948540,3002528],[3000000,3000000],[7999365,3000000],[8012545,2731794],[8014390,2706550],[8017355,2682127],[8053080,2441243],[8057101,2417481],[8066007,2379375],[8125524,2141796],[8137108,2105243],[8220008,1873540],[8234134,1838589],[8339915,1614942],[8356417,1581780],[8484328,1368366],[8512139,1323752],[8661647,1122165],[8693352,1082405],[8863384,894802],[8878679,878678],[9067000,707995],[9112663,668577],[9314647,518774],[9368373,484336],[9581787,356425],[9806712,250038],[9871041,220917],[10102825,137987],[10139746,126094],[10377381,66564],[10417481,57102],[10441244,53077],[10682128,17352],[10706544,14390],[10731792,12546],[10974459,621],[10999999,0]],[[10000000,2000000],[10000000,36000000],[44000000,36000000],[44000000,2000000]],[[46000000,8000000],[46000000,30000000],[50008820,30000000],[50008820,8000000]],[[3991180,8000000],[3991180,30000000],[8000000,30000000],[8000000,8000000]]);
    my $layer = $print->objects->[0]->layers->[1];
    my $layerm = $layer->region(0);
    $layerm->slices->clear;
    $layerm->slices->append(Slic3r::Surface->new(surface_type => S_TYPE_INTERNAL, expolygon => $expolygon));
    
    # make perimeters
    $layer->make_perimeters;
    
    # We subtract the area covered by infill and gap fill from the expolygon area.
    # This should coincide with the area covered by perimeters. This test ensures
    # no additional unfilled gap is added between perimeters and infill. This could
    # happen when gap detection didn't use safety offset and narrow polygons were
    # detected as gaps (but failed to be infilled of course). GH #1803
    my $non_fill = diff(
        \@$expolygon,
        [
            (map $_->p, @{$layerm->fill_surfaces}),
            @{union([ map $_->polyline->grow(scale $_->flow_spacing/2), @{$layerm->thin_fills} ])},
        ]
    );
    
    my $pflow = $layerm->perimeter_flow;
    ok scalar(@{$non_fill = offset($non_fill, -$pflow->scaled_width*1.1)}) <= 4, 'no gap between perimeters and infill';
}

__END__
