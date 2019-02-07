project(agg_svg)
cmake_minimum_required(VERSION 2.6)

add_library(agg_svg STATIC 
	agg_svg_exception.h
	agg_svg_parser.cpp
	agg_svg_parser.h
	agg_svg_path_renderer.cpp
	agg_svg_path_renderer.h
	agg_svg_path_tokenizer.cpp
	agg_svg_path_tokenizer.h

#	agg_bezier_arc.cpp
	agg_curves.cpp
	agg_trans_affine.cpp
	agg_vcgen_contour.cpp
	agg_vcgen_stroke.cpp
	)

target_include_directories(agg_svg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(agg_svg ${EXPAT_LIBRARIES})
