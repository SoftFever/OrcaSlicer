project(nowide)
cmake_minimum_required(VERSION 2.6)

add_library(nowide STATIC
    nowide/args.hpp
    nowide/cenv.hpp
    nowide/config.hpp
    nowide/convert.hpp
    nowide/cstdio.hpp
    nowide/cstdlib.hpp
    nowide/filebuf.hpp
    nowide/fstream.hpp
    nowide/integration/filesystem.hpp
    nowide/iostream.cpp
    nowide/iostream.hpp
    nowide/stackstring.hpp
    nowide/system.hpp
    nowide/utf8_codecvt.hpp
    nowide/windows.hpp
)

target_link_libraries(nowide PUBLIC boost_headeronly)


