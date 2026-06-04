# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
# cmake_minimum_required(VERSION 3.16)

# include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# project(test_cam_3)

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add managed_components only if it exists
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/managed_components")
    set(EXTRA_COMPONENT_DIRS managed_components)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_cam_3)