#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import os
import random


def write_host_file(hosts, path='/tmp', slots=1):
    """Write out a hostfile suitable for orterun.

    Args:
        hosts (NodeSet): hosts to write to the hostfile
        path (str, optional): where to write the hostfile. Defaults to '/tmp'.
        slots (int, optional): slots per host to specify in the hostfile.
            Defaults to 1.

    Raises:
        ValueError: if no hosts have been specified

    Returns:
        str: the full path of the written hostfile

    """
    log = getLogger()
    unique = random.randint(1, 100000)  # nosec

    if not os.path.exists(path):
        os.makedirs(path)
    hostfile = os.path.join(path, "".join(["hostfile", str(unique)]))

    if not hosts:
        raise ValueError("host list parameter must be provided.")

    log.debug("Writing hostfile: %s (hosts=%s, slots=%s)", hostfile, hosts, slots)
    with open(hostfile, "w") as hostfile_handle:
        for host in hosts:
            hostfile_line = [host]
            if slots:
                hostfile_line.append(f"slots={slots}")
            hostfile_handle.write(f"{' '.join(hostfile_line)}\n")
            log.debug("  %s", " ".join(hostfile_line))

    return hostfile
