#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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
