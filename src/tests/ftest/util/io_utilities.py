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

import time
import random
import string

def continuous_io(container, seconds):
    """ Perform a combination of reads/writes for the specified time period """

    finish_time = time.time() + seconds
    oid = None
    total_written = 0
    size = 500

    while time.time() < finish_time:
        # make some stuff up
        dkey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                       for _ in range(5))
        akey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                       for _ in range(5))
        data = ''.join(random.choice(string.ascii_uppercase + string.digits)
                       for _ in range(size))

        # write it then read it back
        oid, epoch = container.write_an_obj(data, size, dkey, akey, oid, 5)
        data2 = container.read_an_obj(size, dkey, akey, oid, epoch)

        # verify it came back correctly
        if data != data2.value:
            raise ValueError("Data mismatch in ContinousIo")

        # collapse down the commited epochs
        container.consolidate_epochs()

        total_written += size

    return total_written

def write_until_full(container):
    """
    write until we get enospace back
    """

    total_written = 0
    size = 2048
    _oid = None

    try:
        while True:
            # make some stuff up and write
            dkey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            akey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            data = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(size))

            _oid, _epoch = container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the commited epochs
            container.slip_epoch()


    except ValueError as exp:
        print(exp)

    return total_written

def write_quantity(container, size_in_bytes):
    """ Write a specific number of bytes.  Note the minimum amount
        that will be written is 2048 bytes.

        container --which container to write to, it should be in an open
                    state prior to the call
        size_in_bytes --number of bytes to be written, although no less that
                        2048 will be written.
    """

    total_written = 0
    size = 2048
    _oid = None

    try:
        while total_written < size_in_bytes:

            # make some stuff up and write
            dkey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            akey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            data = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(size))

            _oid, _epoch = container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the commited epochs
            container.slip_epoch()

    except ValueError as exp:
        print(exp)

    return total_written
