What is mchecksum?
==================

MChecksum is a generic checksum library.

Please see the accompanying LICENSE.txt file for license details.

Supported platforms
===================

Linux / MacOS

Documentation
=============

Please see the documentation available on the mercury [website][documentation]
for a quick introduction to mchecksum.

Software requirements
=====================

CRC32 and Adler32 checksums require ZLIB.
CRC32C can be used with or without HW acceleration -- SSE4.2 acceleration
requires the SSE4.2 instruction set to be supported by the CPU. Improved
performance can also be achieved using the Intel(R) [ISA-L][isal] library but
requires the PCLMULQDQ instruction to be supported.

Building
========

If you install the full sources, put the tarball in a directory where you
have permissions (e.g., your home directory) and unpack it:

    gzip -cd mchecksum-X.tar.gz | tar xvf -

   or

    bzip2 -dc mchecksum-X.tar.bz2 | tar xvf -

Replace `'X'` with the version number of the package.

MChecksum makes use of the CMake build-system and requires that you do an
out-of-source build. In order to do that, you must create a new build
directory and run the `ccmake` command from it:

    cd mchecksum-X
    mkdir build
    cd build
    ccmake .. (where ".." is the relative path to the mchecksum-X directory)

Type `'c'` multiple times and choose suitable options. Recommended options are:

    BUILD_SHARED_LIBS                ON (or OFF if the library you link
                                     against requires static libraries)
    BUILD_TESTING                    ON
    CMAKE_INSTALL_PREFIX             /path/to/install/directory
    MCHECKSUM_USE_ISAL               OFF (Optional)
    MCHECKSUM_USE_SSE4_2             ON
    MCHECKSUM_USE_ZLIB               OFF (Optional)

Setting include directory and library paths may require you to toggle to
the advanced mode by typing `'t'`. Once you are done and do not see any
errors, type `'g'` to generate makefiles. Once you exit the CMake
configuration screen and are ready to build the targets, do:

    make

(Optional) Verbose compile/build output:

This is done by inserting `VERBOSE=1` in the `make` command. E.g.:

    make VERBOSE=1

Installing
==========

Assuming that the `CMAKE_INSTALL_PREFIX` has been set (see previous step)
and that you have write permissions to the destination directory, do
from the build directory:

    make install

Testing
=======

CTest is used to run the tests, simply run from the build directory:

    ctest .

(Optional) Verbose testing:

This is done by inserting `-V` in the `ctest` command.  E.g.:

    ctest -V .

Extra verbose information can be displayed by inserting `-VV`. E.g.:

    ctest -VV .

[documentation]: https://mercury-hpc.github.io/user/checksum/
[isal]: https://github.com/01org/isa-l
