"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import queue
import time
import threading
import re

from pydaos.raw import DaosContainer, IORequest, DaosObj, DaosApiError

from avocado import fail_on
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from exception_utils import CommandFailure
from general_utils import create_string_buffer, run_command


class OSAUtils(MdtestBase, IorTestBase):
    """Test Class Description: This test runs daos_server offline drain test cases.

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
        self.server_count = len(self.hostlist_servers)
        self.engine_count = self.server_managers[0].get_config_value(
            "engines_per_host")
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = False
        self.test_during_aggregation = False
        self.test_during_rebuild = False
        self.test_with_checksum = True
        # By default, test_with_rf is set to False.
        # It is up to individual test to enable it.
        self.test_with_rf = False
        self.test_with_blank_node = False
        self.test_with_snapshot = False

    @fail_on(CommandFailure)
    def assert_on_rebuild_failure(self):
        """If the rebuild is not successful, raise assert."""
        rebuild_status = self.pool.get_rebuild_status(True)
        self.log.info("Rebuild Status: %s", rebuild_status)
        if rebuild_status in ["failed", "scanning", "aborted", "busy"]:
            self.fail("Rebuild failed")

    @fail_on(CommandFailure)
    def print_and_assert_on_rebuild_failure(self, out, timeout=3):
        """Print the out value (daos, dmg, etc) and check for rebuild completion.

        If rebuild does not complete, raise an assertion.
        """
        self.log.info(out)
        self.pool.wait_for_rebuild_to_start()
        self.pool.wait_for_rebuild_to_end(timeout)
        self.assert_on_rebuild_failure()

    @fail_on(CommandFailure)
    def get_ipaddr_for_rank(self, rank=None):
        """Obtain the IPAddress and port number for a particular server rank.

        Args:
            rank (int): daos_engine rank. Defaults to None.

        Returns:
            ip_addr (str) : IPAddress for the rank.
            port_num (str) : Port number for the rank.
        """
        output = self.dmg_command.system_query()
        members_length = self.server_count * self.engine_count
        for index in range(0, members_length):
            if rank == int(output["response"]["members"][index]["rank"]):
                temp = output["response"]["members"][index]["addr"]
                ip_addr = temp.split(":")
                temp = output["response"]["members"][index]["fabric_uri"]
                port_num = temp.split(":")
                return ip_addr[0], port_num[2]
        return None, None

    @fail_on(CommandFailure)
    def remove_pool_dir(self, ip_addr=None, port_num=None):
        """Remove the /mnt/daos[x]/<pool_uuid>/vos-* directory.

        Args:
            ip_addr (str): IP address of the daos server. Defaults to None.
            port_number (str) : Port number the daos server.
        """
        # Create the expected port list
        # expected_ports = [port0] - Single engine/server
        # expected_ports = [port0, port1] - Two engine/server
        expected_ports = [engine_param.get_value("fabric_iface_port")
                          for engine_param in self.server_managers[-1].
                          manager.job.yaml.engine_params]
        self.log.info("Expected ports : %s", expected_ports)
        if ip_addr is None or port_num is None:
            self.log.info("ip_addr : %s port_number: %s", ip_addr, port_num)
            self.fail("No IP Address or Port number provided")
        else:
            if self.engine_count == 1:
                self.log.info("Single Engine per Server")
                cmd = "/usr/bin/ssh {} -oStrictHostKeyChecking=no \
                      sudo rm -rf /mnt/daos/{}/vos-*". \
                      format(ip_addr, self.pool.uuid)
            elif self.engine_count == 2:
                if port_num == str(expected_ports[0]):
                    port_val = 0
                elif port_num == str(expected_ports[1]):
                    port_val = 1
                else:
                    self.log.info("port_number: %s", port_num)
                    self.fail("Invalid port number")
                cmd = "/usr/bin/ssh {} -oStrictHostKeyChecking=no \
                      sudo rm -rf /mnt/daos{}/{}/vos-*". \
                      format(ip_addr, port_val, self.pool.uuid)
            else:
                self.fail("Not supported engine per server configuration")
            run_command(cmd)

    def set_container(self, container):
        """Set the OSA utils container object.

        Args:
            container (TestContainer): Container object to be used within OSA utils.
        """
        self.container = container

    def simple_osa_reintegrate_loop(self, rank, action="exclude", loop_time=100):
        """Exclude or drain and reintegrate a rank for a certain amount of time.

        Args:
            rank (int): daos server rank.
            action (str, optional): "exclude" or "drain". Defaults to "exclude"
            loop_time (int, optional): Total time to perform drain/reintegrate operation in a loop.
                Defaults to 100.
        """
        start_time = 0
        finish_time = 0
        start_time = time.time()
        while int(finish_time - start_time) < loop_time:
            if action == "exclude":
                output = self.pool.exclude(rank)
            else:
                output = self.pool.drain(rank)
            self.print_and_assert_on_rebuild_failure(output)
            output = self.pool.reintegrate(rank)
            self.print_and_assert_on_rebuild_failure(output)
            finish_time = time.time()

    @fail_on(DaosApiError)
    def write_single_object(self):
        """Write some data to the existing pool."""
        self.pool.connect(2)
        csum = self.params.get("enable_checksum", '/run/container/*')
        self.container = DaosContainer(self.context)
        input_param = self.container.cont_input_values
        input_param.enable_chksum = csum
        self.container.create(poh=self.pool.pool.handle, con_prop=input_param)
        self.container.open()
        self.obj = DaosObj(self.context, self.container)
        self.obj.create(objcls=1)
        self.obj.open()
        self.ioreq = IORequest(self.context, self.container, self.obj, objtype=4)
        self.log.info("Writing the Single Dataset")
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = str(akey)[0] * self.record_length
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
                indata = str(akey)[0] * self.record_length
                c_dkey = create_string_buffer("dkey {0}".format(dkey))
                c_akey = create_string_buffer("akey {0}".format(akey))
                val = self.ioreq.single_fetch(c_dkey, c_akey, len(indata) + 1)
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
        """Prepare the containers for IOR write and read invocations.

        To enable aggregation:
            - Create two containers and read always from first container
        Normal usage (use only a single container):
            - Create a single container and use the same.

        Args:
            oclass (str): IOR object class
            flags (str): IOR flags
        """
        self.log.info(self.pool_cont_dict)
        # If pool is not in the dictionary,
        # initialize its container list to None
        # {pool : [None, None], [None, None]}
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
                rf_value = "rd_fac:{}".format(tmp - 1)
                self.update_cont_properties(rf_value)
            self.container.create()
            self.pool_cont_dict[self.pool][0] = self.container
            self.pool_cont_dict[self.pool][1] = "Updated"
        else:
            if ((self.test_during_aggregation is True)
                    and (self.pool_cont_dict[self.pool][1] == "Updated")
                    and (self.pool_cont_dict[self.pool][3] is None)
                    and ("-w" in flags)):
                # Write to the second container
                self.add_container(self.pool, create=False)
                self.set_cont_class_properties(oclass)
                if self.test_with_checksum is False:
                    tmp = self.get_object_replica_value(oclass)
                    rf_value = "rd_fac:{}".format(tmp - 1)
                    self.update_cont_properties(rf_value)
                self.container.create()
                self.pool_cont_dict[self.pool][2] = self.container
                self.pool_cont_dict[self.pool][3] = "Updated"
            else:
                self.container = self.pool_cont_dict[self.pool][0]

    def delete_extra_container(self, pool):
        """Delete the extra container in the pool.

        Refer prepare_cont_ior_write_read. This method should be called when OSA tests intend to
        enable aggregation.

        Args:
            pool (TestPool): pool object
        """
        self.pool.set_property("reclaim", "time")
        extra_container = self.pool_cont_dict[pool][2]
        extra_container.destroy()
        self.pool_cont_dict[pool][3] = None

    def get_object_replica_value(self, oclass):
        """Get the object replica value for an object class.

        Args:
            oclass (str): Object Class (eg: RP_2G1,etc)

        Returns:
            int: Object replica value

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
            cont_prop (str): Replace existing container properties with new value
        """
        self.container.properties.value = cont_prop

    def set_cont_class_properties(self, oclass="S1"):
        """Update the container class to match the IOR/Mdtest object class.

        Fix the rf factor based on object replica value.
        Also, remove the redundancy factor for S type object class.

        Args:
            oclass (str, optional): Container object class to be set. Defaults to "S1".
        """
        self.container.oclass.value = oclass
        # Set the container properties properly for S!, S2 class.
        # rf should not be set to 1 for S type object class.
        match = re.search("^S\\d$", oclass)
        prop = self.container.properties.value
        if match is not None:
            prop = prop.replace("rd_fac:1", "rd_fac:0")
        else:
            tmp = self.get_object_replica_value(oclass)
            rf_value = "rd_fac:{}".format(tmp - 1)
            prop = prop.replace("rd_fac:1", rf_value)
        self.container.properties.value = prop
        # Over-write oclass settings if using redundancy factor
        # and self.test_with_rf is True.
        # This has to be done so that container created doesn't
        # use the object class.
        if self.test_with_rf is True and \
           "rf" in self.container.properties.value:
            self.log.info(
                "Detected container redundancy factor: %s",
                self.container.properties.value)
            self.ior_cmd.dfs_oclass.update(None, "ior.dfs_oclass")
            self.ior_cmd.dfs_dir_oclass.update(None, "ior.dfs_dir_oclass")
            self.container.oclass.update(None)

    def assert_on_exception(self, out_queue=None):
        """Assert on exception while executing an application.

        Args:
            out_queue (queue): Check whether the queue is empty. If empty, app (ior, mdtest) didn't
                encounter error.
        """
        if out_queue is None:
            out_queue = self.out_queue
        if out_queue.empty():
            pass
        else:
            exc = out_queue.get(block=False)
            out_queue.put(exc)
            raise CommandFailure(exc)

    def cleanup_queue(self, out_queue=None):
        """Cleanup the existing thread queue.

        Args:
            out_queue (queue): Queue to cleanup.
        """
        if out_queue is None:
            out_queue = self.out_queue
        while not out_queue.empty():
            out_queue.get(block=True)

    def run_ior_thread(self, action, oclass, test, single_cont_read=True, fail_on_warning=True,
                       pool=None):
        """Start the IOR thread for either writing or reading data to/from a container.

        Args:
            action (str): Start the IOR thread with Read or Write
            oclass (str): IOR object class
            test (list): IOR test sequence
            flags (str): IOR flags
            single_cont_read (bool, optional): Always read from the 1st container. Defaults to True.
            fail_on_warning (bool, optional): Test terminates for IOR warnings. Defaults to True.
            pool (TestPool, optional): Pool to run ior on. Defaults to None.

        """
        # Intermediate (between correct and hack) implementation for allowing a
        # pool to be passed in. Needs to be fixed by making the pool argument
        # required.
        if pool is None:
            pool = self.pool

        self.cleanup_queue()
        if action == "Write":
            flags = self.ior_w_flags
        else:
            flags = self.ior_r_flags

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.ior_thread,
                                   kwargs={"pool": pool,
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
        process.join()
        if fail_on_warning and not self.out_queue.empty():
            self.assert_on_exception()

    def ior_thread(self, pool, oclass, test, flags, single_cont_read=True, fail_on_warning=True):
        """Start an IOR thread.

        Args:
            pool (object): pool handle
            oclass (str): IOR object class, container class.
            test (list): IOR test sequence
            flags (str): IOR flags
            single_cont_read (bool, optional): Always read from the 1st container. Defaults to True.
            fail_on_warning (bool, optional): Test terminates for IOR warnings. Defaults to True.
        """
        self.cleanup_queue()
        self.pool = pool
        self.ior_cmd.get_params(self)
        self.ior_cmd.set_daos_params(self.server_group, self.pool, None)
        self.log.info("Redundancy Factor : %s", self.test_with_rf)
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
        # Update oclass settings if using redundancy factor
        # and self.test_with_rf is True.
        if self.test_with_rf is True and "rf" in self.container.properties.value:
            self.log.info(
                "Detected container redundancy factor: %s", self.container.properties.value)
            self.ior_cmd.dfs_oclass.update(None, "ior.dfs_oclass")
            self.ior_cmd.dfs_dir_oclass.update(None, "ior.dfs_dir_oclass")
        self.run_ior_with_pool(create_pool=False, create_cont=False,
                               fail_on_warning=fail_on_warning,
                               out_queue=self.out_queue)
        if fail_on_warning and not self.out_queue.empty():
            self.assert_on_exception()

    def run_mdtest_thread(self, oclass="RP_2G1"):
        """Start mdtest thread and wait until thread completes.

        Args:
            oclass (str): IOR object class, container class.
        """
        # Create container only
        self.mdtest_cmd.dfs_destroy = False
        create_container = 0
        if self.container is None:
            self.add_container(self.pool, create=False)
            create_container = 1
        self.mdtest_cmd.dfs_oclass.update(oclass)
        self.set_cont_class_properties(oclass)
        if self.test_with_checksum is False:
            tmp = self.get_object_replica_value(oclass)
            rf_value = "rd_fac:{}".format(tmp - 1)
            self.update_cont_properties(rf_value)
        if create_container == 1:
            self.container.create()
        job_manager = self.get_mdtest_job_manager_command(self.manager)
        job_manager.job.dfs_cont.update(self.container.uuid)
        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.execute_mdtest)
        # Launch the MDtest thread
        process.start()
        # Wait for the thread to finish
        process.join()
        if not self.out_queue.empty():
            self.assert_on_exception()
