project(semver)
cmake_minimum_required(VERSION 2.6)

add_library(semver STATIC
    semver.c
    semver.h
)

encoding_check(semver)
