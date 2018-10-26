find_package(Armadillo REQUIRED)

add_library(OptimlibOptimizer INTERFACE)
target_include_directories(OptimlibOptimizer INTERFACE ${ARMADILLO_INCLUDE_DIRS})
target_link_libraries(OptimlibOptimizer INTERFACE ${ARMADILLO_LIBRARIES})