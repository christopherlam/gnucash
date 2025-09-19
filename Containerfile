FROM fedora:latest

# Essential build deps + GUI + ccache + valgrind
RUN dnf -y install \
        gcc gcc-c++ make automake autoconf libtool \
        cmake ninja-build git \
        guile30 guile30-devel \
        pkg-config \
        gtk3 gtk3-devel \
        webkit2gtk4.0 webkit2gtk4.0-devel \
        libdbi-dbd-sqlite libdbi-devel \
        boost-devel \
        gtest-devel \
        gmock-devel \
        libxml2-devel \
        libxslt-devel \
        swig \
        gettext gettext-devel \
        glib2-devel \
        python3 python3-devel \
        valgrind \
        perl-podlators \
    && dnf clean all

WORKDIR /app
CMD bash
