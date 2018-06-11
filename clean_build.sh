#!/bin/sh

rm -rf build
mkdir -p build
pushd build
cmake ../
make
popd


