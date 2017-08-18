# Enable C++11 language standard.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)

# Where all the bundled libraries reside?
set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/)
# For the bundled boost libraries (boost::nowide)
include_directories(${LIBDIR})
# For libslic3r.h
include_directories(${LIBDIR}/libslic3r)
#set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (WIN32)
    add_definitions(-D_USE_MATH_DEFINES -D_WIN32)
endif ()

add_library(libslic3r STATIC
    ${LIBDIR}/libslic3r/BoundingBox.cpp
    ${LIBDIR}/libslic3r/BridgeDetector.cpp
    ${LIBDIR}/libslic3r/ClipperUtils.cpp
    ${LIBDIR}/libslic3r/Config.cpp
    ${LIBDIR}/libslic3r/EdgeGrid.cpp
    ${LIBDIR}/libslic3r/ExPolygon.cpp
    ${LIBDIR}/libslic3r/ExPolygonCollection.cpp
    ${LIBDIR}/libslic3r/Extruder.cpp
    ${LIBDIR}/libslic3r/ExtrusionEntity.cpp
    ${LIBDIR}/libslic3r/ExtrusionEntityCollection.cpp
    ${LIBDIR}/libslic3r/ExtrusionSimulator.cpp
    ${LIBDIR}/libslic3r/Fill/Fill.cpp
    ${LIBDIR}/libslic3r/Fill/Fill3DHoneycomb.cpp
    ${LIBDIR}/libslic3r/Fill/FillBase.cpp
    ${LIBDIR}/libslic3r/Fill/FillConcentric.cpp
    ${LIBDIR}/libslic3r/Fill/FillHoneycomb.cpp
    ${LIBDIR}/libslic3r/Fill/FillPlanePath.cpp
    ${LIBDIR}/libslic3r/Fill/FillRectilinear.cpp
    ${LIBDIR}/libslic3r/Fill/FillRectilinear2.cpp
    ${LIBDIR}/libslic3r/Fill/FillRectilinear3.cpp
    ${LIBDIR}/libslic3r/Flow.cpp
    ${LIBDIR}/libslic3r/Format/AMF.cpp
    ${LIBDIR}/libslic3r/Format/OBJ.cpp
    ${LIBDIR}/libslic3r/Format/objparser.cpp
    ${LIBDIR}/libslic3r/Format/PRUS.cpp
    ${LIBDIR}/libslic3r/Format/STL.cpp
    ${LIBDIR}/libslic3r/GCode.cpp
    ${LIBDIR}/libslic3r/GCode/Analyzer.cpp
    ${LIBDIR}/libslic3r/GCode/CoolingBuffer.cpp
    ${LIBDIR}/libslic3r/GCode/PressureEqualizer.cpp
    ${LIBDIR}/libslic3r/GCode/SpiralVase.cpp
    ${LIBDIR}/libslic3r/GCode/ToolOrdering.cpp
    ${LIBDIR}/libslic3r/GCode/WipeTowerPrusaMM.cpp
    ${LIBDIR}/libslic3r/GCodeReader.cpp
    ${LIBDIR}/libslic3r/GCodeSender.cpp
    ${LIBDIR}/libslic3r/GCodeTimeEstimator.cpp
    ${LIBDIR}/libslic3r/GCodeWriter.cpp
    ${LIBDIR}/libslic3r/Geometry.cpp
    ${LIBDIR}/libslic3r/Layer.cpp
    ${LIBDIR}/libslic3r/LayerRegion.cpp
    ${LIBDIR}/libslic3r/Line.cpp
    ${LIBDIR}/libslic3r/Model.cpp
    ${LIBDIR}/libslic3r/MotionPlanner.cpp
    ${LIBDIR}/libslic3r/MultiPoint.cpp
    ${LIBDIR}/libslic3r/PerimeterGenerator.cpp
    ${LIBDIR}/libslic3r/PlaceholderParser.cpp
    ${LIBDIR}/libslic3r/Point.cpp
    ${LIBDIR}/libslic3r/Polygon.cpp
    ${LIBDIR}/libslic3r/Polyline.cpp
    ${LIBDIR}/libslic3r/PolylineCollection.cpp
    ${LIBDIR}/libslic3r/Print.cpp
    ${LIBDIR}/libslic3r/PrintConfig.cpp
    ${LIBDIR}/libslic3r/PrintObject.cpp
    ${LIBDIR}/libslic3r/PrintRegion.cpp
    ${LIBDIR}/libslic3r/Slicing.cpp
    ${LIBDIR}/libslic3r/SlicingAdaptive.cpp
    ${LIBDIR}/libslic3r/SupportMaterial.cpp
    ${LIBDIR}/libslic3r/Surface.cpp
    ${LIBDIR}/libslic3r/SurfaceCollection.cpp
    ${LIBDIR}/libslic3r/SVG.cpp
    ${LIBDIR}/libslic3r/TriangleMesh.cpp
)

