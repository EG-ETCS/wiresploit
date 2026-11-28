# Cross-compile toolchain: build the Pi 5 (aarch64, Debian Bookworm) binaries on
# an x86-64 Debian Bookworm machine, normally the dev container
# (node/docker/Dockerfile). node/scripts/build_pi.sh passes this file.
#
# Needs: crossbuild-essential-arm64, libzip-dev:arm64 (Debian multiarch), and
# piolib cross-built into PI5LA_ARM64_PREFIX. pioasm is a build-time tool, so it
# is the host's own pioasm, found on PATH or given with -DPIOASM_EXECUTABLE.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(PI5LA_CROSS_TRIPLET aarch64-linux-gnu)
set(CMAKE_C_COMPILER ${PI5LA_CROSS_TRIPLET}-gcc)
set(CMAKE_CXX_COMPILER ${PI5LA_CROSS_TRIPLET}-g++)
set(CMAKE_LIBRARY_ARCHITECTURE ${PI5LA_CROSS_TRIPLET})

# Where the arm64 piolib (and any other arm64-only library built from source)
# is installed. The dev container installs it into /opt/arm64-prefix.
if(DEFINED ENV{PI5LA_ARM64_PREFIX})
    set(PI5LA_ARM64_PREFIX "$ENV{PI5LA_ARM64_PREFIX}" CACHE PATH "arm64 install prefix")
else()
    set(PI5LA_ARM64_PREFIX "/opt/arm64-prefix" CACHE PATH "arm64 install prefix")
endif()
list(APPEND CMAKE_FIND_ROOT_PATH "${PI5LA_ARM64_PREFIX}")

# Programs (pioasm, python3) run on the build machine; libraries and headers
# come from the arm64 prefix and the multiarch directories.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# pkg-config must only see arm64 .pc files, or it would hand back the
# build machine's x86-64 libzip.
set(ENV{PKG_CONFIG_LIBDIR}
    "${PI5LA_ARM64_PREFIX}/lib/pkgconfig:/usr/lib/${PI5LA_CROSS_TRIPLET}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
