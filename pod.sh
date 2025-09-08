#!/bin/bash
set -e

IMAGE_NAME=gnucash-dev
WORKDIR=$PWD

# Build container if missing
if ! podman image exists $IMAGE_NAME; then
    echo "Building container $IMAGE_NAME..."
    podman build -t $IMAGE_NAME -f Containerfile
fi

# Create persistent volumes if missing
podman volume inspect gnucash-build >/dev/null 2>&1 || podman volume create gnucash-build
podman volume inspect gnucash-ccache >/dev/null 2>&1 || podman volume create gnucash-ccache

# Action argument: run (default), rebuild, valgrind
ACTION=${1:-run}

# Run container
podman run -it --rm \
  -v "$WORKDIR:/app:Z" \
  -v gnucash-build:/app/build:Z \
  -v gnucash-ccache:/ccache:Z \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:Z \
  $IMAGE_NAME bash -c "
    mkdir -p /app/build
    cd /app/build
    if [ '$ACTION' = 'rebuild' ]; then
        echo 'Cleaning build directory...'
        rm -rf ./*
    fi
    echo 'Configuring CMake...'
    cmake -GNinja .. -DCMAKE_BUILD_TYPE=Debug -DWITH_OFX=ON -DWITH_AQBANKING=OFF
    echo 'Building with Ninja...'
    ninja
    echo 'CCache stats:'
    ccache -s
    if [ '$ACTION' = 'run' ]; then
        echo 'Running GnuCash...'
        ./bin/gnucash
    elif [ '$ACTION' = 'valgrind' ]; then
        echo 'Running GnuCash under Valgrind...'
        valgrind ./bin/gnucash
    fi
  "
