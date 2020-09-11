#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
"""
from logging import getLogger

import os
import random


def write_host_file(hostlist, path='/tmp', slots=1, segment=" slots="):
    """Write out a hostfile suitable for orterun.

    Args:
        hostlist (list): list of hosts to write to the hostfile
        path (str, optional): where to write the hostfile. Defaults to '/tmp'.
        slots (int, optional): slots per host to specify in the hostfile.
            Defaults to 1.
        segment (str, optional): string used to identify the slot segment per
            host. Defaults to " slots=" for openmpi. The ":" segment should be
            used for mpich.

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
        hostlist, slots if not slots else "".join([segment, str(slots)]),
        hostfile)
    with open(hostfile, "w") as hostfile_handle:
        for host in hostlist:
            if slots is None:
                hostfile_handle.write("{0}\n".format(host))
            else:
                hostfile_handle.write(
                    "{0}{1}{2}\n".format(host, segment, slots))

    return hostfile
