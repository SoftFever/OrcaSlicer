# Enable c++11 language standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add our own cmake module path
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/)

set(LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/src/)
include_directories(${LIBDIR})

# Bring back SLIC3R_DYNAMIC logic from Build.PL
if (DEFINED SLIC3R_DYNAMIC AND NOT SLIC3R_DYNAMIC)
    set(SLIC3R_STATIC 1)
endif ()

# Bring back SLIC3R_NOGUI logic from Build.PL
# Map SLIC3R_PRUS to SLIC3R_GUI
#if (DEFINED SLIC3R_NOGUI AND NOT SLIC3R_NOGUI OR SLIC3R_PRUS)
#    set(SLIC3R_GUI 0)
#else ()
    set(SLIC3R_GUI 1)
#endif ()

# Generate XS typemap file
find_package(Perl REQUIRED)
set(MyTypemap ${CMAKE_CURRENT_BINARY_DIR}/typemap)
add_custom_command(
        OUTPUT ${MyTypemap}
        COMMAND ${PERL_EXECUTABLE} -MExtUtils::Typemaps -MExtUtils::Typemaps::Basic -e "$typemap = ExtUtils::Typemaps->new(file => \"${CMAKE_CURRENT_LIST_DIR}/xsp/my.map\"); $typemap->merge(typemap => ExtUtils::Typemaps::Basic->new); $typemap->write(file => \"${MyTypemap}\")"
        VERBATIM
)

# Generate main.xs file
set(MyMainXs ${CMAKE_CURRENT_BINARY_DIR}/main.xs)
file(GLOB files xsp/*.xsp)
foreach (file ${files})
    if (MSVC)
        # Visual Studio C compiler has issues with FILE pragmas containing quotes.
        set(INCLUDE_COMMANDS "${INCLUDE_COMMANDS}INCLUDE_COMMAND: $^X -MExtUtils::XSpp::Cmd -e xspp -- -t ${CMAKE_CURRENT_LIST_DIR}/xsp/typemap.xspt ${file}\n")
    else ()
        set(INCLUDE_COMMANDS "${INCLUDE_COMMANDS}INCLUDE_COMMAND: $^X -MExtUtils::XSpp::Cmd -e xspp -- -t \"${CMAKE_CURRENT_LIST_DIR}/xsp/typemap.xspt\" \"${file}\"\n")
    endif ()
endforeach ()
configure_file(main.xs.in ${MyMainXs} @ONLY) # Insert INCLUDE_COMMANDS into main.xs

# Generate XS.cpp file
set(MyXsC "${CMAKE_CURRENT_BINARY_DIR}/XS.cpp")
add_custom_command(
        OUTPUT ${MyXsC}
        DEPENDS ${MyTemplate} ${MyTypemap}
        COMMAND COMMAND xsubpp -typemap typemap -output ${MyXsC} -hiertype ${MyMainXs}
)

# Find and define all source files
file(GLOB MySrc src/*.cpp)
file(GLOB MyAdmesh src/admesh/*.cpp)
SET_SOURCE_FILES_PROPERTIES(${MyAdmesh} PROPERTIES LANGUAGE CXX) # admesh has C++ syntax in it's source but the files are *.c

file(GLOB MyGuiSrc src/slic3r/GUI/*.cpp)
file(GLOB_RECURSE MyLibSlic3rSrc src/libslic3r/*.cpp)
file(GLOB_RECURSE MyPoly2TriSrc src/poly2tri/*.cc)
file(GLOB MyShinySrc src/Shiny/*.c)

# Define target file
add_library(XS SHARED ${MyXsC} ${MySrc} ${MyAdmesh} ${MyGuiSrc} ${MyLibSlic3rSrc} ${MyPoly2TriSrc} ${MyShinySrc})

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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE")
    target_link_libraries(XS -framework IOKit -framework CoreFoundation -lc++)
elseif (MSVC)
    target_link_libraries(XS )
else ()
    target_link_libraries(XS -lc++)
endif ()

# Windows specific stuff
if (WIN32)
    target_compile_definitions(XS PRIVATE -D_USE_MATH_DEFINES -DNOGDI -DNOMINMAX -D_WIN32 -DHAS_BOOL)
endif ()

## Configuration flags
if (SLIC3R_GUI)
    message("Slic3r will be built with GUI support")
#    target_compile_definitions(XS PRIVATE -DSLIC3R_GUI -DSLIC3R_PRUS)
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
execute_process(
    COMMAND ${PERL_EXECUTABLE} -MExtUtils::Embed -e "
        use Text::ParseWords; 
        my \$line; 
        { local *STDOUT; open STDOUT, '>', \\\$line; ccflags; } 
        \$line =~ s/\\\\/\\\\\\\\/g; 
        my @words = shellwords(\$line); 
        print join(';', @words)"
    OUTPUT_VARIABLE PERL_CCLAGS
)
execute_process(
    COMMAND ${PERL_EXECUTABLE} -MExtUtils::Embed -e "
        use Text::ParseWords; 
        my \$line; 
        { local *STDOUT; open STDOUT, '>', \\\$line; ldopts; } 
        \$line =~ s/\\\\/\\\\\\\\/g; 
        my @words = shellwords(\$line); 
        print join(';', @words)"
    OUTPUT_VARIABLE PERL_LDFLAGS
)
target_include_directories(XS PRIVATE ${PERL_INCLUDE_PATH})

message("PERL_INCLUDE_PATH: ${PERL_INCLUDE_PATH}")
message("PERL_LIBRARY: ${PERL_LIBRARY}")
message("PERL_EXECUTABLE: ${PERL_EXECUTABLE}")
message("PERL_SITESEARCH: ${PERL_SITESEARCH}")
message("PERL_SITELIB: ${PERL_SITELIB}")
message("PERL_VENDORARCH: ${PERL_VENDORARCH}")
message("PERL_VENDORLIB: ${PERL_VENDORLIB}")
message("PERL_ARCHLIB: ${PERL_ARCHLIB}")
message("PERL_PRIVLIB: ${PERL_PRIVLIB}")
message("PERL_EXTRA_C_FLAGS: ${PERL_EXTRA_C_FLAGS}")
message("PERL_CCLAGS: ${PERL_CCLAGS}")
message("PERL_LDFLAGS: ${PERL_LDFLAGS}")

target_compile_options(XS PRIVATE ${PERL_CCLAGS})
#target_link_libraries(XS ${PERL_LDFLAGS})
target_link_libraries(XS C:\\wperl64d\\lib\\CORE\\perl524.lib)

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
find_package(TBB REQUIRED)
if (TBB_FOUND)
    include_directories(${TBB_INCLUDE_DIRS})
    add_definitions(${TBB_DEFINITIONS})
    target_link_libraries(XS ${TBB_LIBRARIES})
endif ()

# Find and configure wxWidgets
set(wxWidgets_UseAlienWx 1)
if (wxWidgets_UseAlienWx)
    # ${PROJECT_SOURCE_DIR}/cmake/helpers/alien_wx_to_cmake.pl
else ()
    find_package(wxWidgets REQUIRED)
    if (wxWidgets_FOUND)
        include(${wxWidgets_USE_FILE})
        target_link_libraries(XS ${wxWidgets_LIBRARIES})
    endif ()
endif ()

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
if (NOT SLIC3R_STATIC)
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
target_link_libraries(XS nowide)

# Installation
install(TARGETS XS DESTINATION lib/slic3r-prusa3d/auto/Slic3r/XS)
install(FILES lib/Slic3r/XS.pm DESTINATION lib/slic3r-prusa3d/Slic3r)
