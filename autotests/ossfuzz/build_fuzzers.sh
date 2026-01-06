#!/bin/bash -eu
#
# SPDX-FileCopyrightText: 2019 Google Inc.
# SPDX-FileCopyrightText: 2025 Azhar Momin <azhar.momin@kdemail.net>
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2019 Google Inc.
# Copyright 2025 Azhar Momin <azhar.momin@kdemail.net>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

rm -rf $WORK/*

export PATH="$WORK/bin:$PATH"
export PKG_CONFIG_PATH="$WORK/lib/pkgconfig:$WORK/lib/x86_64-linux-gnu/pkgconfig"
if [[ $FUZZING_ENGINE == "afl" ]]; then
    export LDFLAGS="-fuse-ld=lld"
fi

# build zstd
cd $SRC/zstd
cmake -S build/cmake -G Ninja \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=$WORK
ninja install -j$(nproc)

# Build zlib
cd $SRC/zlib
./configure --static --prefix $WORK
make install -j$(nproc)

# Build bzip2
# Inspired from ../bzip2/build
cd $SRC
tar xzf bzip2-*.tar.gz && rm -f bzip2-*.tar.gz
cd bzip2-*
SRCL=(blocksort.o huffman.o crctable.o randtable.o compress.o decompress.o bzlib.o)

for source in ${SRCL[@]}; do
    name=$(basename $source .o)
    $CC $CFLAGS -c ${name}.c
done
rm -f libbz2.a
ar cq libbz2.a ${SRCL[@]}
cp -f bzlib.h $WORK/include
cp -f libbz2.a $WORK/lib

# Build xz
export ORIG_CFLAGS="${CFLAGS}"
export ORIG_CXXFLAGS="${CXXFLAGS}"
unset CFLAGS
unset CXXFLAGS
cd $SRC/xz
./autogen.sh --no-po4a --no-doxygen
./configure --enable-static --disable-debug --disable-shared --disable-xz --disable-xzdec --disable-lzmainfo --prefix $WORK
make install -j$(nproc)
export CFLAGS="${ORIG_CFLAGS}"
export CXXFLAGS="${ORIG_CXXFLAGS}"

# Build openssl
cd $SRC/openssl
CONFIG_FLAGS="no-shared no-tests --prefix=$WORK --openssldir=$WORK"
if [[ $CFLAGS = *sanitize=memory* ]]
then
    # Disable assembly for proper instrumentation
    CONFIG_FLAGS+=" no-asm"
fi
./config $CONFIG_FLAGS
make build_generated
make libcrypto.a -j$(nproc)
make install_sw

# Build extra-cmake-modules
cd $SRC/extra-cmake-modules
cmake . -G Ninja \
    -DCMAKE_INSTALL_PREFIX=$WORK
ninja install -j$(nproc)

# Build qtbase
cd $SRC/qtbase
./configure -no-glib -qt-libpng -qt-pcre -opensource -confirm-license -static -no-opengl \
    -no-icu -platform linux-clang-libc++ -debug -prefix $WORK -no-feature-gui -no-feature-sql \
    -no-feature-network  -no-feature-xml -no-feature-dbus -no-feature-printsupport
ninja install -j$(nproc)

# Build karchive
cd $SRC/karchive
rm -rf poqm
cmake . -G Ninja \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_FUZZERS=ON \
    -DCMAKE_INSTALL_PREFIX=$WORK
ninja install -j$(nproc)

EXTENSIONS="k7z_fuzzer 7z
    kar_fuzzer ar
    ktar_fuzzer tar
    kzip_fuzzer zip
    ktar_bz2_fuzzer tar.bz2
    ktar_gz_fuzzer tar.gz
    ktar_lz_fuzzer tar.lz
    ktar_xz_fuzzer tar.xz
    ktar_zst_fuzzer tar.zst"
echo "$EXTENSIONS" | while read fuzzer_name extension; do
    # Copy the fuzzer
    cp bin/fuzzers/$fuzzer_name $OUT

    # Create a seed corpus
    find . -name "*.$extension" -exec zip -jq "$OUT/${fuzzer_name}_seed_corpus.zip" {} +

    # Copy the dictionary file
    if [ -f "autotests/data/dict/$fuzzer_name.dict" ]; then
      cp "autotests/data/dict/$fuzzer_name.dict" $OUT
    fi
done
