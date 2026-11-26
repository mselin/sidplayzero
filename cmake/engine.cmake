# SPDX-License-Identifier: GPL-3.0-or-later
#
# The engine (SPEC-002, SPEC-003): libresidfp 1.3.0 and libsidplayfp 3.1.2, built from their release
# archives as static libraries, with the compiler, the flags and the sanitizers of this build, and linked
# into the program. Both archives are checked against their pins when this build is configured, so a wrong
# checksum stops the build before anything is compiled (cmake/fetch.cmake).
#
# libsidplayfp is configured --without-exsid, and --without-usbsid unless the part `usbsid` is built
# (SPEC-191). Its song length reader is in the library but never called (SPEC-008).
#
# What this gives:
#   spz::engine_libraries   an interface target: the engine's headers and its two static libraries
#   spz_engine              the real Engine of SPEC-021 (src/platform/real_engine.cpp), over them
include(ExternalProject)

spz_fetch(libresidfp _spz_residfp_archive)
spz_fetch(libsidplayfp _spz_sidplayfp_archive)

set(SPZ_ENGINE_PREFIX "${CMAKE_BINARY_DIR}/engine")
file(MAKE_DIRECTORY "${SPZ_ENGINE_PREFIX}/include")

# The flags the engine is compiled with: those of this build's type, and the options every source of this
# build gets (the sanitizers and -ffile-prefix-map): the top CMakeLists.txt's SPZ_COMPILE_OPTIONS and
# SPZ_LINK_OPTIONS, the lists it gives add_compile_options() and add_link_options().
string(TOUPPER "${CMAKE_BUILD_TYPE}" _spz_type)
list(JOIN SPZ_COMPILE_OPTIONS " " _spz_options_text)
list(JOIN SPZ_LINK_OPTIONS " " _spz_link_text)
set(_spz_cflags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${_spz_type}} ${_spz_options_text}")
set(_spz_cxxflags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_spz_type}} ${_spz_options_text}")
set(_spz_ldflags "${CMAKE_EXE_LINKER_FLAGS} ${_spz_link_text}")
# The engine's own sources lie in this build's folder: they are named from there, not by the full path.
string(APPEND _spz_cflags " -ffile-prefix-map=${SPZ_ENGINE_PREFIX}=engine")
string(APPEND _spz_cxxflags " -ffile-prefix-map=${SPZ_ENGINE_PREFIX}=engine")
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    # A debug build of ours still wants an engine that keeps up, also under the sanitizers.
    string(APPEND _spz_cflags " -O2")
    string(APPEND _spz_cxxflags " -O2")
endif()

set(_spz_cc "${CMAKE_C_COMPILER}")
set(_spz_cxx "${CMAKE_CXX_COMPILER}")
set(_spz_host_arguments "")
if(CMAKE_CROSSCOMPILING)
    set(_spz_host_arguments "--host=${CMAKE_LIBRARY_ARCHITECTURE}")
    if(NOT CMAKE_LIBRARY_ARCHITECTURE)
        set(_spz_host_arguments "--host=${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")
    endif()
endif()
if(CMAKE_SYSROOT)
    string(APPEND _spz_cflags " --sysroot=${CMAKE_SYSROOT}")
    string(APPEND _spz_cxxflags " --sysroot=${CMAKE_SYSROOT}")
    string(APPEND _spz_ldflags " --sysroot=${CMAKE_SYSROOT}")
endif()

set(_spz_usbsid "--without-usbsid")
if("usbsid" IN_LIST SPZ_PARTS)
    set(_spz_usbsid "--with-usbsid")
endif()

# The engine's make runs inside this build's own (Ninja's), which keeps the other cores busy meanwhile: it
# starts a job only while the machine's load is under the number of cores, so that the two together do not
# run several times as many jobs as there are cores.
cmake_host_system_information(RESULT _spz_cores QUERY NUMBER_OF_LOGICAL_CORES)
find_program(SPZ_MAKE NAMES make gmake REQUIRED)

