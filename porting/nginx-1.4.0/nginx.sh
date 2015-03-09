#!/bin/bash

# Apply nginx.patch to vanilla nginx-1.4.0
# cp nginx.sh in nginx-1.4.0 and chmod +x nginx.sh
# ./nginx.sh
# nginx will be installed to $PREFIX/
# sudo ./nginx and try accessing http://localhost

MCFI_SDK_PATH=
PREFIX=~/MCFI/server
CC=$MCFI_SDK_PATH/bin/clang

./configure --with-cc=$CC --without-pcre --without-http_rewrite_module --without-http_gzip_module --with-cc-opt="-O2" --prefix=/home/ben/MCFI/server --sbin-path=$PREFIX/nginx --conf-path=$PREFIX/conf --error-log-path=$PREFIX/error --pid-path=$PREFIX/pid --lock-path=$PREFIX/lock

make install
