#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

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

import os

def get_hosts_from_file(hostfile):
    """
    Return the list of hosts from a given host file.
    """
    hosts = []
    if os.path.exists(hostfile):
        for line in open(hostfile, "r").readlines():
            hosts.append(line.split(' ', 1)[0])

    return hosts

def main():
    """
    Entry point for standalone run.
    """
    print(get_hosts_from_file('/tmp/hostfile'))

if __name__ == "__main__":
    main()
