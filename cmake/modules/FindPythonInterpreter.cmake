# Copyright 2021, Robert Adam. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# source tree.

cmake_minimum_required(VERSION 3.5)

function(find_python_interpreter)
	set(options REQUIRED EXACT)
	set(oneValueArgs VERSION INTERPRETER_OUT_VAR VERSION_OUT_VAR)
	set(multiValueArgs HINTS)
	cmake_parse_arguments(FIND_PYTHON_INTERPRETER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})


	# Error handling
	if (FIND_PYTHON_INTERPRETER_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "Unrecognized arguments to find_python_interpreter: \"${FIND_PYTHON_INTERPRETER_UNPARSED_ARGUMENTS}\"")
	endif()
	if (NOT FIND_PYTHON_INTERPRETER_INTERPRETER_OUT_VAR)
		message(FATAL_ERROR "Called find_python_interpreter without the INTERPRETER_OUT_VAR parameter!")
	endif()
	if (FIND_PYTHON_INTERPRETER_EXACT AND NOT FIND_PYTHON_INTERPRETER_VERSION)
		message(FATAL_ERROR "Specified EXACT but did not specify VERSION!")
	endif()


	# Defaults
	if (NOT FIND_PYTHON_INTERPRETER_VERSION)
		set(FIND_PYTHON_INTERPRETER_VERSION "0.0.0")
	endif()
	if (NOT FIND_PYTHON_INTERPRETER_HINTS)
		set(FIND_PYTHON_INTERPRETER_HINTS "")
	endif()


	# Validate
	if (NOT FIND_PYTHON_INTERPRETER_VERSION MATCHES "^[0-9]+(\.[0-9]+(\.[0-9]+)?)?$")
		message(FATAL_ERROR "Invalid VERSION \"FIND_PYTHON_INTERPRETER_VERSION\" - must follow RegEx \"^[0-9]+(\.[0-9]+(\.[0-9]+)?)?$\"")
	endif()


	# "parse" version (first append 0.0.0 in case only a part of the version scheme was set by the user)
	string(CONCAT VERSION_HELPER "${FIND_PYTHON_INTERPRETER_VERSION}" ".0.0.0")
	string(REPLACE "." ";" VERSION_LIST "${VERSION_HELPER}")
	list(GET VERSION_LIST 0 FIND_PYTHON_INTERPRETER_VERSION_MAJOR)
	list(GET VERSION_LIST 1 FIND_PYTHON_INTERPRETER_VERSION_MINOR)
	list(GET VERSION_LIST 1 FIND_PYTHON_INTERPRETER_VERSION_PATCH)


	# Create names for the interpreter to search for
	set(INTERPRETER_NAMES "")
	# Orca: Support pyenv-windows
	if(WIN32)
		list(APPEND INTERPRETER_NAMES "python.bat")
	endif()
	if (FIND_PYTHON_INTERPRETER_VERSION_MAJOR STREQUAL "0")
		# Search for either Python 2 or 3
		list(APPEND INTERPRETER_NAMES "python3")
		list(APPEND INTERPRETER_NAMES "python")
		list(APPEND INTERPRETER_NAMES "python2")
	else()
		# Search for specified version
		list(APPEND INTERPRETER_NAMES "python${FIND_PYTHON_INTERPRETER_VERSION_MAJOR}")
		list(APPEND INTERPRETER_NAMES "python")

		if (NOT FIND_PYTHON_INTERPRETER_VERSION_MINOR EQUAL 0)
			list(PREPEND INTERPRETER_NAMES "python${FIND_PYTHON_INTERPRETER_VERSION_MAJOR}.${FIND_PYTHON_INTERPRETER_VERSION_MINOR}")

			if (NOT FIND_PYTHON_INTERPRETER_VERSION_PATCH EQUAL 0)
				list(PREPEND INTERPRETER_NAMES
					"python${FIND_PYTHON_INTERPRETER_VERSION_MAJOR}.${FIND_PYTHON_INTERPRETER_VERSION_MINOR}.${FIND_PYTHON_INTERPRETER_VERSION_PATCH}")
			endif()
		endif()
	endif()


	# Start by trying to search for a python executable in PATH and HINTS
	find_program(PYTHON_INTERPRETER NAMES ${INTERPRETER_NAMES} HINTS ${FIND_PYTHON_INTERPRETER_HINTS})
	message(STATUS "Python found: ${PYTHON_INTERPRETER}")

	if (NOT PYTHON_INTERPRETER)
		# Fall back to find_package
		message(VERBOSE "Can't find Python interpreter in PATH -> Falling back to find_package")
		if (FIND_PYTHON_INTERPRETER_VERSION_MAJOR EQUAL 0)
			# Search arbitrary version
			find_package(Python COMPONENTS Interpreter QUIET)
			set(PYTHON_INTERPRETER "${Python_EXECUTABLE}")
		else()
			# Search specific version (Python 2 or 3)
			find_package(Python${FIND_PYTHON_INTERPRETER_VERSION_MAJOR} COMPONENTS Interpreter QUIET)
			set(PYTHON_INTERPRETER "${Python${FIND_PYTHON_INTERPRETER_VERSION_MAJOR}_EXECUTABLE}")
		endif()
	endif()


	if (PYTHON_INTERPRETER)
		# Verify that the version found is the one that is wanted
		execute_process(
			COMMAND ${PYTHON_INTERPRETER} "--version"
			OUTPUT_VARIABLE PYTHON_INTERPRETER_VERSION
			ERROR_VARIABLE PYTHON_INTERPRETER_VERSION # Python 2 reports the version on stderr
		)

		# Remove leading "Python " from version information
		string(REPLACE "Python " "" PYTHON_INTERPRETER_VERSION "${PYTHON_INTERPRETER_VERSION}")
		string(STRIP "${PYTHON_INTERPRETER_VERSION}" PYTHON_INTERPRETER_VERSION)


		if (PYTHON_INTERPRETER_VERSION VERSION_LESS FIND_PYTHON_INTERPRETER_VERSION)
			message(STATUS "Found Python version ${PYTHON_INTERPRETER_VERSION} but required at least ${FIND_PYTHON_INTERPRETER_VERSION}")
			set(PYTHON_INTERPRETER "NOTFOUND")
			set(PYTHON_INTERPRETER_VERSION "NOTFOUND")
		elseif(PYTHON_INTERPRETER_VERSION VERSION_GREATER FIND_PYTHON_INTERPRETER_VERSION AND FIND_PYTHON_INTERPRETER_EXACT)
			message(STATUS "Found Python interpreter version ${PYTHON_INTERPRETER_VERSION} but required exactly ${FIND_PYTHON_INTERPRETER_VERSION}")
			set(PYTHON_INTERPRETER "NOTFOUND")
			set(PYTHON_INTERPRETER_VERSION "NOTFOUND")
		else()
			message(STATUS "Found Python interpreter version ${PYTHON_INTERPRETER_VERSION}")
		endif()
	else()
		set(PYTHON_INTERPRETER_VERSION "NOTFOUND")
	endif()


	# Set "return" values
	set(${FIND_PYTHON_INTERPRETER_INTERPRETER_OUT_VAR} "${PYTHON_INTERPRETER}" PARENT_SCOPE)
	if (FIND_PYTHON_INTERPRETER_VERSION_OUT_VAR)
		set(${FIND_PYTHON_INTERPRETER_VERSION_OUT_VAR} "${PYTHON_INTERPRETER_VERSION}" PARENT_SCOPE)
	endif()

	if (NOT PYTHON_INTERPRETER AND FIND_PYTHON_INTERPRETER_REQUIRED)
		message(FATAL_ERROR "Did NOT find Python interpreter")
	endif()
endfunction()

