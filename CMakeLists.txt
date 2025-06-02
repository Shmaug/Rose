cmake_minimum_required(VERSION 3.23)

project(RoseEngine)

set(ROSE_ENABLE_TESTING ON CACHE BOOL "Build tests")
set(ROSE_BUILD_APPS     ON CACHE BOOL "Build default applications")
set(SLANG_BUILD_DIR     "" CACHE PATH "Optional path to Slang build directory containing bin/ include/ and lib/. If empty, the Vulkan SDK Slang will be used instead.")

# Compiler options

set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_definitions(
	_USE_MATH_DEFINES
	GLM_FORCE_XYZW_ONLY
	IMGUI_DEFINE_MATH_OPERATORS
	VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

# Dependencies

find_package(Vulkan REQUIRED)

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
		thirdparty/implot_items.cpp
		thirdparty/implot.cpp
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

target_link_libraries(RoseLib Vulkan::Vulkan glm glfw slang)
target_include_directories(RoseLib PRIVATE thirdparty/glfw/include thirdparty/glfw/deps)

# Slang

if(EXISTS ${SLANG_BUILD_DIR})
	target_include_directories(RoseLib PUBLIC ${SLANG_BUILD_DIR}/include)
	target_link_directories(RoseLib PUBLIC ${SLANG_BUILD_DIR}/lib)
	if(WIN32)
		configure_file("${SLANG_BUILD_DIR}/bin/slang.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang.dll" COPYONLY)
		configure_file("${SLANG_BUILD_DIR}/bin/slang.pdb" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang.pdb" COPYONLY)
	endif()
else()
	target_include_directories(RoseLib PUBLIC $ENV{VULKAN_SDK}/include/slang)
	target_link_directories(RoseLib PUBLIC $ENV{VULKAN_SDK}/Lib)
endif()

# Platform options/defines

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
else()
	message(FATAL_ERROR "Unsupported platform")
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

# Builtin apps

if (ROSE_BUILD_APPS)
	add_executable(SceneRendererApp src/SceneRendererApp/SceneRendererApp.cpp)
	target_link_libraries(SceneRendererApp PRIVATE RoseLib)

	add_executable(WorkGraphApp src/WorkGraphEditorApp.cpp)
	target_link_libraries(WorkGraphApp PRIVATE RoseLib)
endif()

# Tests

if (ROSE_ENABLE_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

# Export header files

install(TARGETS RoseLib FILE_SET HEADERS)
