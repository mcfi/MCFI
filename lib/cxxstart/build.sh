#!/bin/bash
gcc -DSHARED crtbegin.S -c -o crtbegin.o
gcc -DSHARED crtend.S -c -o crtend.o

gcc crtbeginS.S -c -o crtbeginS.o
gcc crtendS.S -c -o crtendS.o
gcc crtbeginT.S -c -o crtbeginT.o
gcc crtendT.S -c -o crtendT.o

if [ -z "$MCFI_SDK" ]
then
    MCFI_SDK=$HOME/MCFI/toolchain
fi

cp *.o $MCFI_SDK/lib/
