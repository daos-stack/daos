#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

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

import getpass
import threading
import Queue

from ssh_connection import Ssh
from paramiko import SSHException

SPDK_SETUP_SCRIPT = "/opt/daos/spdk/scripts/setup.sh"

class SpdkFailed(Exception):
    """ SPDK Setup/Cleanup did not work """

def nvme_setup_thread(result_queue, hostname, debug=True):
    """
    nvme setup thread function
    Arge:
        result_queue: Queue for PASS/FAIL
        hostname: server name
        debug: print the debug message, default True
    """
    try:
        host = Ssh(hostname, debug=debug)
        host.connect()
        cmd = ["sudo /usr/binasd/mkdasdir -p /opt/daos",
               "sudo /usr/bin/git clone https://github.com/spdk/spdk.git" \
               " /opt/daos/spdk",
               "sudo HUGEMEM=4096 TARGET_USER=\"{0}\" {1}"
               .format(getpass.getuser(), SPDK_SETUP_SCRIPT),
               "sudo /usr/bin/chmod 777 /dev/hugepages",
               "sudo /usr/bin/chmod 666 /dev/uio*",
               "sudo /usr/bin/chmod 666 \
               /sys/class/uio/uio*/device/config",
               "sudo /usr/bin/chmod 666 \
               /sys/class/uio/uio*/device/resource*",
               "sudo /usr/bin/rm -f /dev/hugepages/*"]
        rccode = host.call(" && ".join(cmd))
    except SSHException:
        result_queue.put("FAIL")
        raise
    if rccode is not 0:
        print("--- FAIL --- Failed SPDK setup command on {} with Error {}"
              .format(hostname, rccode))
        result_queue.put("FAIL")
    result_queue.put("PASS")
    host.disconnect()

def nvme_cleanup_thread(result_queue, hostname, debug=True):
    """
    nvme cleanup thread function
    Args:
        result_queue: Queue for PASS/FAIL
        hostname: server name
        debug: print the debug message, default True
    """
    try:
        host = Ssh(hostname, debug=debug)
        host.connect()
        cmd = ["sudo HUGEMEM=4096 TARGET_USER=\"{0}\" {1} reset"
               .format(getpass.getuser(), SPDK_SETUP_SCRIPT),\
               "sudo /usr/bin/rm -f /dev/hugepages/*",
               "sudo /usr/bin/rm -rf /opt/daos/"]
        rccode = host.call(" && ".join(cmd))
    except SSHException:
        result_queue.put("FAIL")
        raise
    if rccode is not 0:
        print("--- FAIL --- Failed SPDK setup command on {} with Error {}"
              .format(hostname, rccode))
        result_queue.put("FAIL")
    result_queue.put("PASS")
    host.disconnect()

def _nvme_main(hostlist, operation=False):
    """
    nvme main function starts thread for each server doing SPDK setup/cleanup.
    Args:
        hostlist[list]: List of servers hostname.
        operation:
            True: setup
            False: cleanup
    return:
        None
    """
    _oper = ["cleanup", nvme_cleanup_thread]
    if operation:
        _oper = ["setup", nvme_setup_thread]
    print("NVMe server {} Started......".format(_oper[0]))
    threads = []
    out_queue = Queue.Queue()
    for _hosts in hostlist:
        threads.append(threading.Thread(target=_oper[1],
                                        args=(out_queue, _hosts)))
    #Start Threads
    for thrd in threads:
        thrd.start()
    #Wait for Threads to finish.
    for thrd in threads:
        thrd.join()

    #Check failure message in queue, and raise SpdkFailed if any thread Fails
    while not out_queue.empty():
        if out_queue.get() == "FAIL":
            raise SpdkFailed("SPDK {} Failed".format(_oper[0]))
    print("NVMe server {} Finished......".format(_oper[0]))

def nvme_setup(hostlist):
    """
    nvme setup function called from Avocado test, This will start
    thread for each server doing SPDK setup.
    Args:
        hostlist[list]: List of servers hostname.
    return:
        None
    """
    _nvme_main(hostlist, True)

def nvme_cleanup(hostlist):
    """
    nvme clenaup function called from Avocado test, This will start
    thread for each server doing SPDK cleanup.
    Args:
        hostlist[list]: List of servers hostname.
    return:
        None
    """
    _nvme_main(hostlist, False)
