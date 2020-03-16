# Native Programming Interface

## Building against the DAOS library

To build application or I/O middleware against the native DAOS API, include
the daos.h header file in your program and link with -Ldaos. Examples are
available under src/tests.

## DAOS API Reference

libdaos is written in C and uses Doxygen comments that are added to C header
files.

\[TODO] Generate Doxygen document and add a link here.

## Bindings to Different Languages

API bindings to both Python[^1] and Go[^2] languages are available.

[^1]: https://github.com/daos-stack/daos/blob/master/src/client/pydaos/raw/README.md

[^2]: https://godoc.org/github.com/daos-stack/go-daos/pkg/daos

[^3]: https://bitbucket.hdfgroup.org/projects/HDF5VOL/repos/daos-vol/browse/README.md