set(_spz_environment
    "CC=${_spz_cc}" "CXX=${_spz_cxx}"
    "CFLAGS=${_spz_cflags}" "CXXFLAGS=${_spz_cxxflags}" "LDFLAGS=${_spz_ldflags}"
    "PKG_CONFIG_PATH=${SPZ_ENGINE_PREFIX}/lib/pkgconfig" "PKG_CONFIG_LIBDIR=${SPZ_ENGINE_PREFIX}/lib/pkgconfig"
    "PKG_CONFIG_SYSROOT_DIR=")

ExternalProject_Add(spz_libresidfp
    URL "${_spz_residfp_archive}"
    URL_HASH "SHA256=${SPZ_PIN_libresidfp_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    PREFIX "${SPZ_ENGINE_PREFIX}/work/residfp"
    INSTALL_DIR "${SPZ_ENGINE_PREFIX}"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env ${_spz_environment}
        <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --quiet --enable-static --disable-shared
        ${_spz_host_arguments}
    BUILD_COMMAND ${SPZ_MAKE} -j${_spz_cores} -l${_spz_cores}
    INSTALL_COMMAND ${SPZ_MAKE} install
    BUILD_BYPRODUCTS "${SPZ_ENGINE_PREFIX}/lib/libresidfp.a"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON)

ExternalProject_Add(spz_libsidplayfp
    URL "${_spz_sidplayfp_archive}"
    URL_HASH "SHA256=${SPZ_PIN_libsidplayfp_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    DEPENDS spz_libresidfp
    PREFIX "${SPZ_ENGINE_PREFIX}/work/sidplayfp"
    INSTALL_DIR "${SPZ_ENGINE_PREFIX}"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env ${_spz_environment}
        <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --quiet --enable-static --disable-shared
        --without-exsid ${_spz_usbsid} ${_spz_host_arguments}
    BUILD_COMMAND ${SPZ_MAKE} -j${_spz_cores} -l${_spz_cores}
    INSTALL_COMMAND ${SPZ_MAKE} install
    BUILD_BYPRODUCTS "${SPZ_ENGINE_PREFIX}/lib/libsidplayfp.a"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON)

add_library(spz_engine_libraries INTERFACE)
add_dependencies(spz_engine_libraries spz_libsidplayfp)
target_include_directories(spz_engine_libraries SYSTEM INTERFACE "${SPZ_ENGINE_PREFIX}/include")
target_link_libraries(spz_engine_libraries INTERFACE
    "${SPZ_ENGINE_PREFIX}/lib/libsidplayfp.a" "${SPZ_ENGINE_PREFIX}/lib/libresidfp.a")
if("usbsid" IN_LIST SPZ_PARTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SPZ_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    find_package(Threads REQUIRED)
    target_link_libraries(spz_engine_libraries INTERFACE PkgConfig::SPZ_LIBUSB Threads::Threads)
endif()
add_library(spz::engine_libraries ALIAS spz_engine_libraries)

# The real Engine: the one place that names the library (SPEC-020).
add_library(spz_engine STATIC "${CMAKE_SOURCE_DIR}/src/platform/real_engine.cpp")
target_include_directories(spz_engine PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(spz_engine PUBLIC spz_core PRIVATE spz::engine_libraries)
add_dependencies(spz_engine spz_libsidplayfp)
target_compile_options(spz_engine PRIVATE ${SPZ_WARNINGS})
if(SIDPLAYZERO_SANITIZE STREQUAL "fuzzer")
    target_compile_definitions(spz_engine PRIVATE "SPZ_ENGINE_SANITIZER=\"address\"")
else()
    target_compile_definitions(spz_engine PRIVATE "SPZ_ENGINE_SANITIZER=\"${SIDPLAYZERO_SANITIZE}\"")
endif()
