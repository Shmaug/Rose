cmake_minimum_required(VERSION 3.23)

project(RoseEngine)

# compiler options
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(ROSE_ENABLE_TESTING ON CACHE BOOL "Build tests")
set(ROSE_BUILD_APPS     ON CACHE BOOL "Build default applications")

# Find dependencies

find_package(Vulkan REQUIRED)

add_compile_definitions(
	_USE_MATH_DEFINES
	GLM_FORCE_XYZW_ONLY
	IMGUI_DEFINE_MATH_OPERATORS
	VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

add_subdirectory(thirdparty/glfw)
add_subdirectory(thirdparty/glm)

# RoseLib target
file(GLOB_RECURSE CORE_SRCS  "${CMAKE_CURRENT_SOURCE_DIR}/src/Rose/**.cpp")
file(GLOB_RECURSE CORE_HPP   "${CMAKE_CURRENT_SOURCE_DIR}/src/Rose/**.hpp")
file(GLOB_RECURSE CORE_H     "${CMAKE_CURRENT_SOURCE_DIR}/src/Rose/**.h")
file(GLOB_RECURSE CORE_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/src/Rose/**.slang")
file(GLOB         IMGUI_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/*.cpp")

add_library(RoseLib)
set_target_properties(RoseLib PROPERTIES LINKER_LANGUAGE CXX)
target_sources(RoseLib
	PUBLIC
		${CORE_SRCS}
		${IMGUI_SRCS}
		thirdparty/ImGuizmo.cpp
		thirdparty/imnodes.cpp
		thirdparty/miniz.c
		thirdparty/pugixml.cpp
		thirdparty/tinyply.cpp
	PUBLIC FILE_SET HEADERS
	BASE_DIRS src/
	FILES
		${CORE_HPP}
		${CORE_H}
		${CORE_SLANG}
)

target_include_directories(RoseLib
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

target_link_directories(RoseLib PUBLIC $ENV{VULKAN_SDK}/Lib)

target_link_libraries(RoseLib Vulkan::Vulkan)
target_link_libraries(RoseLib glm glfw slang)

if(WIN32)
	target_link_libraries(RoseLib glfw $ENV{VULKAN_SDK}/Lib/slang.lib)
else()
	find_library(SLANG_LIB
	  NAMES slang         # CMake will automatically prepend "lib" and append ".so"
	  HINTS /usr/local/lib      # Optional, since /usr/lib is usually in the default search path
	)
	if(NOT SLANG_LIB)
	  message(FATAL_ERROR "Could not find the slang library.")
	else()
	  message(STATUS "Found slang library: ${SLANG_LIB}")
	endif()
	target_link_libraries(RoseLib glfw ${SLANG_LIB})
endif()
target_include_directories(RoseLib PRIVATE thirdparty/glfw/include thirdparty/glfw/deps)

if(UNIX)
    target_link_libraries(RoseLib pthread ${CMAKE_DL_LIBS} xcb xcb-keysyms)
    target_compile_definitions(RoseLib PUBLIC
		VK_USE_PLATFORM_XCB_KHR
		_GLFW_X11
	)
elseif(WIN32)
    target_compile_definitions(RoseLib PUBLIC
		VK_USE_PLATFORM_WIN32_KHR
		_GLFW_WIN32
		WIN32_LEAN_AND_MEAN
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
	)
    target_compile_options(RoseLib PUBLIC /bigobj /MP)
endif()

# Optional dependencies

find_package(assimp CONFIG)
if (assimp_FOUND)
	if (WIN32)
		target_link_libraries(RoseLib assimp::assimp)
	else()
		target_link_libraries(RoseLib assimp)
	endif()
    target_compile_definitions(RoseLib PUBLIC ENABLE_ASSIMP)
    message(STATUS "Assimp enabled")
endif()

find_package(OpenVDB CONFIG)
if (OpenVDB_FOUND)
    target_link_libraries(RoseLib OpenVDB::openvdb)
    target_compile_definitions(RoseLib PRIVATE ENABLE_OPENVDB)
    message(STATUS "OpenVDB enabled")
endif()

# Test and app targets

if (ROSE_BUILD_APPS)
	add_executable(SceneApp src/SceneApp.cpp)
	target_link_libraries(SceneApp PRIVATE RoseLib)

	add_executable(WorkGraphApp src/WorkGraphApp.cpp)
	target_link_libraries(WorkGraphApp PRIVATE RoseLib)
endif()

if (ROSE_ENABLE_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

# misc

configure_file("${CMAKE_CURRENT_LIST_DIR}/thirdparty/imgui/misc/fonts/DroidSans.ttf" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DroidSans.ttf" COPYONLY)

install(TARGETS RoseLib FILE_SET HEADERS)
