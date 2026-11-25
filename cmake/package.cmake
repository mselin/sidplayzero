# SPDX-License-Identifier: GPL-3.0-or-later
#
# The .deb for the device (SPEC-220 to SPEC-222): what it installs, what it depends on, and its name.
# The maintainer comes from the environment when the package is made, and has no default here (SPEC-221).
if(NOT DEFINED ENV{SIDPLAYZERO_MAINTAINER} OR "$ENV{SIDPLAYZERO_MAINTAINER}" STREQUAL "")
    message(FATAL_ERROR
        "The package needs a maintainer. Set the environment variable SIDPLAYZERO_MAINTAINER, "
        "for example to \"Your Name <you@your.domain>\", and run this again.")
endif()

set(_spz_packaging "${CMAKE_SOURCE_DIR}/packaging")

# SPEC-220: these files and nothing else. The program itself is installed by src/CMakeLists.txt.
install(FILES "${_spz_packaging}/sidplayzero.desktop"
    DESTINATION "share/APPLaunch/applications")
install(FILES
        "${_spz_packaging}/icons/sidplayzero.png"
        "${_spz_packaging}/icons/sidplayzero_80.png"
        "${_spz_packaging}/icons/sidplayzero_100.png"
    DESTINATION "share/APPLaunch/share/images")
install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${_spz_packaging}/THIRD-PARTY-NOTICES.md"
        "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION "share/doc/sidplayzero")

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_NAME "sidplayzero")
set(CPACK_PACKAGE_VERSION "${SPZ_VERSION}")
set(CPACK_PACKAGE_CONTACT "$ENV{SIDPLAYZERO_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A SID music player for the CardputerZero")
set(CPACK_PACKAGE_DESCRIPTION
    "SidPlayZero browses a collection of Commodore 64 SID music and plays it with the sidplayfp engine, from the CardputerZero's own keyboard. It contains no music.")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "$ENV{SIDPLAYZERO_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")                # the packaging's revision: 1.0.0-1 (SPEC-220)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")            # sidplayzero_<version>-1_arm64.deb
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mselin/sidplayzero")
# SPEC-221. With the part `usbsid` built, libusb-1.0-0 joins the list.
set(_spz_depends "libc6, libstdc++6, libgcc-s1, libcurl4t64, ca-certificates, libpulse0, pulseaudio-utils")
if("usbsid" IN_LIST SPZ_PARTS)
    string(APPEND _spz_depends ", libusb-1.0-0")
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${_spz_depends}")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "7zip")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
# Folders and files belong to root with the usual modes, whoever builds the package.
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

include(CPack)
