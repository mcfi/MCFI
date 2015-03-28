This file documents the steps of compiling MCFI's native counterpart toolchain
on Ubuntu 14.04. For convenience, just execute build_native_toolchain.sh in
the current directory and the native sdk will be downloaded, built and installed
in $HOME/native

0. Download prebuilt binaries of clang-3.5 from
http://llvm.org/releases/3.5.0/clang+llvm-3.5.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz,
decompress and install it in $LLVM_HOME (e.g., ~/llvm)

1. Download llvm-3.5, clang-3.5, libunwind-1.1 and musl-1.0.4 from their
websites.

2. Create a directory $NATIVE (e.g., ~/native) and decompress the downloaded
tools. clang-3.5 will be contained in a folder cfe-3.5. Rename it to clang and
mv it to $NATIVE/llvm-3.5/tools/

3. Apply native.patch in this directory to $NATIVE/

   cp native.patch $NATIVE/

   git apply native.patch

4. Build llvm-3.5. You may set the toolchain's installation prefix to
$NATIVE_SDK (e.g., $NATIVE/toolchain) for convenience. The following build
should also use the same prefix.

   mkdir $NATIVE/llvm-3.5.0.src/release && cd $NATIVE/llvm-3.5.0.src/release

   ../configure --enable-targets=x86_64 --disable-debug-runtime --disable-debug-symbols --disable-jit --enable-optimized --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK

   make -j8

   make install

5. Build musl-1.0.4.

   ./configure --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK CC=clang CFLAGS=-O3 --disable-static

   make install -j8

6. Build libunwind-1.1.

   autoreconf # this will fail

   automake --add-missing

   autoreconf # this will succeed

   mkdir $NATIVE/libunwind-1.1/build && cd $NATIVE/libunwind-1.1/build

   ../configure --enable-cxx-exceptions --disable-static --prefix=$NATIVE_SDK --exec-prefix=$NATIVE_SDK CC=$NATIVE_SDK/bin/clang CFLAGS=-O3

   make install

7. Build libcxx-3.5 against libstdc++

   mkdir $NATIVE/libcxx-3.5.0.src/build_std && cd $NATIVE/libcxx-3.5.0.src/build_std

   CC=$LLVM_HOME/bin/clang CXX=$LLVM_HOME/bin/clang++ cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libstdc++ -DLIBCXX_LIBSUPCXX_INCLUDE_PATHS="/usr/include/c++/4.8/;/usr/include/x86_64-linux-gnu/c++/4.8/" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$NATIVE_SDK

   make install

8. Build libcxxabi-3.5 against the already-built libcxx-3.5

   cd $NATIVE/libcxxabi-3.5.0.src/lib

   ./buildit

9. Build libcxx-3.5 against the already-built libcxxabi-3.5

   mkdir $NATIVE/libcxx-3.5.0.src/build_libcxxabi && cd $NATIVE/libcxx-3.5.0.src/build_libcxxabi

   # trick cmake to pass the Makefile generation
   mv $NATIVE_SDK/bin/clang $NATIVE_SDK/bin/clang.new

   ln -s $LLVM_HOME/bin/clang $NATIVE_SDK/bin/clang

   CC="$NATIVE_SDK/bin/clang" CXX="$NATIVE_SDK/bin/clang++ -D__MUSL__ -U__GLIBC__ -O3" cmake -G "Unix Makefiles" -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_LIBCXXABI_INCLUDE_PATHS="$NATIVE_SDK/include" -DCMAKE_BUILD_TYPE=Release ../ -DCMAKE_INSTALL_PREFIX=$NATIVE_SDK

   # change clang back
   mv $NATIVE_SDK/bin/clang.new $NATIVE_SDK/bin/clang

   make install

10. The native toolchain build is done, and you may use it to build other
programs, such as SPECCPU2006 and Google V8 JavaScript engine. Before building
SPECCPU2006, you need to apply patches in spec2006. Also note that compiling
400.perlbench needs -std=gnu89. To build v8, first you need to download v8
using "git clone https://chromium.googlesource.com/v8/v8", and then do "git
checkout 3.29.88.19", since only this branch was modified to RockJIT. Then you
may execute build_v8.sh in this directory to build v8.