add_library(libslic3r_gui STATIC
    ${LIBDIR}/slic3r/GUI/3DScene.cpp
    ${LIBDIR}/slic3r/GUI/GLShader.cpp
    ${LIBDIR}/slic3r/GUI/GUI.cpp
)

add_library(admesh STATIC
    ${LIBDIR}/admesh/connect.cpp
    ${LIBDIR}/admesh/normals.cpp
    ${LIBDIR}/admesh/shared.cpp
    ${LIBDIR}/admesh/stl_io.cpp
    ${LIBDIR}/admesh/stlinit.cpp
    ${LIBDIR}/admesh/util.cpp
)

add_library(clipper STATIC ${LIBDIR}/clipper.cpp)

add_library(polypartition STATIC ${LIBDIR}/polypartition.cpp)

add_library(poly2tri STATIC
    ${LIBDIR}/poly2tri/common/shapes.cc
    ${LIBDIR}/poly2tri/sweep/advancing_front.cc
    ${LIBDIR}/poly2tri/sweep/cdt.cc
    ${LIBDIR}/poly2tri/sweep/sweep_context.cc
    ${LIBDIR}/poly2tri/sweep/sweep.cc
)

add_library(nowide STATIC 
    ${LIBDIR}/boost/nowide/args.hpp
    ${LIBDIR}/boost/nowide/cenv.hpp
    ${LIBDIR}/boost/nowide/config.hpp
    ${LIBDIR}/boost/nowide/convert.hpp
    ${LIBDIR}/boost/nowide/cstdio.hpp
    ${LIBDIR}/boost/nowide/cstdlib.hpp
    ${LIBDIR}/boost/nowide/filebuf.hpp
    ${LIBDIR}/boost/nowide/fstream.hpp
    ${LIBDIR}/boost/nowide/integration/filesystem.hpp
    ${LIBDIR}/boost/nowide/iostream.cpp
    ${LIBDIR}/boost/nowide/iostream.hpp
    ${LIBDIR}/boost/nowide/stackstring.hpp
    ${LIBDIR}/boost/nowide/system.hpp
    ${LIBDIR}/boost/nowide/utf8_codecvt.hpp
    ${LIBDIR}/boost/nowide/windows.hpp
)

add_library(Shiny STATIC
    ${LIBDIR}/Shiny/ShinyManager.c
    ${LIBDIR}/Shiny/ShinyNode.c
    ${LIBDIR}/Shiny/ShinyNodePool.c
    ${LIBDIR}/Shiny/ShinyNodeState.c
    ${LIBDIR}/Shiny/ShinyOutput.c
    ${LIBDIR}/Shiny/ShinyTools.c
    ${LIBDIR}/Shiny/ShinyZone.c
)

