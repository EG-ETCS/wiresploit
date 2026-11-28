# Wiresploit Node dev container.
#
# Builds and tests the Node on any x86-64 laptop, cross-compiles the Pi 5
# binaries, and builds the docs. Debian Bookworm matches Raspberry Pi OS
# Bookworm, so the arm64 libraries it links against are the ones on the Pi.
#
# Build (from the repo root, which is the build context):
#   docker build -f node/docker/Dockerfile -t wiresploit-node-dev .
#
# Use from a terminal (repo mounted at /work, files owned by you):
#   docker run --rm -it --user "$(id -u):$(id -g)" -v "$PWD:/work" \
#       wiresploit-node-dev node/scripts/test.sh all
#
# VS Code: "Reopen in Container" uses .devcontainer/node/devcontainer.json.
FROM debian:bookworm

# Pinned upstream sources, so every build is the same.
# pico-sdk 2.2.0: only tools/pioasm is used (a build-time tool, runs natively).
ARG PICO_SDK_COMMIT=a1438dff1d38bd9c65dbd693f0e5db4b9ae91779
ARG PIOASM_VERSION_STRING=2.2.0
# raspberrypi/utils: only piolib is used (cross-built for the Pi, static).
ARG RPI_UTILS_COMMIT=cf42db188fda72ecf2bd46b9acd5c114b6cc6fb9
# The non-root user. VS Code rewrites the uid to match yours
# (updateRemoteUserUID); terminal users pass --user instead.
ARG USER_NAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PYTHONDONTWRITEBYTECODE=1

# Keep this package list in sync with node/scripts/setup_host.sh.
RUN dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        crossbuild-essential-arm64 \
        cmake \
        pkg-config \
        libzip-dev \
        libzip-dev:arm64 \
        python3 \
        python3-venv \
        sigrok-cli \
        doxygen \
        shellcheck \
        whiptail \
        git \
        rsync \
        openssh-client \
        file \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# The toolchain file is shared with node/scripts/build_pi.sh.
COPY node/capture/cmake/aarch64-linux-gnu.cmake /opt/cmake/aarch64-linux-gnu.cmake

# pioasm, built natively from a sparse, shallow pico-sdk checkout.
RUN git init -q /tmp/pico-sdk \
    && cd /tmp/pico-sdk \
    && git remote add origin https://github.com/raspberrypi/pico-sdk \
    && git sparse-checkout set tools/pioasm \
    && git fetch -q --depth 1 --filter=blob:none origin "${PICO_SDK_COMMIT}" \
    && git checkout -q FETCH_HEAD \
    && cmake -S tools/pioasm -B build -DCMAKE_BUILD_TYPE=Release \
        -DPIOASM_VERSION_STRING="${PIOASM_VERSION_STRING}" \
    && cmake --build build -j"$(nproc)" \
    && install -m 0755 build/pioasm /usr/local/bin/pioasm \
    && cd / && rm -rf /tmp/pico-sdk

# piolib, cross-built for aarch64 as a static library into /opt/arm64-prefix,
# so the Pi only needs libzip4 at runtime. Only the library target is built:
# some piolib examples need pinctrl's gpiolib.h, which this checkout leaves out.
RUN git init -q /tmp/rpi-utils \
    && cd /tmp/rpi-utils \
    && git remote add origin https://github.com/raspberrypi/utils \
    && git sparse-checkout set piolib \
    && git fetch -q --depth 1 --filter=blob:none origin "${RPI_UTILS_COMMIT}" \
    && git checkout -q FETCH_HEAD \
    && cmake -S piolib -B build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/cmake/aarch64-linux-gnu.cmake \
        -DCMAKE_INSTALL_PREFIX=/opt/arm64-prefix \
        -DBUILD_SHARED_LIBS=OFF \
    && cmake --build build --target pio -j"$(nproc)" \
    && cmake --install build --component piolib \
    && file /opt/arm64-prefix/lib/libpio.a | grep -q 'ar archive' \
    && cd / && rm -rf /tmp/rpi-utils \
    && printf 'pico-sdk %s (pioasm %s)\nraspberrypi/utils %s (piolib)\n' \
        "${PICO_SDK_COMMIT}" "${PIOASM_VERSION_STRING}" "${RPI_UTILS_COMMIT}" \
        > /opt/toolchain-versions.txt

# Python tools: docs, tests, formatting, and the web_ui's Flask.
COPY requirements-docs.txt /opt/requirements/requirements-docs.txt
COPY node/docker/requirements-dev.txt /opt/requirements/requirements-dev.txt
COPY node/capture/tools/web_ui/requirements.txt /opt/requirements/requirements-web-ui.txt
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir \
        -r /opt/requirements/requirements-docs.txt \
        -r /opt/requirements/requirements-dev.txt \
        -r /opt/requirements/requirements-web-ui.txt
ENV PATH=/opt/venv/bin:$PATH \
    PI5LA_ARM64_PREFIX=/opt/arm64-prefix

RUN groupadd --gid "${USER_GID}" "${USER_NAME}" \
    && useradd --create-home --uid "${USER_UID}" --gid "${USER_GID}" \
        --shell /bin/bash "${USER_NAME}" \
    && git config --system --add safe.directory /work

WORKDIR /work
USER ${USER_NAME}
CMD ["bash"]
