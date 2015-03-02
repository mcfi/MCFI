#!/bin/bash

# This command documents how we made the patches for the SPECCPU2006
# C/C++ benchmarks that can be compiled and hardened with MCFI.

NEW=~/cpu2006/benchspec/CPU2006
ORIG=~/vanilla_cpu2006/benchspec/CPU2006

NEWSRC=src.patched
SRC=src

for bmk in 400.perlbench 401.bzip2 403.gcc 429.mcf 433.milc 445.gobmk \
    456.hmmer 458.sjeng 462.libquantum 464.h264ref 470.lbm 482.sphinx3 \
    444.namd 447.dealII 450.soplex 453.povray 471.omnetpp 473.astar 483.xalancbmk;
do
    cp -r $NEW/$bmk/src $NEWSRC
    cp -r $ORIG/$bmk/src $SRC
    diff -rupN $SRC $NEWSRC > $bmk.patch
    if [ ! -s $bmk.patch ]; then
        rm $bmk.patch
    else
        chmod -w $bmk.patch
    fi
    rm -r $NEWSRC $SRC
    echo $bmk
done
