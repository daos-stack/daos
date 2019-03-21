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

import os
import sys
import random

def WriteHostFile(hostlist, path='/tmp', slots=1):
    """ write out a hostfile suitable for orterun """

    unique = random.randint(1,100000)

    if not os.path.exists(path):
        os.makedirs(path)
    hostfile = path + '/hostfile' + str(unique)

    if hostlist is None:
        raise ValueError("host list parameter must be provided.")
    f = open(hostfile, 'w')

    for host in hostlist:
        if slots is None:
            print "<<{}>>".format(slots)
            f.write("{0}\n".format(host))
        else:
            print "<<{}>>".format(slots)
            f.write("{0} slots={1}\n".format(host, slots))
    f.close()
    return hostfile