# Generate the Slic3r Perl module (XS) typemap file.
#FIXME add the dependencies.
find_package(Perl REQUIRED)
set(MyTypemap ${CMAKE_CURRENT_BINARY_DIR}/typemap)
add_custom_command(
        OUTPUT ${MyTypemap}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/xsp/my.map
        COMMAND ${PERL_EXECUTABLE} -MExtUtils::Typemaps -MExtUtils::Typemaps::Basic -e "$typemap = ExtUtils::Typemaps->new(file => \"${CMAKE_CURRENT_LIST_DIR}/xsp/my.map\"); $typemap->merge(typemap => ExtUtils::Typemaps::Basic->new); $typemap->write(file => \"${MyTypemap}\")"
        VERBATIM
)

# Generate the Slic3r Perl module (XS) main.xs file.
set(XS_MAIN_XS ${CMAKE_CURRENT_BINARY_DIR}/main.xs)
#FIXME list the dependecies explicitely, add dependency on the typemap.
file(GLOB XS_XSP_FILES 
    ${LIBDIR}/xsp/BoundingBox.xsp
    ${LIBDIR}/xsp/BridgeDetector.xsp
    ${LIBDIR}/xsp/Clipper.xsp
    ${LIBDIR}/xsp/Config.xsp
    ${LIBDIR}/xsp/ExPolygon.xsp
    ${LIBDIR}/xsp/ExPolygonCollection.xsp
    ${LIBDIR}/xsp/ExtrusionEntityCollection.xsp
    ${LIBDIR}/xsp/ExtrusionLoop.xsp
    ${LIBDIR}/xsp/ExtrusionMultiPath.xsp
    ${LIBDIR}/xsp/ExtrusionPath.xsp
    ${LIBDIR}/xsp/ExtrusionSimulator.xsp
    ${LIBDIR}/xsp/Filler.xsp
    ${LIBDIR}/xsp/Flow.xsp
    ${LIBDIR}/xsp/GCode.xsp
    ${LIBDIR}/xsp/GCodeSender.xsp
    ${LIBDIR}/xsp/Geometry.xsp
    ${LIBDIR}/xsp/GUI.xsp
    ${LIBDIR}/xsp/GUI_3DScene.xsp
    ${LIBDIR}/xsp/Layer.xsp
    ${LIBDIR}/xsp/Line.xsp
    ${LIBDIR}/xsp/Model.xsp
    ${LIBDIR}/xsp/MotionPlanner.xsp
    ${LIBDIR}/xsp/PerimeterGenerator.xsp
    ${LIBDIR}/xsp/PlaceholderParser.xsp
    ${LIBDIR}/xsp/Point.xsp
    ${LIBDIR}/xsp/Polygon.xsp
    ${LIBDIR}/xsp/Polyline.xsp
    ${LIBDIR}/xsp/PolylineCollection.xsp
    ${LIBDIR}/xsp/Print.xsp
    ${LIBDIR}/xsp/Surface.xsp
    ${LIBDIR}/xsp/SurfaceCollection.xsp
    ${LIBDIR}/xsp/TriangleMesh.xsp
    ${LIBDIR}/xsp/XS.xsp
)
foreach (file ${XS_XSP_FILES})
    if (MSVC)
        # Visual Studio C compiler has issues with FILE pragmas containing quotes.
        set(INCLUDE_COMMANDS "${INCLUDE_COMMANDS}INCLUDE_COMMAND: $^X -MExtUtils::XSpp::Cmd -e xspp -- -t ${CMAKE_CURRENT_LIST_DIR}/xsp/typemap.xspt ${file}\n")
    else ()
        set(INCLUDE_COMMANDS "${INCLUDE_COMMANDS}INCLUDE_COMMAND: $^X -MExtUtils::XSpp::Cmd -e xspp -- -t \"${CMAKE_CURRENT_LIST_DIR}/xsp/typemap.xspt\" \"${file}\"\n")
    endif ()
endforeach ()
configure_file(main.xs.in ${XS_MAIN_XS} @ONLY) # Insert INCLUDE_COMMANDS into main.xs

