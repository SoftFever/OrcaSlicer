use Module::Build;

my $build = Module::Build->new(
    module_name     => 'Slic3r',
    dist_abstract   => 'G-code generator for 3D printers',
    dist_author     => 'Alessandro Ranellucci <aar@cpan.org>',
    dist_version    => '0.1',
    license         => 'perl',
    requires        => {
        'Boost::Geometry::Utils'    => '0.06',
        'Encode::Locale'            => '0',
        'File::Basename'            => '0',
        'File::Spec'                => '0',
        'Getopt::Long'              => '0',
        'Math::Clipper'             => '1.17',
        'Math::ConvexHull::MonotoneChain' => '0.01',
        'Math::Geometry::Voronoi'   => '1.3',
        'Math::PlanePath'           => '53',
        'Moo'                       => '0.091009',
        'perl'                      => '5.10.0',
        'Scalar::Util'              => '0',
        'Time::HiRes'               => '0',
    },
    build_requires => {
        'Test::More' => '0.10',
        'IO::Scalar' => '0.10',
    },
    recommends     => {
        'Growl::GNTP' => '0.15',
        'Net::DBus'   => '0',
        'XML::SAX::ExpatXS' => '0',
        'Wx'          => '0.9901',
    },
    script_files    => ['slic3r.pl'],
);

if (not $ENV{SLIC3R_NO_AUTO})
{
    $build->dispatch('installdeps');
    $build->dispatch('test', verbose => 0);
}

$build->create_build_script;
