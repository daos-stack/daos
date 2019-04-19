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
    host = Ssh(hostname, debug=debug)
    host.connect()
    cmd = ["sudo /usr/bin/mkdir -p /opt/daos",
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
    host = Ssh(hostname, debug=debug)
    host.connect()
    cmd = ["sudo HUGEMEM=4096 TARGET_USER=\"{0}\" {1} reset"
           .format(getpass.getuser(), SPDK_SETUP_SCRIPT),\
           "sudo /usr/bin/rm -f /dev/hugepages/*",
           "sudo /usr/bin/rm -rf /opt/daos/"]
    rccode = host.call(" && ".join(cmd))
    if rccode is not 0:
        print("--- FAIL --- Failed SPDK setup command on {} with Error {}"
              .format(hostname, rccode))
        result_queue.put("FAIL")
    result_queue.put("PASS")
    host.disconnect()

def nvme(hostlist, operation="cleanup"):
    """
    nvme function called from Avocado test, This will start
    thread for each server doing SPDK setup or cleanup.
    Args:
        hostlist[list]: List of servers hostname.
        operation: setup or cleanup
    return:
        None
    """
    function = {
        'setup': nvme_setup_thread,
        'cleanup': nvme_cleanup_thread
        }
    print("NVMe server {} Started......".format(operation))
    threads = []
    out_queue = Queue.Queue()
    for _hosts in hostlist:
        threads.append(threading.Thread(target=function[operation],
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
            raise SpdkFailed("{} Failed".format(operation))
    print("NVMe server {} Finished......".format(operation))
