#! /bin/sh

# Build cmake project
mkdir -p build
cd build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j4
cd ..

# Run the built executable
./bin/Debug/x64/WindowManagerTest
