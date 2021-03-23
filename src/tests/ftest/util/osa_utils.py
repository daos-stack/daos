#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import queue
import time
import threading

from avocado import fail_on
from ior_test_base import IorTestBase
from command_utils import CommandFailure
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from mpio_utils import MpioUtils
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj, DaosApiError)
from general_utils import create_string_buffer


class OSAUtils(IorTestBase):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.container = None
        self.obj = None
        self.ioreq = None
        self.dmg_command = self.get_dmg_command()
        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*',
                                           default=[0])[0]
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*',
                                           default=[0])[0]
        self.record_length = self.params.get("length", '/run/record/*',
                                             default=[0])[0]
        self.ior_w_flags = self.params.get("write_flags", '/run/ior/iorflags/*',
                                           default="")
        self.ior_r_flags = self.params.get("read_flags", '/run/ior/iorflags/*')
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = False

    @fail_on(CommandFailure)
    def get_pool_leader(self):
        """Get the pool leader.

        Returns:
            int: pool leader value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["response"]["leader"])

    @fail_on(CommandFailure)
    def get_rebuild_status(self):
        """Get the rebuild status.

        Returns:
            str: reuild status

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return data["response"]["rebuild"]["status"]

    @fail_on(CommandFailure)
    def is_rebuild_done(self, time_interval):
        """Rebuild is completed/done.
        Args:
            time_interval: Wait interval between checks
        Returns:
            False: If rebuild_status not "done" or "completed".
            True: If rebuild status is "done" or "completed".
        """
        status = False
        fail_count = 0
        completion_flag = ["done", "completed"]
        while fail_count <= 20:
            rebuild_status = self.get_rebuild_status()
            time.sleep(time_interval)
            fail_count += 1
            if rebuild_status in completion_flag:
                status = True
                break
        return status

    @fail_on(CommandFailure)
    def assert_on_rebuild_failure(self):
        """If the rebuild is not successful,
        raise assert.
        """
        rebuild_status = self.get_rebuild_status()
        self.log.info("Rebuild Status: %s", rebuild_status)
        rebuild_failed_string = ["failed", "scanning", "aborted", "busy"]
        self.assertTrue(rebuild_status not in rebuild_failed_string,
                        "Rebuild failed")

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["response"]["version"])

    @fail_on(DaosApiError)
    def write_single_object(self):
        """Write some data to the existing pool."""
        self.pool.connect(2)
        csum = self.params.get("enable_checksum", '/run/container/*')
        self.container = DaosContainer(self.context)
        input_param = self.container.cont_input_values
        input_param.enable_chksum = csum
        self.container.create(poh=self.pool.pool.handle,
                              con_prop=input_param)
        self.container.open()
        self.obj = DaosObj(self.context, self.container)
        self.obj.create(objcls=1)
        self.obj.open()
        self.ioreq = IORequest(self.context,
                               self.container,
                               self.obj, objtype=4)
        self.log.info("Writing the Single Dataset")
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0])
                          * self.record_length)
                d_key_value = "dkey {0}".format(dkey)
                c_dkey = create_string_buffer(d_key_value)
                a_key_value = "akey {0}".format(akey)
                c_akey = create_string_buffer(a_key_value)
                c_value = create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))
                self.ioreq.single_insert(c_dkey, c_akey, c_value, c_size)
        self.obj.close()
        self.container.close()

    @fail_on(DaosApiError)
    def verify_single_object(self):
        """Verify the container data on the existing pool."""
        self.pool.connect(2)
        self.container.open()
        self.obj.open()
        self.log.info("Single Dataset Verification -- Started")
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0]) *
                          self.record_length)
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                val = self.ioreq.single_fetch(c_dkey,
                                              c_akey,
                                              len(indata)+1)
                if indata != (repr(val.value)[1:-1]):
                    self.d_log.error("ERROR:Data mismatch for "
                                     "dkey = {0}, "
                                     "akey = {1}".format(
                                         "dkey {0}".format(dkey),
                                         "akey {0}".format(akey)))
                    self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}"
                              .format("dkey {0}".format(dkey),
                                      "akey {0}".format(akey)))
        self.obj.close()
        self.container.close()

    def run_ior_thread(self, action, oclass, api, test):
        """Start the IOR thread for either writing or
        reading data to/from a container.
        Args:
            action (str): Start the IOR thread with Read or
                          Write
            oclass (str): IOR object class
            api (str): IOR API
            test (list): IOR test sequence
            flags (str): IOR flags
        """
        if action == "Write":
            flags = self.ior_w_flags
        else:
            flags = self.ior_r_flags

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.ior_thread,
                                   kwargs={"pool": self.pool,
                                           "oclass": oclass,
                                           "api": api,
                                           "test": test,
                                           "flags": flags,
                                           "results":
                                           self.out_queue})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        process.join()

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """Start threads and wait until all threads are finished.

        Args:
            pool (object): pool handle
            oclass (str): IOR object class
            api (str): IOR api
            test (list): IOR test sequence
            flags (str): IOR flags
            results (queue): queue for returning thread results

        """
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test : Mpich not installed on :"
                      " {}".format(self.hostfile_clients[0]))
        self.pool = pool
        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, self.pool)
        ior_cmd.dfs_oclass.update(oclass)
        ior_cmd.dfs_dir_oclass.update(oclass)
        ior_cmd.api.update(api)
        ior_cmd.transfer_size.update(test[2])
        ior_cmd.block_size.update(test[3])
        ior_cmd.flags.update(flags)

        # Define the job manager for the IOR command
        self.job_manager = Mpirun(ior_cmd, mpitype="mpich")
        # Create container only
        if self.container is None:
            self.add_container(self.pool)
        self.job_manager.job.dfs_cont.update(self.container.uuid)
        env = ior_cmd.get_default_env(str(self.job_manager))
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(self.processes)
        self.job_manager.assign_environment(env, True)

        # run IOR Command
        try:
            self.job_manager.run()
        except CommandFailure as _error:
            results.put("FAIL")
