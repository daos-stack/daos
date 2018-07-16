#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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

#import os
#import sys
import time
import random
import string

def ContinousIo(container, seconds):
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
        oid, epoch = container.write_an_obj(data, size, dkey, akey, oid)
        data2 = container.read_an_obj(size, dkey, akey, oid, epoch)

        # verify it came back correctly
        if data != data2.value:
            print "data>{}".format(data)
            print "data2>{}".format(data2.value)
            raise ValueError("Data mismatch in ContinousIo")

        total_written += size
    return total_written

def WriteUntilFull(container):
    """ write until we get enospace back """

    total_written = 0
    size = 2048
    oid = None

    try:
        while True:
            # make some stuff up and write
            dkey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            akey = ''.join(random.choice(string.ascii_uppercase + string.digits)
                           for _ in range(5))
            data = ''.join(random.choice(string.ascii_uppercase + string.digits)
                       for _ in range(size))

            oid, epoch = container.write_an_obj(data, size, dkey, akey)
            total_written += size

    except ValueError as exp:
        print exp

    return total_written
