#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

import sys
import numpy as np

from pydaos.raw.daos_io import DaosFile

class DaosIOFailed(Exception):
    """ DAOS I/O failure of some sort. """

def write_some_data():
    """
    Write sys.argv[1] bytes to the file handle specified by sys.argv[2]
    """
    sizeinbytes = long(sys.argv[1])
    filename = sys.argv[2]

    # create some data to write, integers 0,1,2,...
    data = np.arange(sizeinbytes, dtype=np.int8)

    # create a file
    file_handle = DaosFile.open(filename, DaosFile.MODE_RDWR_CREATE)

    # dump some data into it and close
    file_handle.write(data)
    file_handle.close()

    # reopen and read the last byte back
    file_handle = DaosFile.open(filename, DaosFile.MODE_RDWR_CREATE)
    rdata1 = np.zeros(1, dtype=np.uint8)
    file_handle.read_at(sizeinbytes-10, rdata1)
    file_handle.close()

    # we know what it should be
    if not rdata1 == ((sizeinbytes-10) % 256):
        print("value is {0}".format(rdata1))
        raise DaosIOFailed

if __name__ == "__main__":
    write_some_data()
