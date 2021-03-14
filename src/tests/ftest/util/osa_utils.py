#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import time
import threading

from avocado import fail_on
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from command_utils import CommandFailure
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from mpio_utils import MpioUtils
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj, DaosApiError)

try:
    # python 3.x
    import queue as test_queue
except ImportError:
    # python 2.7
    import Queue as test_queue


class OSAUtils(MdtestBase, IorTestBase):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAUtils, self).setUp()
        self.pool_cont_dict = {}
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
        self.out_queue = test_queue.Queue()
        self.dmg_command.exit_status_exception = False
        self.test_during_aggregation = False
        self.test_during_rebuild = False

    @fail_on(CommandFailure)
    def get_pool_leader(self):
        """Get the pool leader.

        Returns:
            int: pool leader value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["leader"])

    @fail_on(CommandFailure)
    def get_rebuild_status(self):
        """Get the rebuild status.

        Returns:
            str: reuild status

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return data["rebuild"]["status"]

    @fail_on(CommandFailure)
    def is_rebuild_done(self, time_interval,
                        wait_for_rebuild_to_complete=False):
        """Rebuild is completed/done.
        Args:
            time_interval: Wait interval between checks
            wait_for_rebuild_to_complete: Rebuild completed
                                          (Default: False)
        """
        self.pool.wait_for_rebuild(wait_for_rebuild_to_complete,
                                   interval=time_interval)

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
    def print_and_assert_on_rebuild_failure(self, out, timeout=3):
        """Print the out value (daos, dmg, etc) and check for rebuild
        completion. If not, raise assert.
        """
        self.log.info(out)
        self.is_rebuild_done(timeout)
        self.assert_on_rebuild_failure()

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["version"])

    def simple_exclude_reintegrate_loop(self, rank, loop_time=100):
        """This method performs exclude and reintegration on a rank,
        for a certain amount of time.
        """
        start_time = 0
        finish_time = 0
        while (int(finish_time - start_time) > loop_time):
            start_time = time.time()
            output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                   rank)
            self.print_and_assert_on_rebuild_failure(output)
            output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                       rank)
            self.print_and_assert_on_rebuild_failure(output)

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
                c_dkey = ctypes.create_string_buffer(d_key_value)
                a_key_value = "akey {0}".format(akey)
                c_akey = ctypes.create_string_buffer(a_key_value)
                c_value = ctypes.create_string_buffer(indata)
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
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
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

    def delete_extra_container(self, pool):
        """Delete the extra container in the pool.
        Args:
            pool (object): pool handle
        """
        self.pool.set_property("reclaim", "time")
        extra_container = self.pool_cont_dict[pool][2]
        extra_container.destroy()
        self.pool_cont_dict[pool][3] = None

    def run_ior_thread(self, action, oclass, test):
        """Start the IOR thread for either writing or
        reading data to/from a container.
        Args:
            action (str): Start the IOR thread with Read or
                          Write
            oclass (str): IOR object class
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
                                           "test": test,
                                           "flags": flags})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        process.join()

    def ior_thread(self, pool, oclass, test, flags):
        """Start threads and wait until all threads are finished.

        Args:
            pool (object): pool handle
            oclass (str): IOR object class
            test (list): IOR test sequence
            flags (str): IOR flags

        """
        self.pool = pool
        self.ior_cmd.get_params(self)
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        self.ior_cmd.dfs_oclass.update(oclass)
        self.ior_cmd.dfs_dir_oclass.update(oclass)
        self.log.info(self.pool_cont_dict)
        # If pool is not in the dictionary,
        # initialize its container list to None
        # {poolA : [None, None], [None, None]}
        if self.pool not in self.pool_cont_dict:
            self.pool_cont_dict[self.pool] = [None] * 4
        # Create container if the pool doesn't have one.
        # Otherwise, use the existing container in the pool.
        # pool_cont_dict {pool A: [containerA, Updated,
        #                          containerB, Updated],
        #                 pool B : containerA, Updated,
        #                          containerB, None]}
        if self.pool_cont_dict[self.pool][0] is None:
            self.add_container(self.pool)
            self.pool_cont_dict[self.pool][0] = self.container
            self.pool_cont_dict[self.pool][1] = "Updated"
        else:
            if ((self.test_during_aggregation is True) and
               (self.pool_cont_dict[self.pool][1] == "Updated") and
               (self.pool_cont_dict[self.pool][3] is None) and
               ("-w" in flags)):
                # Write to the second container
                self.add_container(self.pool)
                self.pool_cont_dict[self.pool][2] = self.container
                self.pool_cont_dict[self.pool][3] = "Updated"
            else:
                self.container = self.pool_cont_dict[self.pool][0]
        job_manager = self.get_ior_job_manager_command()
        job_manager.job.dfs_cont.update(self.container.uuid)
        self.ior_cmd.transfer_size.update(test[2])
        self.ior_cmd.block_size.update(test[3])
        self.ior_cmd.flags.update(flags)
        self.run_ior_with_pool(create_pool=False, create_cont=False)

    def run_mdtest_thread(self):
        """Start mdtest thread and wait until thread completes.
        """
        # Create container only
        self.mdtest_cmd.dfs_destroy = False
        if self.container is None:
            self.add_container(self.pool)
        job_manager = self.get_mdtest_job_manager_command(self.manager)
        job_manager.job.dfs_cont.update(self.container.uuid)
        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.execute_mdtest)
        # Launch the MDtest thread
        process.start()
        # Wait for the thread to finish
        process.join()
