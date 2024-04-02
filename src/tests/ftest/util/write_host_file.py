"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from logging import getLogger
from tempfile import mkstemp


def write_host_file(hosts, path='/tmp', slots=None):
    """Write out a hostfile suitable for orterun.

    Args:
        hosts (NodeSet): hosts to write to the hostfile
        path (str, optional): where to write the hostfile. Defaults to '/tmp'.
        slots (int, optional): slots per host to specify in the hostfile.
            Defaults to None.

    Raises:
        ValueError: if no hosts have been specified

    Returns:
        str: the full path of the written hostfile

    """
    if not hosts:
        raise ValueError("hosts parameter must be provided.")

    log = getLogger()
    os.makedirs(path, exist_ok=True)

    _, hostfile = mkstemp(dir=path, prefix='hostfile_')

    log.debug("Writing hostfile: %s (hosts=%s, slots=%s)", hostfile, hosts, slots)
    with open(hostfile, "w") as hostfile_handle:
        if slots:
            hostfile_handle.writelines(f'{host} slots={slots}\n' for host in sorted(hosts))
        else:
            hostfile_handle.writelines(f'{host}\n' for host in sorted(hosts))

    return hostfile
