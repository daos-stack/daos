#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time
import ctypes

from apricot import TestWithServers
from pydaos.raw import DaosContainer, IORequest, DaosObjClass
from general_utils import create_string_buffer
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from command_utils_base import CommandFailure


class CatastrophicRecoveryDebugger(IorTestBase):
    """
    :avocado: recursive
    """

    def insert_objects(self):
        """Insert objects, dkeys, akeys, and data into the given container.
        """
        container = DaosContainer(self.context)
        container.create(self.pool.pool.handle)
        container.open()

        ioreqs = []
        dkeys_a = []
        dkeys_b = []

        for i in range(5):
            ioreqs.append(IORequest(
                context=self.context, container=container, obj=None,
                objtype=DaosObjClass.OC_S1))

            # Prepare 2 dkeys and 1 akey in each dkey. Use the same akey for both dkeys.
            dkey_str_a = b"Sample dkey A %d" % i
            dkey_str_b = b"Sample dkey B %d" % i
            akey_str = b"Sample akey %d" % i
            data_str = b"Sample data %d" % i
            data = create_string_buffer(data_str)

            # Pass in length of the key so that it won't have \0 termination.
            # Not necessary here because we're not interested in the list
            # output. Just for debugging.
            dkeys_a.append(create_string_buffer(value=dkey_str_a, size=len(dkey_str_a)))
            dkeys_b.append(create_string_buffer(value=dkey_str_b, size=len(dkey_str_b)))
            akey = create_string_buffer(value=akey_str, size=len(akey_str))
            c_size = ctypes.c_size_t(ctypes.sizeof(data))

            # Insert the dkeys.
            ioreqs[-1].single_insert(
                dkey=dkeys_a[-1], akey=akey, value=data, size=c_size)
            ioreqs[-1].single_insert(
                dkey=dkeys_b[-1], akey=akey, value=data, size=c_size)

    def run_debug_ior(self, file_name, pool, namespace):
        """Run IOR command and store the results to the results dictionary.

        Create a new IorCommand object instead of using the one in IorTestBase because
        we'll run a test that runs multiple IOR processes at the same time.

        Args:
            file_name (str): File name used for self.ior_cmd.test_file.
            pool (TestPool): Pool to run IOR.
            namespace (str): IOR namespace defined in the test yaml.
        """
        self.add_container(pool=self.pool, namespace="/run/container_wo_rf/*")

        # Update the object class depending on the test case.
        ior_cmd = IorCommand(namespace=namespace)
        ior_cmd.get_params(self)

        # Standard IOR prep sequence.
        ior_cmd.set_daos_params(self.server_group, pool, self.container.uuid)
        testfile = os.path.join("/", file_name)
        ior_cmd.test_file.update(testfile)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess,
            mpi_type="mpich")
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        manager.ppn.update(ppn, 'mpirun.ppn')
        manager.processes.update(None, 'mpirun.np')

        # Run the command.
        try:
            self.log.info("--- IOR command start ---")
            manager.run()
            self.log.info("--- IOR command end ---")
        except CommandFailure:
            self.log.info("--- IOR command failed ---")

    def test_cat_rec_debug(self):
        """
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=cat_rec
        :avocado: tags=cat_rec_debug
        """
        self.add_pool()

        # Insert objects with API.
        # self.insert_objects(container=container)

        # Insert objects with IOR.
        self.run_debug_ior(
            file_name="test_file_1", pool=self.pool, namespace="/run/ior/*")

        file_name = "signal.txt"
        file = open(file_name, "w")
        file.write("Makito")
        file.close()

        self.log.debug("## Stop daos_server to test ddb.")

        count = 0

        while os.path.exists(file_name):
            self.log.debug("## Sleep until %s is deleted. - %d", file_name, count)
            time.sleep(10)
            count += 1

        self.log.debug("## %s is deleted.", file_name)
