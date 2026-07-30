cmake_minimum_required(VERSION 3.10)

project(LaserScannerLinux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

find_library(
	FLY_CAPTURE_LIBRARY
	NAMES flycapture
	PATHS
		/usr/lib
	NO_DEFAULT_PATH
)

find_path(
	FLY_CAPTURE_INCLUDE_DIR
	NAMES FlyCapture2.h
	PATHS
		/usr/include/flycapture
	NO_DEFAULT_PATH
)

if(NOT FLY_CAPTURE_INCLUDE_DIR)
	message(FATAL ERROR "FlyCapture2.h não encontrado")
endif()

if(NOT FLY_CAPTURE_LIBRARY)
	message(FATAL ERROR "libflycapture.so não encontrado")
endif()

add_executable(laser_scanner
	src/main.cpp
	src/FlyCaptureCamera.cpp
	src/ImagePathTrack.cpp
	src/VideoViewer.cpp
)

target_include_directories(laser_scanner PRIVATE
	"${FLY_CAPTURE_INCLUDE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(laser_scanner PRIVATE
	"${FLY_CAPTURE_LIBRARY}"
	${OpenCV_LIBS}
	Qt${QT_VERSION_MAJOR}::Widgets
)

message(STATUS "FlyCapture include: ${FLY_CAPTURE_INCLUDE_DIR}")
message(STATUS "FlyCapture library: ${FLY_CAPTURE_LIBRARY}")
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "Qt major version: ${QT_VERSION_MAJOR}")
