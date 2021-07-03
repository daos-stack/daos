#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger

import os
import random

from collections import Counter

def write_host_file(hostlist, path='/tmp', slots=1):
    """Write out a hostfile suitable for orterun.

    Args:
        hostlist (list): list of hosts to write to the hostfile
        path (str, optional): where to write the hostfile. Defaults to '/tmp'.
        slots (int, optional): slots per host to specify in the hostfile.
            Defaults to 1.

    Raises:
        ValueError: if no hosts have been specified

    Returns:
        str: the full path of the written hostfile

    """

    log = getLogger()
    unique = random.randint(1, 100000)

    if not os.path.exists(path):
        os.makedirs(path)
    hostfile = os.path.join(path, "".join(["hostfile", str(unique)]))

    if hostlist is None:
        raise ValueError("host list parameter must be provided.")

    log.info(
        "Writing hostfile:\n  hosts: %s\n  slots: %s\n  file:  %s",
        hostlist, slots, hostfile)
    with open(hostfile, "w") as hostfile_handle:
        for host in hostlist:
            if slots is None:
                hostfile_handle.write("{0}\n".format(host))
            else:
                hostfile_handle.write("{0} slots={1}\n".format(host, slots))

    return hostfile
