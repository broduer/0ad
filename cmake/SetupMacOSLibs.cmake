include(ExternalProject)

# Add the path where cmake can find the prebuild libraries
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/link_libraries)
