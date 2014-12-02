#!/bin/bash
gcc -DSHARED crtbegin.S -c -o crtbegin.o
gcc -DSHARED crtend.S -c -o crtend.o

if [ -z "$MCFI_SDK" ]
then
    MCFI_SDK=$HOME/MCFI/toolchain
fi

cp *.o $MCFI_SDK/lib/
