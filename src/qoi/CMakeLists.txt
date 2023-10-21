# PrusaSlicer specific CMake

cmake_minimum_required(VERSION 2.8.12)
project(qoi)

add_library(qoi STATIC
    qoi.h
    qoilib.c
)
