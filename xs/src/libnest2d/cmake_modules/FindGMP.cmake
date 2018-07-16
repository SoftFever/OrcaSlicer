# Try to find the GMP libraries:
# GMP_FOUND - System has GMP lib
# GMP_INCLUDE_DIR - The GMP include directory
# GMP_LIBRARIES - Libraries needed to use GMP

if (GMP_INCLUDE_DIR AND GMP_LIBRARIES)
	# Force search at every time, in case configuration changes
	unset(GMP_INCLUDE_DIR CACHE)
	unset(GMP_LIBRARIES CACHE)
endif (GMP_INCLUDE_DIR AND GMP_LIBRARIES)

find_path(GMP_INCLUDE_DIR NAMES gmp.h)

if(WIN32)
	find_library(GMP_LIBRARIES NAMES libgmp.a gmp gmp.lib mpir mpir.lib)
else(WIN32)
	if(STBIN)
		message(STATUS "STBIN: ${STBIN}")
		find_library(GMP_LIBRARIES NAMES libgmp.a gmp)
	else(STBIN)
		find_library(GMP_LIBRARIES NAMES libgmp.so gmp)
	endif(STBIN)
endif(WIN32)

if(GMP_INCLUDE_DIR AND GMP_LIBRARIES)
   set(GMP_FOUND TRUE)
endif(GMP_INCLUDE_DIR AND GMP_LIBRARIES)

if(GMP_FOUND)
	message(STATUS "Configured GMP: ${GMP_LIBRARIES}")
else(GMP_FOUND)
	message(STATUS "Could NOT find GMP")
endif(GMP_FOUND)

mark_as_advanced(GMP_INCLUDE_DIR GMP_LIBRARIES)