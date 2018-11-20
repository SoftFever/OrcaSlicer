project(poly2tri)
cmake_minimum_required(VERSION 2.6)

add_library(poly2tri STATIC
    common/shapes.cc
    common/shapes.h
    common/utils.h
    poly2tri.h
    sweep/advancing_front.cc
    sweep/advancing_front.h
    sweep/cdt.cc
    sweep/cdt.h
    sweep/sweep.cc
    sweep/sweep.h
    sweep/sweep_context.cc
    sweep/sweep_context.h
)
