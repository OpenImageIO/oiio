# Copyright (c) 2012-2016 DreamWorks Animation LLC
#
# All rights reserved. This software is distributed under the
# Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
#
# Redistributions of source code must retain the above copyright
# and license notice and the following restrictions and disclaimer.
#
# *     Neither the name of DreamWorks Animation nor the names of
# its contributors may be used to endorse or promote products derived
# from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
# LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
#

#-*-cmake-*-
# - Find TBB
#
# Author : Nicholas Yue yue.nicholas@gmail.com
#
# This auxiliary CMake file helps in find the TBB headers and libraries
#
# TBB_FOUND                  set if TBB is found.
# TBB_INCLUDE_DIR            TBB's include directory
# TBB_tbb_LIBRARY            TBB libraries
# TBB_tbb_preview_LIBRARY    TBB_preview libraries (Mulitple Rendering Context)
# TBB_tbbmalloc_LIBRARY      TBBmalloc libraries (Mulitple Rendering Context)

FIND_PACKAGE ( PackageHandleStandardArgs )

# SET ( TBB_FOUND FALSE )

FIND_PATH( TBB_LOCATION include/tbb/tbb.h
  "$ENV{TBB_ROOT}"
  NO_CMAKE_ENVIRONMENT_PATH
  NO_SYSTEM_ENVIRONMENT_PATH
  NO_CMAKE_SYSTEM_PATH
  )

FIND_PACKAGE_HANDLE_STANDARD_ARGS ( TBB
  REQUIRED_VARS TBB_LOCATION
  )

