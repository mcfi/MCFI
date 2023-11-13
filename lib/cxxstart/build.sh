#!/bin/bash

if [ -z "$MCFI_SDK" ]
then
    MCFI_SDK=$HOME/MCFI/toolchain
fi

$MCFI_SDK/bin/clang crtbegin.c -O3 -c -o crtbegin.o
$MCFI_SDK/bin/clang crtbegin.c -O3 -c -o crtbeginT.o
$MCFI_SDK/bin/clang -fPIC -DSHARED crtbegin.c -O3 -c -o crtbeginS.o

# empty crtend.S, since everything has been included in crtbegin.c
touch crtend.S
echo "       .section        .note.GNU-stack,\"\",@progbits" > crtend.S
as crtend.S -o crtend.o
as crtend.S -o crtendT.o
as crtend.S -o crtendS.o

cp *.o $MCFI_SDK/lib/
