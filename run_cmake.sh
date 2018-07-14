


mkdir -p ./BUILD/release

cd BUILD/release

cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
                          -DCMAKE_MACOSX_RPATH=NEW \
                          ../..