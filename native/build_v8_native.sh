make dependencies
export NATIVE_SDK=/home/ben/native/toolchain # you may replace the native sdk path to your own
export CXX="$NATIVE_SDK/bin/clang++"
export CC="$NATIVE_SDK/bin/clang"
export CPP="$CC -E"
export LINK="$NATIVE_SDK/bin/clang++"
make x64.release disassembler=on snapshot=off werror=no -j4