# Generate the Slic3r Perl module (XS) XS.cpp file.
#FIXME add the dependency on main.xs and typemap.
set(XS_MAIN_CPP ${CMAKE_CURRENT_BINARY_DIR}/XS.cpp)
add_custom_command(
        OUTPUT ${XS_MAIN_CPP}
        DEPENDS ${MyTemplate} ${MyTypemap}
        COMMAND COMMAND xsubpp -typemap typemap -output ${XS_MAIN_CPP} -hiertype ${XS_MAIN_XS}
)

# Define the Perl XS shared library.
add_library(XS SHARED 
    ${XS_MAIN_CPP}
    ${LIBDIR}/perlglue.cpp
    ${LIBDIR}/libslic3r/utils.cpp
)
target_link_libraries(XS libslic3r libslic3r_gui admesh clipper nowide polypartition poly2tri)
if(SLIC3R_DEBUG)
    target_link_libraries(Shiny)
endif()

# Add the OpenGL and GLU libraries.
if (SLIC3R_GUI)
    if (MSVC)
        target_link_libraries(XS OpenGL32.Lib GlU32.Lib)
    elseif (MINGW)
        target_link_libraries(XS -lopengl32)
    elseif (APPLE)
        target_link_libraries(XS -framework OpenGL)
    else ()
        target_link_libraries(XS -lGL -lGLU)
    endif ()
endif ()

target_include_directories(XS PRIVATE src src/libslic3r) # Local include directories
target_compile_definitions(XS PRIVATE -DSLIC3RXS)
set_target_properties(XS PROPERTIES PREFIX "") # Prevent cmake from generating libXS.so instead of XS.so

if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    target_link_libraries(XS -framework IOKit -framework CoreFoundation -lc++)
elseif (MSVC)
    target_link_libraries(XS )
else ()
    target_link_libraries(XS -lstdc++)
endif ()

# Windows specific stuff
if (WIN32)
    target_compile_definitions(XS PRIVATE -DNOGDI -DNOMINMAX -DHAS_BOOL)
endif ()

## Configuration flags
if (SLIC3R_GUI)
    message("Slic3r will be built with GUI support")
    target_compile_definitions(XS PRIVATE -DSLIC3R_GUI)
endif ()

if (SLIC3R_PROFILE)
    message("Slic3r will be built with a Shiny invasive profiler")
    target_compile_definitions(XS PRIVATE -DSLIC3R_PROFILE)
endif ()

if (SLIC3R_HAS_BROKEN_CROAK)
    target_compile_definitions(XS PRIVATE -DSLIC3R_HAS_BROKEN_CROAK)
endif ()

if (CMAKE_BUILD_TYPE MATCHES DEBUG)
    target_compile_definitions(XS PRIVATE -DSLIC3R_DEBUG -DDEBUG -D_DEBUG)
else ()
    target_compile_definitions(XS PRIVATE -DNDEBUG)
endif ()

# Perl specific stuff
find_package(PerlLibs REQUIRED)
set(PerlEmbed_DEBUG 1)
find_package(PerlEmbed REQUIRED)
target_include_directories(XS PRIVATE ${PERL_INCLUDE_PATH})
target_compile_options(XS PRIVATE ${PerlEmbed_CCFLAGS})
# If the Perl is compiled with optimization off, disable optimization over the whole project.
#if ("-Od" IN_LIST PerlEmbed_CCFLAGS OR "/Od" IN_LIST PerlEmbed_CCFLAGS)
#    set(CMAKE_CXX_FLAGS_RELEASE /Od)
#    set(CMAKE_C_FLAGS_RELEASE /Od)
#endif()
target_link_libraries(XS ${PERL_LIBRARY})

## REQUIRED packages

# Find and configure boost
if (SLIC3R_STATIC)
    # Use static boost libraries.
    set(Boost_USE_STATIC_LIBS ON)
    # Use boost libraries linked statically to the C++ runtime.
    # set(Boost_USE_STATIC_RUNTIME ON)
