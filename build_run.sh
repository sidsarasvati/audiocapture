#!/bin/sh

mkdir -p build
pushd build
cmake ../
make
if [ $? -eq 0 ]; then
    echo "Build Succeeded"
else
    echo "Build Failed!"
    exit $?
fi    
    
popd

# run program; pass all arguments
./build/audiocapture "$@"


