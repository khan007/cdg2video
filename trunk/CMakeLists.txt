
PROJECT(cdg2video)
SET(PACKAGE cdg2video)
SET(VERSION 0.6)

cmake_minimum_required(VERSION 2.6)

#if you don't want the full compiler output, remove the following line
SET(CMAKE_VERBOSE_MAKEFILE ON)

#create config.h
CONFIGURE_FILE(
${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake 
${CMAKE_CURRENT_BINARY_DIR}/config.h
)

#add definitions, compiler switches, etc.
ADD_DEFINITIONS("-DHAVE_CONFIG_H")
ADD_DEFINITIONS(-Wall -O2)
#ADD_DEFINITIONS(-O0 -g3)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

#check required functions
#INCLUDE(CheckFunctionExists)
#CHECK_FUNCTION_EXISTS(func_name HAVE_func_name)

#list all source files here
ADD_EXECUTABLE(cdg2video main.cpp cdgfile.cpp help.cpp utils.cpp cdgio.cpp)

#Linking...
FIND_LIBRARY(LIB_SWSCALE  swscale)
FIND_LIBRARY(LIB_AVCODEC  avcodec)
FIND_LIBRARY(LIB_AVFORMAT avformat)
FIND_LIBRARY(LIB_AVUTIL   avutil)
FIND_LIBRARY(LIB_ZIP      zip)
FIND_LIBRARY(LIB_SWRESAMPLE  swresample)

TARGET_LINK_LIBRARIES(cdg2video ${LIB_AVCODEC} ${LIB_AVFORMAT} ${LIB_AVUTIL} ${LIB_SWSCALE} ${LIB_ZIP} ${LIB_SWRESAMPLE})

#install location
INSTALL(TARGETS ${PACKAGE} RUNTIME DESTINATION bin)
INSTALL(PROGRAMS cdg2video-player DESTINATION bin)

# Packing stuff (make package_source)
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CDG+MP3 to video converter")
SET(CPACK_PACKAGE_VENDOR "Nikolay Nikolov")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "6")
SET(CPACK_PACKAGE_VERSION_PATCH "0")
SET(CPACK_GENERATOR TGZ)
SET(CPACK_SOURCE_PACKAGE_FILE_NAME "${PACKAGE}-${VERSION}")
SET(CPACK_SOURCE_GENERATOR TGZ)
SET(CPACK_SOURCE_IGNORE_FILES "\\\\.svn;CMakeFiles;CMakeCache.txt;CPack*;progress.make;cmake_install.cmake;Makefile;config.h$;~$;gz$;kdevelop$;kdevelop.filelist$;kdevelop.pcs$;kdevses$;install_manifest.txt;Doxyfile;${PROJECT_SOURCE_DIR}/${PACKAGE}")
INCLUDE(CPack)

