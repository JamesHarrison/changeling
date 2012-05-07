
cmake_minimum_required (VERSION 2.6)
## Project setup
project (changeling)
set (changeling_VERSION_MAJOR 0)
set (changeling_VERSION_MINOR 1)
set (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")
set ( CMAKE_VERBOSE_MAKEFILE OFF )
set ( CMAKE_BUILD_TYPE Debug )
# Warn about everything. Beautiful code doesn't warn.
set (CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} "-Wl,-lprofiler")
set (CMAKE_CXX_FLAGS "-Wall")

# Optimisations - can cause issues.
include (OptimizeForArchitecture)
OptimizeForArchitecture()
find_package (Jack REQUIRED)
find_package (Sndfile REQUIRED)
find_package (Mosquitto REQUIRED)
include_directories (includes)
include_directories (${SNDFILE_INCLUDE_DIR})
include_directories (${JACK_INCLUDE_DIRS})
include_directories (${MOSQUITTO_INCLUDE_DIR})

# Packaging

SET(CPACK_GENERATOR "DEB;TGZ;ZIP;STGZ;RPM")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Changeling is a broadcast profanity delay for the JACK Audio Connection Kit.")
SET(CPACK_PACKAGE_VENDOR "James Harrison <james at talkunafraid dot co dot uk>")
SET(CPACK_PACKAGE_CONTACT "James Harrison <james at talkunafraid dot co dot uk>")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.markdown")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.markdown")
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "1")
SET(CPACK_PACKAGE_VERSION_PATCH "0")
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libjack (>=0.1), libsndfile (>=0.1)")
INCLUDE(CPack)
INCLUDE(InstallRequiredSystemLibraries)
# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
add_custom_target(doc
${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
COMMENT "Generating API documentation with Doxygen" VERBATIM
)
endif(DOXYGEN_FOUND)


add_executable (changeling src/bin/changeling.cpp)
target_link_libraries (changeling ${MOSQUITTO_LIBRARIES} ${SNDFILE_LIBRARIES} ${JACK_LIBRARIES})
INSTALL(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/changeling DESTINATION bin)