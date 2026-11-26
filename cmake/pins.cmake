# SPDX-License-Identifier: GPL-3.0-or-later
#
# Everything this project fetches from outside, with the version and the checksum it must have
# (TF-004; SPEC-002, SPEC-005, SPEC-071, SPEC-223, SPEC-224). Nothing else is downloaded by the build
# or the tests, apart from Debian's package index and a Debian container image in the package level.
#
# A line is: spz_pin(NAME URL SHA256). tools/testing/pins.py reads this same file, so that the build
# and the test tools cannot disagree. To move a pin forward, change its line and nothing else.

# The engine: libsidplayfp and the SID emulation it needs (SPEC-002).
spz_pin(libsidplayfp
    "https://github.com/libsidplayfp/libsidplayfp/releases/download/v3.1.2/libsidplayfp-3.1.2.tar.gz"
    "6d16c20e523ac4d3e385531dafe247a830b59bcb4eb73a557f15cf4e7ae6da84")
spz_pin(libresidfp
    "https://github.com/libsidplayfp/libresidfp/releases/download/v1.3.0/libresidfp-1.3.0.tar.gz"
    "6de8880dcfc11b993107f71b538262f08ae07fd0e595297ce776f641b5435b38")

# The reference program the tests compare the app's sound with (SPEC-071). Tests only.
spz_pin(sidplayfp
    "https://github.com/libsidplayfp/sidplayfp/releases/download/v3.2.3/sidplayfp-3.2.3.tar.gz"
    "083a21a80e5da57a0cef15451f326ea3dbb4c28e0b453daefad0814070a1a440")

# Sound output (SPEC-005). One header, public domain or MIT-0.
spz_pin(miniaudio
    "https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h"
    "ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89")

# The device's sysroot: M5Stack's BSP, release 0.0.4 (SPEC-001). 435 MB; only the package level needs it.
spz_pin(sysroot
    "https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/v0.0.4/sdk_bsp.tar.gz"
    "e51b6eb803ed08f450e459efbfe62dd0341440846f3be9d01da861fe6cfdebb0")

# The store's own checks, at one commit of CardputerZero/packages (SPEC-223, SPEC-224). Tests only.
spz_pin(install_path_policy
    "https://raw.githubusercontent.com/CardputerZero/packages/21ea24cfdb8d082487a4030d2551d4615d827552/.github/scripts/install_path_policy.py"
    "3cedaa05f85fe4a98c97f5d77221c53dd53d7772a775d2995fadb1f7c602b4c5")
spz_pin(store_meta_policy
    "https://raw.githubusercontent.com/CardputerZero/packages/21ea24cfdb8d082487a4030d2551d4615d827552/.github/scripts/store_meta_policy.py"
    "748c9a2c5cb204b5e6ad1d19a44f82d61acf3e66f034ae73fc2fbe1cfdb74505")

# LVGL 9.5.0 (SPEC-001) is fetched with git, because its release has no archive of its own; the pin is
# the commit its tag must point at.
set(SPZ_LVGL_REPOSITORY "https://github.com/lvgl/lvgl.git")
set(SPZ_LVGL_TAG "v9.5.0")
set(SPZ_LVGL_COMMIT "85aa60d18b3d5e5588d7b247abf90198f07c8a63")

# doctest 2.4.12 is kept in third_party/doctest/doctest.h; this is the checksum of that file.
set(SPZ_DOCTEST_SHA256 "2a31654ead2a6e5ef93086ca97659b701710c80275207b3bdb12676c012daceb")

# The typeface of the app's fonts: DejaVu Sans, from DejaVu's release 2.37 (SPEC-010). The fonts are made
# from it ahead of time by `cmake -P cmake/fonts.cmake`, and the sources made are kept in src/ui/fonts.
spz_pin(dejavu_fonts
    "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2"
    "fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7")
