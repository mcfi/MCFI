MCFI
====

This directory contains the MCFI toolchain that has been tested on Ubuntu 14.04. To build the toolchain, you first need to execute the following command to install the required tools.

sudo apt-get install build-essential g++-multilib flex bison libtool subversion git cmake autoconf automake

Also, you need a prebuilt clang 3.5.0 compiler that can be downloaded from http://llvm.org/releases/3.5.0/clang+llvm-3.5.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz . After you download the prebuilt compiler, decompress it. Then set and export LLVM_HOME environment variable to be the directory name of the decompressed clang compiler.

1. Run build.sh to build the MCFI toolchain. By default, the toolchain will be installed in $HOME/MCFI/toolchain if you do not set MCFI_SDK to some other places.

2. cd native && ./build_native_toolchain.sh will build the native toolchain so that you can compare the performance of MCFI-hardened programs with native counterparts.

3. Now you may use MCFI toolchain or the native toolchain just as you use gcc.

By default, clang would generate PICFI-enforced binaries, but you may pass the following command line options to clang to experience enhanced functionalities:

-Xclang -mdisable-tail-callinsts: turn tail call optimization off at the machine code level. This is the single most important CFG precision improvement method AFAIK.

-Xclang -mdisable-picfi: disable PICFI but enable MCFI.

-Xclang -mcount-iib: instrument each MCFI-instrumented indirect branch further so that the amount of its dynamic execution can be counted.
