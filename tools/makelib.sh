#! /bin/sh

LIBNAME=libaosen.so
LIBPATH=/usr/local/lib/

CORE=src/core/*.c 
JSON=src/json/*.c
HTTP=src/http/*.c 
CLIENT=src/client/*.c


gcc -shared $CORE $JSON  $HTTP $CLIENT -o $LIBNAME -fPIC
cp $LIBNAME $LIBPATH
ldconfig
