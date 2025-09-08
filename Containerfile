FROM docker.io/library/archlinux:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN pacman -Syu --quiet --noconfirm glibc gcc cmake make boost python3 pkg-config gettext gtk3 guile git ninja gtest gmock sqlite3 webkit2gtk swig gwenhywfar aqbanking intltool libxslt libofx postgresql-libs libmariadbclient libdbi libdbi-drivers wayland-protocols ccache valgrind perl && pacman -Scc --noconfirm

ENV CCACHE_DIR=/ccache
ENV PATH="/usr/lib/ccache/bin:${PATH}"

WORKDIR /app
CMD ["bash"]