endif ()
find_package(Boost REQUIRED COMPONENTS system filesystem thread log locale)
if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(XS ${Boost_LIBRARIES})
    target_compile_definitions(XS PRIVATE -DBOOST_ASIO_DISABLE_KQUEUE -DBOOST_LIBS -DBOOST_ALL_NO_LIB)
    if (NOT SLIC3R_STATIC)
        target_compile_definitions(XS PRIVATE -DBOOST_LOG_DYN_LINK)
    endif ()
endif ()

# Find and configure intel-tbb
if(SLIC3R_STATIC)
    set(TBB_STATIC 1)
endif()
set(TBB_DEBUG 1)
find_package(TBB REQUIRED)
include_directories(${TBB_INCLUDE_DIRS})
add_definitions(${TBB_DEFINITIONS})
if(MSVC)
    # Suppress implicit linking of the TBB libraries by the Visual Studio compiler.
    add_definitions(-D__TBB_NO_IMPLICIT_LINKAGE)
endif()
target_link_libraries(XS ${TBB_LIBRARIES})

# Find and configure wxWidgets
if (SLIC3R_PRUSACONTROL)
    set(wxWidgets_UseAlienWx 1)
    if (wxWidgets_UseAlienWx)
        set(AlienWx_DEBUG 1)
        find_package(AlienWx REQUIRED COMPONENTS base)
        include_directories(${AlienWx_INCLUDE_DIRS})
        #add_compile_options(${AlienWx_CXX_FLAGS})
        add_definitions(${AlienWx_DEFINITIONS})
        target_link_libraries(XS ${AlienWx_LIBRARIES})
    else ()
        find_package(wxWidgets REQUIRED COMPONENTS base)
        include(${wxWidgets_USE_FILE})
        target_link_libraries(XS ${wxWidgets_LIBRARIES})
    endif ()
    target_compile_definitions(XS PRIVATE -DSLIC3R_GUI -DSLIC3R_PRUS)
endif()

## OPTIONAL packages

# Find eigen3 or use bundled version
if (NOT SLIC3R_STATIC)
    find_package(Eigen3)
endif ()
if (NOT Eigen3_FOUND)
    set(Eigen3_FOUND 1)
    set(EIGEN3_INCLUDE_DIR ${LIBDIR}/eigen/)
endif ()
include_directories(${EIGEN3_INCLUDE_DIR})

# Find expat or use bundled version
# Always use the system libexpat on Linux.
if (NOT SLIC3R_STATIC OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(EXPAT)
endif ()
if (NOT EXPAT_FOUND)
    add_library(expat STATIC
        ${LIBDIR}/expat/xmlparse.c
        ${LIBDIR}/expat/xmlrole.c
        ${LIBDIR}/expat/xmltok.c
    )
    set(EXPAT_FOUND 1)
    set(EXPAT_INCLUDE_DIRS ${LIBDIR}/expat/)
    set(EXPAT_LIBRARIES expat)
endif ()
include_directories(${EXPAT_INCLUDE_DIRS})
target_link_libraries(XS ${EXPAT_LIBRARIES})

# Find glew or use bundled version
if (NOT SLIC3R_STATIC)
    find_package(GLEW)
endif ()
if (NOT GLEW_FOUND)
    add_library(glew STATIC ${LIBDIR}/glew/src/glew.c)
    set(GLEW_FOUND 1)
    set(GLEW_INCLUDE_DIRS ${LIBDIR}/glew/include/)
    set(GLEW_LIBRARIES glew)
    target_compile_definitions(glew PRIVATE -DGLEW_STATIC)
    target_compile_definitions(XS PRIVATE -DGLEW_STATIC)
endif ()
include_directories(${GLEW_INCLUDE_DIRS})
target_link_libraries(XS ${GLEW_LIBRARIES})

# Installation
install(TARGETS XS DESTINATION lib/slic3r-prusa3d/auto/Slic3r/XS)
install(FILES lib/Slic3r/XS.pm DESTINATION lib/slic3r-prusa3d/Slic3r)
