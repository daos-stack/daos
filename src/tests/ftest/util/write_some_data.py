#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import sys
import numpy as np

from daos_io import DaosFile

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
