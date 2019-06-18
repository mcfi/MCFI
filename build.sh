#!/bin/bash

if [ -z "$MCFI_SDK" ]
then
    export MCFI_SDK=$HOME/MCFI/toolchain
    mkdir -p $MCFI_SDK
fi

if [ -z "$LLVM_HOME" ]
then
    LLVM_HOME=$HOME/llvm
fi

MCFI=$PWD

# Build runtime
cd $MCFI/runtime && make

# Build the compiler
mkdir -p $MCFI/compiler/llvm-3.5.0.src/release && cd $MCFI/compiler/llvm-3.5.0.src/release

../configure --enable-targets=x86_64 --disable-debug-runtime --disable-debug-symbols --disable-jit --enable-optimized --prefix=$MCFI_SDK --exec-prefix=$MCFI_SDK

make -j8

make install

# Build the linker
mkdir -p $MCFI/compiler/binutils-2.24/build && cd $MCFI/compiler/binutils-2.24/build

../configure --prefix=$MCFI_SDK --exec-prefix=$MCFI_SDK CFLAGS="-Wno-error -g -O2"

make -j8

make install

# Build crt
cd $MCFI/lib/cxxstart && ./build.sh

# Build musl

cd $MCFI/lib/musl-1.0.4

./configure --prefix=$MCFI_SDK --exec-prefix=$MCFI_SDK CC=$MCFI_SDK/bin/clang CFLAGS=-O3 --disable-static

make install -j8

# Build libunwind-1.1
mkdir -p $MCFI/lib/libunwind-1.1/build && cd $MCFI/lib/libunwind-1.1

autoreconf

automake --add-missing

autoreconf

cd build

../configure --enable-cxx-exceptions --disable-static --prefix=$MCFI_SDK --exec-prefix=$MCFI_SDK CC=$MCFI_SDK/bin/clang CFLAGS=-O3

make install

# Build libcxx-3.5 against libstdc++
mkdir -p $MCFI/lib/libcxx-3.5.0.src/build_std && cd $MCFI/lib/libcxx-3.5.0.src/build_std

CC=$LLVM_HOME/bin/clang CXX=$LLVM_HOME/bin/clang++ cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libstdc++ -DLIBCXX_LIBSUPCXX_INCLUDE_PATHS="/usr/include/c++/4.8/;/usr/include/x86_64-linux-gnu/c++/4.8/" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$MCFI_SDK

make install -j8

#Build libcxxabi-3.5 against the already-built libcxx-3.5

cd $MCFI/lib/libcxxabi-3.5.0.src/lib

./buildit

# Build libcxx-3.5 against the already-built libcxxabi-3.5
mkdir -p $MCFI/lib/libcxx-3.5.0.src/build_libcxxabi && cd $MCFI/lib/libcxx-3.5.0.src/build_libcxxabi

# trick cmake to pass the Makefile generation
mv $MCFI_SDK/bin/clang $MCFI_SDK/bin/clang.new

ln -s $LLVM_HOME/bin/clang $MCFI_SDK/bin/clang

CC=$MCFI_SDK/bin/clang CXX="$MCFI_SDK/bin/clang++ -D__MUSL__ -U__GLIBC__ -O3" cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_LIBCXXABI_INCLUDE_PATHS="$MCFI_SDK/include" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$MCFI_SDK

# change clang back
mv $MCFI_SDK/bin/clang.new $MCFI_SDK/bin/clang

make install -j8
