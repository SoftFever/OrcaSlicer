project(libslic3r)
cmake_minimum_required(VERSION 3.13)

include(PrecompiledHeader)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libslic3r_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h @ONLY)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES OpenVDBUtils.cpp OpenVDBUtils.hpp)
endif()

add_library(libslic3r STATIC
    pchheader.cpp
    pchheader.hpp
    BoundingBox.cpp
    BoundingBox.hpp
    BridgeDetector.cpp
    BridgeDetector.hpp
    Brim.cpp
    Brim.hpp
    clipper.cpp
    clipper.hpp
    ClipperUtils.cpp
    ClipperUtils.hpp
    Config.cpp
    Config.hpp
    EdgeGrid.cpp
    EdgeGrid.hpp
    ElephantFootCompensation.cpp
    ElephantFootCompensation.hpp
    ExPolygon.cpp
    ExPolygon.hpp
    ExPolygonCollection.cpp
    ExPolygonCollection.hpp
    Extruder.cpp
    Extruder.hpp
    ExtrusionEntity.cpp
    ExtrusionEntity.hpp
    ExtrusionEntityCollection.cpp
    ExtrusionEntityCollection.hpp
    ExtrusionSimulator.cpp
    ExtrusionSimulator.hpp
    FileParserError.hpp
    Fill/Fill.cpp
    Fill/Fill.hpp
    Fill/Fill3DHoneycomb.cpp
    Fill/Fill3DHoneycomb.hpp
    Fill/FillAdaptive.cpp
    Fill/FillAdaptive.hpp
    Fill/FillBase.cpp
    Fill/FillBase.hpp
    Fill/FillConcentric.cpp
    Fill/FillConcentric.hpp
    Fill/FillHoneycomb.cpp
    Fill/FillHoneycomb.hpp
    Fill/FillGyroid.cpp
    Fill/FillGyroid.hpp
    Fill/FillPlanePath.cpp
    Fill/FillPlanePath.hpp
    Fill/FillLine.cpp
    Fill/FillLine.hpp
    Fill/FillRectilinear.cpp
    Fill/FillRectilinear.hpp
    Flow.cpp
    Flow.hpp
    format.hpp
    Format/3mf.cpp
    Format/3mf.hpp
    Format/AMF.cpp
    Format/AMF.hpp
    Format/OBJ.cpp
    Format/OBJ.hpp
    Format/objparser.cpp
    Format/objparser.hpp
    Format/PRUS.cpp
    Format/PRUS.hpp
    Format/STL.cpp
    Format/STL.hpp
    Format/SL1.hpp
    Format/SL1.cpp
    GCode/ThumbnailData.cpp
    GCode/ThumbnailData.hpp
    GCode/CoolingBuffer.cpp
    GCode/CoolingBuffer.hpp
    GCode/PostProcessor.cpp
    GCode/PostProcessor.hpp
#    GCode/PressureEqualizer.cpp
#    GCode/PressureEqualizer.hpp
    GCode/PrintExtents.cpp
    GCode/PrintExtents.hpp
    GCode/SpiralVase.cpp
    GCode/SpiralVase.hpp
    GCode/SeamPlacer.cpp
    GCode/SeamPlacer.hpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrdering.hpp
    GCode/WipeTower.cpp
    GCode/WipeTower.hpp
    GCode/GCodeProcessor.cpp
    GCode/GCodeProcessor.hpp
    GCode/AvoidCrossingPerimeters.cpp
    GCode/AvoidCrossingPerimeters.hpp
    GCode.cpp
    GCode.hpp
    GCodeReader.cpp
    GCodeReader.hpp
    # GCodeSender.cpp
    # GCodeSender.hpp
    GCodeWriter.cpp
    GCodeWriter.hpp
    Geometry.cpp
    Geometry.hpp
    Int128.hpp
    KDTreeIndirect.hpp
    Layer.cpp
    Layer.hpp
    LayerRegion.cpp
    libslic3r.h
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h"
    Line.cpp
    Line.hpp
    LibraryCheck.cpp
    LibraryCheck.hpp
    Model.cpp
    Model.hpp
    ModelArrange.hpp
    ModelArrange.cpp
    CustomGCode.cpp
    CustomGCode.hpp
    Arrange.hpp
    Arrange.cpp
    MultiPoint.cpp
    MultiPoint.hpp
    MutablePriorityQueue.hpp
    ObjectID.cpp
    ObjectID.hpp
    PerimeterGenerator.cpp
    PerimeterGenerator.hpp
    PlaceholderParser.cpp
    PlaceholderParser.hpp
    Platform.cpp
    Platform.hpp
    Point.cpp
    Point.hpp
    Polygon.cpp
    Polygon.hpp
    MutablePolygon.cpp
    MutablePolygon.hpp
    PolygonTrimmer.cpp
    PolygonTrimmer.hpp
    Polyline.cpp
    Polyline.hpp
    Preset.cpp
    Preset.hpp
    PresetBundle.cpp
    PresetBundle.hpp
    AppConfig.cpp
    AppConfig.hpp
    Print.cpp
    Print.hpp
    PrintApply.cpp
    PrintBase.cpp
    PrintBase.hpp
    PrintConfig.cpp
    PrintConfig.hpp
    PrintObject.cpp
    PrintRegion.cpp
    PNGReadWrite.hpp
    PNGReadWrite.cpp
    Semver.cpp
    ShortestPath.cpp
    ShortestPath.hpp
    SLAPrint.cpp
    SLAPrintSteps.cpp
    SLAPrintSteps.hpp
    SLAPrint.hpp
    Slicing.cpp
    Slicing.hpp
    SlicesToTriangleMesh.hpp
    SlicesToTriangleMesh.cpp
    SlicingAdaptive.cpp
    SlicingAdaptive.hpp
    SupportMaterial.cpp
    SupportMaterial.hpp
    Surface.cpp
    Surface.hpp
    SurfaceCollection.cpp
    SurfaceCollection.hpp
    SVG.cpp
    SVG.hpp
    Technologies.hpp
    Tesselate.cpp
    Tesselate.hpp
    TriangleMesh.cpp
    TriangleMesh.hpp
    TriangleMeshSlicer.cpp
    TriangleMeshSlicer.hpp
    TriangulateWall.hpp
    TriangulateWall.cpp
    utils.cpp
    Utils.hpp
    Time.cpp
    Time.hpp
    Thread.cpp
    Thread.hpp
    TriangleSelector.cpp
    TriangleSelector.hpp
    MTUtils.hpp
    VoronoiOffset.cpp
    VoronoiOffset.hpp
    VoronoiVisualUtils.hpp
    Zipper.hpp
    Zipper.cpp
    MinAreaBoundingBox.hpp
    MinAreaBoundingBox.cpp
    miniz_extension.hpp
    miniz_extension.cpp
    SimplifyMesh.hpp
    SimplifyMeshImpl.hpp
    SimplifyMesh.cpp
    MarchingSquares.hpp
    Execution/Execution.hpp
    Execution/ExecutionSeq.hpp
    Execution/ExecutionTBB.hpp
    Optimize/Optimizer.hpp
    Optimize/NLoptOptimizer.hpp
    Optimize/BruteforceOptimizer.hpp
    ${OpenVDBUtils_SOURCES}
    SLA/Pad.hpp
    SLA/Pad.cpp
    SLA/SupportTreeBuilder.hpp
    SLA/SupportTreeMesher.hpp
    SLA/SupportTreeMesher.cpp
    SLA/SupportTreeBuildsteps.hpp
    SLA/SupportTreeBuildsteps.cpp
    SLA/SupportTreeBuilder.cpp
    SLA/Concurrency.hpp
    SLA/SupportTree.hpp
    SLA/SupportTree.cpp
#    SLA/SupportTreeIGL.cpp
    SLA/Rotfinder.hpp
    SLA/Rotfinder.cpp
    SLA/BoostAdapter.hpp
    SLA/SpatIndex.hpp
    SLA/SpatIndex.cpp
    SLA/RasterBase.hpp
    SLA/RasterBase.cpp
    SLA/AGGRaster.hpp
    SLA/RasterToPolygons.hpp
    SLA/RasterToPolygons.cpp
    SLA/ConcaveHull.hpp
    SLA/ConcaveHull.cpp
    SLA/Hollowing.hpp
    SLA/Hollowing.cpp
    SLA/JobController.hpp
    SLA/SupportPoint.hpp
    SLA/SupportPointGenerator.hpp
    SLA/SupportPointGenerator.cpp
    SLA/Contour3D.hpp
    SLA/Contour3D.cpp
    SLA/IndexedMesh.hpp
    SLA/IndexedMesh.cpp
    SLA/Clustering.hpp
    SLA/Clustering.cpp
    SLA/ReprojectPointsOnMesh.hpp
)

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)
find_package(CGAL REQUIRED)
cmake_policy(POP)

add_library(libslic3r_cgal STATIC MeshBoolean.cpp MeshBoolean.hpp)
target_include_directories(libslic3r_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Reset compile options of libslic3r_cgal. Despite it being linked privately, CGAL options
# (-frounding-math) still propagate to dependent libs which is not desired.
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set (_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
endif()

target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} libigl)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

encoding_check(libslic3r)

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libslic3r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r PUBLIC ${EXPAT_INCLUDE_DIRS})
target_link_libraries(libslic3r
    libnest2d
    admesh
    cereal
    libigl
    miniz
    boost_libs
    clipper
    nowide
    ${EXPAT_LIBRARIES}
    glu-libtess
    qhull
    semver
    TBB::tbb
    libslic3r_cgal
    ${CMAKE_DL_LIBS}
    PNG::PNG
    ZLIB::ZLIB
    )

if (TARGET OpenVDB::openvdb)
    target_link_libraries(libslic3r OpenVDB::openvdb)
endif()

if(WIN32)
    target_link_libraries(libslic3r Psapi.lib)
endif()

if(SLIC3R_PROFILE)
    target_link_libraries(libslic3r Shiny)
endif()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r pchheader.hpp FORCEINCLUDE)
endif ()
