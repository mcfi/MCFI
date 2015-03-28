#!/bin/bash

if [ -z "$NATIVE" ]
then
    export NATIVE=$HOME/native
    mkdir -p $NATIVE
fi

if [ -z "$NATIVE_SDK" ]
then
    export NATIVE_SDK=$NATIVE/toolchain
    mkdir -p $NATIVE_SDK
fi

if [ -z "$LLVM_HOME" ]
then
    LLVM_HOME=$HOME/llvm
fi

cp native.patch $NATIVE/native.patch

cd $NATIVE

# get clang-3.5
wget http://llvm.org/releases/3.5.0/cfe-3.5.0.src.tar.xz

# get llvm-3.5
wget http://llvm.org/releases/3.5.0/llvm-3.5.0.src.tar.xz

# get libcxx-3.5
wget http://llvm.org/releases/3.5.0/libcxx-3.5.0.src.tar.xz

# get libcxxabi-3.5
wget http://llvm.org/releases/3.5.0/libcxxabi-3.5.0.src.tar.xz

# get libunwind-1.1
wget http://download.savannah.gnu.org/releases/libunwind/libunwind-1.1.tar.gz

# get musl-1.0.4
wget http://www.musl-libc.org/releases/musl-1.0.4.tar.gz

# decompress
tar xf cfe-3.5.0.src.tar.xz
tar xf llvm-3.5.0.src.tar.xz
mv cfe-3.5.0.src llvm-3.5.0.src/tools/clang
tar xf libcxx-3.5.0.src.tar.xz
tar xf libcxxabi-3.5.0.src.tar.xz
tar xf libunwind-1.1.tar.gz
tar xf musl-1.0.4.tar.gz

# apply the patches
git apply native.patch

# build llvm

mkdir -p $NATIVE/llvm-3.5.0.src/release && cd $NATIVE/llvm-3.5.0.src/release

../configure --enable-targets=x86_64 --disable-debug-runtime --disable-debug-symbols --disable-jit --enable-optimized --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK

make -j8

make install

cd $NATIVE

# build musl

cd $NATIVE/musl-1.0.4

./configure --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK CC=clang CFLAGS=-O3 --disable-static

make install -j8

cd $NATIVE

# Build libunwind-1.1
mkdir -p $NATIVE/libunwind-1.1/build && cd $NATIVE/libunwind-1.1/

autoreconf # this will fail

automake --add-missing

autoreconf # this will succeed

cd build

../configure --enable-cxx-exceptions --disable-static --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK CC=$NATIVE_SDK/bin/clang CFLAGS=-O3

make install

cd $NATIVE

# Build libcxx-3.5 against libstdc++
mkdir -p $NATIVE/libcxx-3.5.0.src/build_std && cd $NATIVE/libcxx-3.5.0.src/build_std

CC=$LLVM_HOME/bin/clang CXX=$LLVM_HOME/bin/clang++ cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libstdc++ -DLIBCXX_LIBSUPCXX_INCLUDE_PATHS="/usr/include/c++/4.8/;/usr/include/x86_64-linux-gnu/c++/4.8/" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$NATIVE_SDK

make install -j8

cd $NATIVE

#Build libcxxabi-3.5 against the already-built libcxx-3.5

cd $NATIVE/libcxxabi-3.5.0.src/lib

./buildit

cd $NATIVE

# Build libcxx-3.5 against the already-built libcxxabi-3.5
mkdir -p $NATIVE/libcxx-3.5.0.src/build_libcxxabi && cd $NATIVE/libcxx-3.5.0.src/build_libcxxabi

# trick cmake to pass the Makefile generation
mv $NATIVE_SDK/bin/clang $NATIVE_SDK/bin/clang.new

ln -s $LLVM_HOME/bin/clang $NATIVE_SDK/bin/clang

CC=$NATIVE_SDK/bin/clang CXX="$NATIVE_SDK/bin/clang++ -D__MUSL__ -U__GLIBC__ -O3" cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_LIBCXXABI_INCLUDE_PATHS="$NATIVE_SDK/include" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$NATIVE_SDK

# change clang back
mv $NATIVE_SDK/bin/clang.new $NATIVE_SDK/bin/clang

make install -j8
