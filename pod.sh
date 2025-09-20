#!/bin/bash
set -e

IMAGE_NAME=gnucash-dev
ACTION=${1:-run}

# ---- Build the image if missing ----
if ! podman image exists "$IMAGE_NAME"; then
  echo "Building container image $IMAGE_NAME..."
  podman build -t "$IMAGE_NAME" -f - <<'EOF'
FROM fedora:latest
RUN dnf -y install \
      gcc gcc-c++ make automake autoconf libtool cmake ninja-build git \
      guile30{,-devel} pkg-config gtk3{,-devel} webkit2gtk4.0{,-devel} \
      libdbi{,-devel} libdbi-dbd-sqlite boost-devel gtest-devel gmock-devel \
      libxml2-devel libxslt-devel swig gettext{,-devel} glib2-devel \
      python3{,-devel} valgrind perl-podlators \
    && dnf clean all
WORKDIR /app
EOF
fi

# ---- Ensure persistent build volume ----
podman volume inspect gnucash-build >/dev/null 2>&1 || podman volume create gnucash-build

# ---- Build, install, and run inside the container ----
podman run -it --rm \
  -v "$PWD:/app:Z" \
  -v gnucash-build:/app/build:Z \
  -e DISPLAY=$DISPLAY \
  -e DBUS_SESSION_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
  -e XDG_RUNTIME_DIR="/run/user/$UID" \
  -e NO_AT_BRIDGE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:Z \
  -v /run/user/$UID:/run/user/$UID \
  -v /etc/machine-id:/etc/machine-id:ro \
  "$IMAGE_NAME" bash -c "
    mkdir -p /app/build
    cd /app/build
    if [ '$ACTION' = 'rebuild' ]; then
        echo 'Cleaning build directory...'
        rm -rf ./*
    fi
    echo 'Configuring CMake with install prefix...'
    cmake -GNinja ..  -DCMAKE_BUILD_TYPE=Debug -DWITH_OFX=OFF \
          -DWITH_AQBANKING=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
    echo 'Building with Ninja...'
    ninja
    echo 'Installing to /usr/local...'
    ninja install > /dev/null
    case '$ACTION' in
      run)
        echo 'Running installed GnuCash...'
        /usr/local/bin/gnucash
        ;;
      valgrind)
        echo 'Running installed GnuCash under Valgrind...'
        valgrind /usr/local/bin/gnucash
        ;;
    esac
  "
