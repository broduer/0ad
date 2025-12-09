# Debian 13
FROM debian:trixie-slim

# 0 A.D. dependencies.
# Unlike gcc/g++ clang only has a single package for both C/C++
# gcc/g++ versions: 12 13 14(default)
# clang versions: 17 18 19(default)
# lld for use with clang
# llvm for objdump (spidermonkey)
ARG DEBIAN_FRONTEND=noninteractive
ARG DEBCONF_NOWARNINGS="yes"
RUN apt-get -qqy update \
    && apt-get upgrade -qqy \
    && apt-get install -qqy --no-install-recommends \
        cargo \
        cbindgen \
        clang \
        clang-19 \
        cmake \
        curl \
        g++ \
        g++-14 \
        gcc \
        gcc-14 \
        libboost-filesystem-dev \
        libboost-system-dev \
        libcurl4-gnutls-dev \
        libenet-dev \
        libfmt-dev \
        libfreetype-dev \
        libgloox-dev \
        libicu-dev \
        libminiupnpc-dev \
        libogg-dev \
        libopenal-dev \
        libpng-dev \
        libsdl2-dev \
        libsdl3-dev \
        libsodium-dev \
        libvorbis-dev \
        libwxgtk3.2-dev \
        libxml2-dev \
        libzip-dev \
        lld-19 \
        llvm \
        llvm-19 \
        m4 \
        make \
        nlohmann-json3-dev \
        patch \
        python-is-python3 \
        python3-dev \
        python3-pip \
        rustc \
        subversion \
        xz-utils \
        zlib1g-dev \
    && apt-get clean

ENV SHELL=/bin/bash
