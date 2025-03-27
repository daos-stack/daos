..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2018 Intel Corporation.

ISA-L Compression Poll Mode Driver
==================================

The ISA-L PMD (**librte_compress_isal**) provides poll mode compression &
decompression driver support for utilizing Intel ISA-L library,
which implements the deflate algorithm for both Deflate(compression) and Inflate(decompression).


Features
--------

ISA-L PMD has support for:

Compression/Decompression algorithm:

    * DEFLATE

Huffman code type:

    * FIXED
    * DYNAMIC

Window size support:

    * 32K

Checksum:

    * CRC32
    * ADLER32

To enable a checksum in the driver, the compression and/or decompression xform
structure, rte_comp_xform, must be filled with either of the CompressDev
checksum flags supported. ::

 compress_xform->compress.chksum = RTE_COMP_CHECKSUM_CRC32

 decompress_xform->decompress.chksum = RTE_COMP_CHECKSUM_CRC32

::

 compress_xform->compress.chksum = RTE_COMP_CHECKSUM_ADLER32

 decompress_xform->decompress.chksum = RTE_COMP_CHECKSUM_ADLER32

If you request a checksum for compression or decompression,
the checksum field in the operation structure,  ``op->output_chksum``,
will be filled with the checksum.

.. Note::

 For the compression case above, your output buffer will need to be large enough to hold the compressed data plus a scratchpad for the checksum at the end, the scratchpad is 8 bytes for CRC32 and 4 bytes for Adler32.

Level guide:

The ISA-L levels have been mapped to somewhat correspond to the same ZLIB level,
i.e. ZLIB L1 gives a compression ratio similar to ISA-L L1.
Compressdev level 0 enables "No Compression", which passes the uncompressed
data to the output buffer, plus deflate headers.
The ISA-L library does not support this, therefore compressdev level 0 is not supported.

The compressdev API has 10 levels, 0-9. ISA-L has 4 levels of compression, 0-3.
As a result the level mappings from the API to the PMD are shown below.

.. _table_ISA-L_compression_levels:

.. table:: Level mapping from Compressdev to ISA-L PMD.

   +-------------+----------------------------------------------+-----------------------------------------------+
   | Compressdev | PMD Functionality                            | Internal ISA-L                                |
   | API Level   |                                              | Level                                         |
   +=============+==============================================+===============================================+
   | 0           | No compression, Not Supported                | ---                                           |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 1           | Dynamic (Fast compression)                   | 1                                             |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 2           | Dynamic                                      | 2                                             |
   |             | (Higher compression ratio)                   |                                               |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 3           | Dynamic                                      | 3                                             |
   |             | (Best compression ratio)                     | (Level 2 if                                   |
   |             |                                              | no AVX512/AVX2)                               |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 4           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 5           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 6           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 7           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 8           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+
   | 9           | Dynamic (Best compression ratio)             | Same as above                                 |
   +-------------+----------------------------------------------+-----------------------------------------------+

.. Note::

 The above table only shows mapping when API calls for dynamic compression.
 For fixed compression, regardless of API level, internally ISA-L level 0 is always used.


Limitations
-----------

* Compressdev level 0, no compression, is not supported.

Installation
------------

* To build DPDK with Intel's ISA-L library, the user is required to download the library from `<https://github.com/01org/isa-l>`_.

* Once downloaded, the user needs to build the library, the ISA-L autotools are usually sufficient::

    ./autogen.sh
    ./configure

* make can  be used to install the library on their system, before building DPDK::

    make
    sudo make install

* To build with meson, the **libisal.pc** file, must be copied into "pkgconfig",
  e.g. /usr/lib/pkgconfig or /usr/lib64/pkgconfig depending on your system,
  for meson to find the ISA-L library. The **libisal.pc** is located in library sources::

    cp isal/libisal.pc /usr/lib/pkgconfig/


Initialization
--------------

To use the PMD in an application, user must:

* Call ``rte_vdev_init("compress_isal")`` within the application.

* Use ``--vdev="compress_isal"`` in the EAL options, which will call ``rte_vdev_init()`` internally.

The following parameter (optional) can be provided in the previous two calls:

* ``socket_id:`` Specify the socket where the memory for the device is going to be allocated
  (by default, socket_id will be the socket where the core that is creating the PMD is running on).
