use Module::Build;

my $build = Module::Build->new(
    module_name     => 'Slic3r',
    dist_abstract   => 'STL-to-GCODE translator',
    dist_author     => 'Alessandro Ranellucci <aar@cpan.org>',
    dist_version    => '0.1',
    license         => 'perl',
    requires        => {
        'File::Basename'            => '0',
        'Getopt::Long'              => '0',
        'Math::Clipper'             => '1.02',
        'Math::ConvexHull'          => '1.0.4',
        'Math::Geometry::Voronoi'   => '1.3',
        'Math::PlanePath'           => '53',
        'Moo'                       => '0',
        'Time::HiRes'               => '0',
        'XXX'                       => '0',
    },
    build_requires => {
        'Test::More' => '0.10',
    },
    script_files    => ['slic3r.pl'],
);
$build->dispatch('installdeps');
$build->dispatch('test', verbose => 0);

$build->create_build_script;
