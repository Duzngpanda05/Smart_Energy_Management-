# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/IDF5.5/Espressif/frameworks/esp-idf-v4.4.6/components/bootloader/subproject"
  "D:/IDF5.5/Final/build/bootloader"
  "D:/IDF5.5/Final/build/bootloader-prefix"
  "D:/IDF5.5/Final/build/bootloader-prefix/tmp"
  "D:/IDF5.5/Final/build/bootloader-prefix/src/bootloader-stamp"
  "D:/IDF5.5/Final/build/bootloader-prefix/src"
  "D:/IDF5.5/Final/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/IDF5.5/Final/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
