#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import queue
import time
import threading
import re

from avocado import fail_on
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from command_utils import CommandFailure
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj, DaosApiError)
from general_utils import create_string_buffer


class OSAUtils(MdtestBase, IorTestBase):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
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
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = False
        self.test_during_aggregation = False
        self.test_during_rebuild = False
        self.test_with_checksum = True

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
        return int(data["response"]["version"])

    def set_container(self, container):
        """Set the OSA utils container object.
        Args:
            container (obj) : Container object to be used
                              within OSA utils.
        """
        self.container = container

    def simple_exclude_reintegrate_loop(self, rank, loop_time=100):
        """This method performs exclude and reintegration on a rank,
        for a certain amount of time.
        """
        start_time = 0
        finish_time = 0
        while int(finish_time - start_time) > loop_time:
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

    def prepare_cont_ior_write_read(self, oclass, flags):
        """This method prepares the containers for
        IOR write and read invocations.
            To enable aggregation:
            - Create two containers and read always from
              first container
            Normal usage (use only a single container):
            - Create a single container and use the same.
        Args:
            oclass (str): IOR object class
            flags (str): IOR flags
        """
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
            self.add_container(self.pool, create=False)
            self.set_cont_class_properties(oclass)
            if self.test_with_checksum is False:
                tmp = self.get_object_replica_value(oclass)
                rf_value = "rf:{}".format(tmp - 1)
                self.update_cont_properties(rf_value)
            self.container.create()
            self.pool_cont_dict[self.pool][0] = self.container
            self.pool_cont_dict[self.pool][1] = "Updated"
        else:
            if ((self.test_during_aggregation is True) and
               (self.pool_cont_dict[self.pool][1] == "Updated") and
               (self.pool_cont_dict[self.pool][3] is None) and
               ("-w" in flags)):
                # Write to the second container
                self.add_container(self.pool, create=False)
                self.set_cont_class_properties(oclass)
                if self.test_with_checksum is False:
                    tmp = self.get_object_replica_value(oclass)
                    rf_value = "rf:{}".format(tmp - 1)
                    self.update_cont_properties(rf_value)
                self.container.create()
                self.pool_cont_dict[self.pool][2] = self.container
                self.pool_cont_dict[self.pool][3] = "Updated"
            else:
                self.container = self.pool_cont_dict[self.pool][0]

    def delete_extra_container(self, pool):
        """Delete the extra container in the pool.
        Refer prepare_cont_ior_write_read. This method
        should be called when OSA tests intend to
        enable aggregation.
        Args:
            pool (object): pool handle
        """
        self.pool.set_property("reclaim", "time")
        extra_container = self.pool_cont_dict[pool][2]
        extra_container.destroy()
        self.pool_cont_dict[pool][3] = None

    def get_object_replica_value(self, oclass):
        """ Get the object replica value for an object class.

        Args:
            oclass (str): Object Class (eg: RP_2G1,etc)

        Returns:
            value (int) : Object replica value
        """
        value = 0
        if "_" in oclass:
            replica_list = oclass.split("_")
            value = replica_list[1][0]
        else:
            self.log.info("Wrong Object Class. Cannot split")
        return int(value)

    def update_cont_properties(self, cont_prop):
        """Update the existing container properties.
        Args:
            cont_prop (str): Replace existing container properties
                             with new value
        """
        self.container.properties.value = cont_prop

    def set_cont_class_properties(self, oclass="S1"):
        """Update the container class to match the IOR object
        class. Fix the rf factor based on object replica value.
        Also, remove the redundancy factor for S type
        object class.
        Args:
            oclass (str, optional): Container object class to be set.
                                    Defaults to "S1".
        """
        self.container.oclass.value = oclass
        # Set the container properties properly for S!, S2 class.
        # rf should not be set to 1 for S type object class.
        x = re.search("^S\\d$", oclass)
        prop = self.container.properties.value
        if x is not None:
            prop = prop.replace("rf:1", "rf:0")
        else:
            tmp = self.get_object_replica_value(oclass)
            rf_value = "rf:{}".format(tmp - 1)
            prop = prop.replace("rf:1", rf_value)
        self.container.properties.value = prop

    def assert_on_exception(self, out_queue=None):
        """Assert on exception while executing an application.

        Args:
            out_queue (queue): Check whether the queue is
            empty. If empty, app (ior, mdtest) didn't encounter error.
        """
        if out_queue is None:
            out_queue = self.out_queue
        if out_queue.empty():
            pass
        else:
            exc = out_queue.get(block=False)
            out_queue.put(exc)
            raise exc

    def cleanup_queue(self, out_queue=None):
        """Cleanup the existing thread queue.

        Args:
            out_queue (queue): Queue to cleanup.
        """
        if out_queue is None:
            out_queue = self.out_queue
        while not out_queue.empty():
            out_queue.get(block=True)

    def run_ior_thread(self, action, oclass, test,
                       single_cont_read=True,
                       fail_on_warning=True):
        """Start the IOR thread for either writing or
        reading data to/from a container.
        Args:
            action (str): Start the IOR thread with Read or
                          Write
            oclass (str): IOR object class
            test (list): IOR test sequence
            flags (str): IOR flags
            single_cont_read (bool) : Always read from the
                                      1st container.
                                      Defaults to True.
            fail_on_warning (bool)  : Test terminates
                                      for IOR warnings.
                                      Defaults to True.
        """
        self.cleanup_queue()
        if action == "Write":
            flags = self.ior_w_flags
        else:
            flags = self.ior_r_flags

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.ior_thread,
                                   kwargs={"pool": self.pool,
                                           "oclass": oclass,
                                           "test": test,
                                           "flags": flags,
                                           "single_cont_read":
                                           single_cont_read,
                                           "fail_on_warning":
                                           fail_on_warning})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        try:
            process.join()
        except CommandFailure as err_msg:
            self.out_queue.put(err_msg)
            self.assert_on_exception()

    def ior_thread(self, pool, oclass, test, flags,
                   single_cont_read=True,
                   fail_on_warning=True):
        """Start an IOR thread.

        Args:
            pool (object): pool handle
            oclass (str): IOR object class, container class.
            test (list): IOR test sequence
            flags (str): IOR flags
            single_cont_read (bool) : Always read from the
                                      1st container.
                                      Defaults to True.
            fail_on_warning (bool)  : Test terminates
                                      for IOR warnings.
                                      Defaults to True.
        """
        self.cleanup_queue()
        self.pool = pool
        self.ior_cmd.get_params(self)
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        self.ior_cmd.dfs_oclass.update(oclass)
        self.ior_cmd.dfs_dir_oclass.update(oclass)
        if single_cont_read is True:
            # Prepare the containers created and use in a specific
            # way defined in prepare_cont_ior_write.
            self.prepare_cont_ior_write_read(oclass, flags)
        elif single_cont_read is False and self.container is not None:
            # Here self.container is having actual value. Just use it.
            self.log.info(self.container)
        else:
            self.fail("Not supported option on ior_thread")
        try:
            job_manager = self.get_ior_job_manager_command()
        except CommandFailure as err_msg:
            self.out_queue.put(err_msg)
            self.assert_on_exception()
        job_manager.job.dfs_cont.update(self.container.uuid)
        self.ior_cmd.transfer_size.update(test[2])
        self.ior_cmd.block_size.update(test[3])
        self.ior_cmd.flags.update(flags)
        try:
            self.run_ior_with_pool(create_pool=False, create_cont=False,
                                   fail_on_warning=fail_on_warning)
        except CommandFailure as err_msg:
            self.out_queue.put(err_msg)
            self.assert_on_exception()

    def run_mdtest_thread(self):
        """Start mdtest thread and wait until thread completes.
        """
        # Create container only
        self.mdtest_cmd.dfs_destroy = False
        if self.container is None:
            self.add_container(self.pool, create=False)
            self.set_cont_class_properties(self.mdtest_cmd.dfs_oclass)
            if self.test_with_checksum is False:
                tmp = self.get_object_replica_value(self.mdtest_cmd.dfs_oclass)
                rf_value = "rf:{}".format(tmp - 1)
                self.update_cont_properties(rf_value)
            self.container.create()
        job_manager = self.get_mdtest_job_manager_command(self.manager)
        job_manager.job.dfs_cont.update(self.container.uuid)
        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.execute_mdtest)
        # Launch the MDtest thread
        process.start()
        # Wait for the thread to finish
        try:
            process.join()
        except CommandFailure as err_msg:
            self.out_queue.put(err_msg)
            self.assert_on_exception()
