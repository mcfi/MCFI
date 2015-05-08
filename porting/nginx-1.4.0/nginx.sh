#!/bin/bash

# Steps to compile nginx
# 1. download nginx-1.4.0 and decompress it
# 2. apply nginx.patch to nginx-1.4.0
# 3. download zlib-1.2.8 and decompress it to nginx-1.4.0
# 4. copy this script to nginx-1.4.0 and set MCFI and PREFIX below
# 5. exeucte this script, and you will find the nginx executable in $PREFIX/sbin/

MCFI="`dirname ~`/`basename ~`/MCFI"
PREFIX="$MCFI/server"

./configure --with-cc=$MCFI/toolchain/bin/clang --with-cc-opt="-O2" --with-zlib=./zlib-1.2.8 --with-ipv6  --with-http_realip_module --with-http_addition_module --with-http_sub_module --with-http_random_index_module --with-http_secure_link_module --with-http_degradation_module --with-http_stub_status_module --without-pcre --without-http_rewrite_module --with-mail --prefix="$PREFIX"

make install