IF ( TBB_FOUND )

  SET( TBB_INCLUDE_DIR "${TBB_LOCATION}/include" CACHE STRING "TBB include directory")

  IF (APPLE)
	IF (TBB_FOR_CLANG)
      SET ( TBB_LIBRARYDIR ${TBB_LOCATION}/lib/libc++ CACHE STRING "TBB library directory")
	ELSE ()
      SET ( TBB_LIBRARYDIR ${TBB_LOCATION}/lib CACHE STRING "TBB library directory")
	ENDIF ()
	SET(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
	FIND_LIBRARY ( TBB_LIBRARY_PATH tbb PATHS ${TBB_LIBRARYDIR} )
	FIND_LIBRARY ( TBB_PREVIEW_LIBRARY_PATH tbb_preview PATHS ${TBB_LIBRARYDIR} )
	FIND_LIBRARY ( TBBMALLOC_LIBRARY_PATH tbbmalloc PATHS ${TBB_LIBRARYDIR} )
	LIST ( APPEND TBB_LIBRARIES_LIST ${TBB_LIBRARY_PATH} ${TBBmx_LIBRARY_PATH} )
  ELSEIF (WIN32)
	IF (MSVC10)
      SET ( TBB_VC_DIR vc10 )
	ELSEIF (MSVC11)
      SET ( TBB_VC_DIR vc11 )
	ELSEIF (MSVC12)
      SET ( TBB_VC_DIR vc12 )
	ENDIF ( MSVC10)
	#  MESSAGE ( "TBB_VC_DIR = ${TBB_VC_DIR}" )
	SET (TBB_PATH_SUFFIXES intel64/${TBB_VC_DIR} )
	FIND_LIBRARY ( TBB_LIBRARY_PATH tbb PATHS ${TBB_LIBRARYDIR} PATH_SUFFIXES ${TBB_PATH_SUFFIXES})
	FIND_LIBRARY ( TBB_PREVIEW_LIBRARY_PATH tbb_preview PATHS ${TBB_LIBRARYDIR}  PATH_SUFFIXES ${TBB_PATH_SUFFIXES})
	FIND_LIBRARY ( TBBMALLOC_LIBRARY_PATH tbbmalloc PATHS ${TBB_LIBRARYDIR}  PATH_SUFFIXES ${TBB_PATH_SUFFIXES})
	LIST ( APPEND TBB_LIBRARIES_LIST ${TBB_LIBRARY_PATH} ${TBBmx_LIBRARY_PATH} )
  ELSE (APPLE)
	# MESSAGE ( "CMAKE_COMPILER_IS_GNUCXX = ${CMAKE_COMPILER_IS_GNUCXX}")
    SET ( TBB_LIBRARYDIR ${TBB_LOCATION}/lib CACHE STRING "TBB library directory")
    # If compiling with clang, make sure libstdc++ is being used
    STRING(FIND "${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES}" "stdc++" _tbb_using_libstdcxx)
	IF (("${CMAKE_COMPILER_IS_GNUCXX}") OR (NOT ${_tbb_using_libstdcxx} LESS "0"))
	  IF ( TBB_MATCH_COMPILER_VERSION )
		STRING(REGEX MATCHALL "[0-9]+" GCC_VERSION_COMPONENTS ${CMAKE_CXX_COMPILER_VERSION})
		LIST(GET GCC_VERSION_COMPONENTS 0 GCC_MAJOR)
		LIST(GET GCC_VERSION_COMPONENTS 1 GCC_MINOR)
		# MESSAGE(STATUS ${GCC_MAJOR})
		# MESSAGE(STATUS ${GCC_MINOR})
		SET ( TBB_PATH_SUFFIXES intel64/gcc${GCC_MAJOR}.${GCC_MINOR} x86_64-linux-gnu )
	  ELSEIF (NOT ${_tbb_using_libstdcxx} LESS "0")
		SET ( TBB_PATH_SUFFIXES intel64/gcc4.7 x86_64-linux-gnu )
	  ELSE ()
		SET ( TBB_PATH_SUFFIXES intel64/gcc4.4 x86_64-linux-gnu )
	  ENDIF ()
	ELSE ()
      MESSAGE ( FATAL_ERROR "Can't handle non-GCC compiler")
	ENDIF ()
	FIND_LIBRARY ( TBB_LIBRARY_PATH tbb PATHS ${TBB_LIBRARYDIR} PATH_SUFFIXES ${TBB_PATH_SUFFIXES}
      NO_DEFAULT_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      NO_CMAKE_PATH
      NO_SYSTEM_ENVIRONMENT_PATH
      NO_CMAKE_SYSTEM_PATH
	  )
	FIND_LIBRARY ( TBB_PREVIEW_LIBRARY_PATH tbb_preview PATHS ${TBB_LIBRARYDIR} PATH_SUFFIXES ${TBB_PATH_SUFFIXES}
      NO_DEFAULT_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      NO_CMAKE_PATH
      NO_SYSTEM_ENVIRONMENT_PATH
      NO_CMAKE_SYSTEM_PATH
	  )
	FIND_LIBRARY ( TBBMALLOC_LIBRARY_PATH tbbmalloc PATHS ${TBB_LIBRARYDIR} PATH_SUFFIXES ${TBB_PATH_SUFFIXES}
      NO_DEFAULT_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      NO_CMAKE_PATH
      NO_SYSTEM_ENVIRONMENT_PATH
      NO_CMAKE_SYSTEM_PATH
	  )
	LIST ( APPEND TBB_LIBRARIES_LIST ${TBB_LIBRARY_PATH} ${TBBmx_LIBRARY_PATH} )
  ENDIF (APPLE)

  GET_FILENAME_COMPONENT ( TBB_LIBRARYDIR ${TBB_LIBRARY_PATH} PATH CACHE )

  SET( Tbb_TBB_LIBRARY ${TBB_LIBRARY_PATH} CACHE STRING "tbb library")
  SET( Tbb_TBB_PREVIEW_LIBRARY ${TBB_PREVIEW_LIBRARY_PATH} CACHE STRING "tbb_preview library")
  SET( Tbb_TBBMALLOC_LIBRARY ${TBBMALLOC_LIBRARY_PATH} CACHE STRING "tbbmalloc library")

ENDIF ( TBB_FOUND )
