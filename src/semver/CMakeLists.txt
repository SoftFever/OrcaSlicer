cmake_minimum_required(VERSION 2.8.12)
project(semver)

add_library(semver STATIC
    semver.c
    semver.h
)

encoding_check(semver)
