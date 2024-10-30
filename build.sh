#!/usr/bin/bash

rm -Rf build
mkdir build
cmake --preset "Linux-Release" -B build -S .
cmake --build --preset "Linux-Release" -j 24
