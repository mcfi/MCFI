MCFI/PICFI
====

This directory contains the MCFI/PICFI toolchain that has been tested on x64 Ubuntu 14.04. Technical details of MCFI/PICFI can be found in ```pcfi.pdf```. MCFI enforces fine-grained CFI and PICFI further improves MCFI by enforcing per-input CFGs. To build the toolchain, you first need to execute the following command to install the required tools.

  ```sudo apt-get install build-essential g++-multilib flex bison libtool subversion git cmake autoconf automake texinfo texi2html libtinfo5 gyp```

Also, you need a prebuilt clang 3.5.0 compiler that can be downloaded from http://llvm.org/releases/3.5.0/clang+llvm-3.5.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz . After you download the prebuilt compiler, decompress it. Then **set** and **export** LLVM_HOME environment variable to be the directory name of the decompressed clang compiler. For example, you could add the following two lines in the .bashrc file after you move the decompressed prebuilt compiler into, say, $HOME/llvm. Remember to **restart** bash to pick up the changes in .bashrc file.

```export LLVM_HOME=$HOME/llvm```

```export PATH=.:$LLVM_HOME/bin:$PATH```

Next, execute the following steps to build the toolchain:

1. Run ```build.sh``` to build the MCFI toolchain. By default, the toolchain will be installed in ```$HOME/MCFI/toolchain``` if you do not set MCFI_SDK to some other places.

2. ```cd native && ./build_native_toolchain.sh``` will build the native toolchain so that you can compare the performance of MCFI-hardened programs with native counterparts.

3. Now you may use MCFI toolchain or the native toolchain just as you use gcc.

By default, clang would generate PICFI-enforced binaries, but you may pass the following command line options to clang to experience enhanced functionalities:

```-Xclang -mdisable-tail-callinsts```: turn tail call optimization off at only the machine code level, but not the IR level. This is the single most important CFG precision improvement method AFAIK. The performance does not change much if you pass this option, but the CFG precision would be significantly increased. **If you are interested in analyzing the security of MCFI/PICFI, you definitely should try this option**.

```-Xclang -mdisable-picfi```: disable PICFI but enable MCFI. You also need to type ```make MCFI=1``` to build the runtime.

```-Xclang -mcount-iib```: instrument each MCFI-instrumented indirect branch further so that the amount of its dynamic execution can be counted.

PICFI Code Sharing
==

The current implementation of PICFI relies on online code patching for minimizing performance overhead, so code cannot be shared between processes, which enlarge physical memory consumption if deployed on real systems. To enable code sharing, the following x64 instrumentation can be used instead of code patching. Take a return address after a direct call to function ```foo``` as an example,

```
# for each indirect branch target (IBT), we use a bit in a bitmap to
# indicate if the IBT has been activated or not. The bitmap could be
# allocated in the .bss section for a module, so the relative address
# of the bit is statically known. For example, it may be the 3rd bit
# in a byte at address imm(%rip).

    test $0x4, immi(%rip)                # test if the bit is set
    jz   activate_ra_foo_1               # if the bit is not set, jump to a small
                                         # stub that activates return address 1
    call foo                             # original call instruction
ra_foo_1:                                # return address of foo
    ...
    
activate_ra_foo_1:
    1. marshal the args to the PICFI runtime
    2. call the runtime to update the CFFG
    3. set the bit to 1.
    4. transfer the control flow to call foo
    
```
The above approach may enlarge slightly code size and slow down the program, but enables code sharing.

Ported Applications
==
All SPECCPU2006 C/C++ benchmarks have been tested with both the test and reference data sets. However, you need to apply the patches in the ```spec2006``` directory to make the benchmarks compatible with MCFI/PICFI.

v8: ```https://github.com/mcfi/v8```

nginx-1.4.0: ```https://github.com/mcfi/nginx-1.4.0```

Independent Evaluation
==
This tool chain has been independently verified by researchers from Purdue University, SBA Research and UC Irvine. Please see the results in https://nebelwelt.net/publications/files/17CSUR.pdf.

Usage in Google CTF 2017
==
This tool chain was used in Google CTF 2017! See the CTF challenge https://github.com/google/google-ctf/tree/master/2017/quals/2017-pwn-cfi and a reference solution writeup https://drive.google.com/drive/folders/0BwMPuUHZOj0ndG5BMmQ5b0xxNmc.
