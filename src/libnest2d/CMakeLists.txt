set(LIBNEST2D_SRCFILES
    include/libnest2d/libnest2d.hpp
    include/libnest2d/nester.hpp
    include/libnest2d/geometry_traits.hpp
    include/libnest2d/geometry_traits_nfp.hpp
    include/libnest2d/common.hpp
    include/libnest2d/optimizer.hpp
    include/libnest2d/utils/metaloop.hpp
    include/libnest2d/utils/rotfinder.hpp
    include/libnest2d/utils/rotcalipers.hpp
    include/libnest2d/placers/placer_boilerplate.hpp
    include/libnest2d/placers/bottomleftplacer.hpp
    include/libnest2d/placers/nfpplacer.hpp
    include/libnest2d/selections/selection_boilerplate.hpp
    #include/libnest2d/selections/filler.hpp
    include/libnest2d/selections/firstfit.hpp
    #include/libnest2d/selections/djd_heuristic.hpp
    include/libnest2d/backends/clipper/geometries.hpp
    include/libnest2d/backends/clipper/clipper_polygon.hpp
    include/libnest2d/optimizers/nlopt/nlopt_boilerplate.hpp
    include/libnest2d/optimizers/nlopt/simplex.hpp
    include/libnest2d/optimizers/nlopt/subplex.hpp
    include/libnest2d/optimizers/nlopt/genetic.hpp
    src/libnest2d.cpp
    )

add_library(libnest2d ${LIBNEST2D_SRCFILES})

target_include_directories(libnest2d PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(libnest2d PUBLIC clipper NLopt::nlopt TBB::tbb Boost::boost)
target_compile_definitions(libnest2d PUBLIC USE_TBB LIBNEST2D_STATIC LIBNEST2D_OPTIMIZER_nlopt LIBNEST2D_GEOMETRIES_clipper)
