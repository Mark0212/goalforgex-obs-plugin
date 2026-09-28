cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# The GoalForgeX dock is a Qt widget registered through the OBS frontend API,
# so both are required (the template ships them OFF by default).
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "GoalForgeX for OBS requires ENABLE_FRONTEND_API and ENABLE_QT")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

# HTTPS client. OBS Studio itself links libcurl (frontend/CMakeLists.txt:
# find_package(CURL REQUIRED)), so the DLL is already present in every OBS
# install; the build finds it in the same obs-deps prebuilt package.
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

if(WIN32)
  # DPAPI (CryptProtectData) — encrypts the stored refresh token per Windows user.
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/gfx-http.cpp
    src/gfx-http.hpp
    src/gfx-auth.cpp
    src/gfx-auth.hpp
    src/gfx-sources.cpp
    src/gfx-sources.hpp
    src/gfx-dock.cpp
    src/gfx-dock.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
