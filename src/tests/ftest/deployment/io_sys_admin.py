#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
import os

from small import IorSmall
from large_file_count import LargeFileCount
from mdtest.small import MdtestSmall
from dmg_nvme_scan_test import DmgNvmeScanTest
from dmg_storage_scan_scm import DmgStorageScanSCMTest
from config_generate_output import ConfigGenerateOutput
from daos_control_config import DaosControlConfigTest
from autotest import ContainerAutotestTest
from ior_smoke import EcodIor
from large_file import DmvrPosixLargeFile
from general_utils import human_to_bytes
from data_mover_test_base import DataMoverTestBase
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase

class IoSysAdmin(LargeFileCount, DataMoverTestBase, DmgNvmeScanTest, DmgStorageScanSCMTest, ConfigGenerateOutput):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class wrapping up tests from four
                               different test classes into one. Intent
                               is to run these tests as a basic checkout
                               for newly installed servers.
    :avocado: recursive
    """

    def get_free_space(self):
        """Display pool free space."""
        free_space = []
        scm_free_space = self.pool.get_pool_free_space("scm")
        free_space.append(scm_free_space)
        nvme_free_space = self.pool.get_pool_free_space("nvme")
        free_space.append(nvme_free_space)
        self.log.info("Free space [SCM, Nvme]: [%s, %s]", scm_free_space, nvme_free_space)

        return free_space

    def run_dm_large_file_dep(self, tool):
        """
        Test Description:
            Copy a very large file between daos POSIX containers and
            an external POSIX file system.
        Use Cases:
            Create a pool
            Create POSIX type cont1.
            Run ior -a DFS on cont1.
            Create POSIX type cont2.
            Copy data from cont1 to cont2.
            Copy data from cont2 to external POSIX file system.
            Create POSIX type cont4.
            Copy data from external POSIX file system to cont4.
            Run ior -a DFS with read verify on copied directory to verify
            data in cont3.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the number of ior processes
#        self.ior_processes = self.params.get(
#            self.tool.lower(), "/run/ior/client_processes/*")
#        if not self.ior_processes:
#            self.fail("Failed to get ior processes for {}".format(self.tool))

        # create pool and cont
#        self.create_pool()
#        self.create_cont(self.pool[0])
        cont1 = self.container[-1]

        # update and run ior on cont1
#        self.run_ior_with_params(
#            "DAOS", self.ior_cmd.test_file.value,
#            self.pool[0], self.container[0])

        # create cont2
        self.container.append(self.add_containers(self.ior_cmd.dfs_oclass.value))
        cont2 = self.container[-1]
#        self.create_cont(self.pool[0])

        if tool == 'CONT_CLONE':
            # Clone cont1 to cont2
#            self.start_dfuse(self.dfuse_hosts, self.pool, cont1, mount_dir='/tmp/dfuse_cont1')
#            command = 'ls -l /tmp/dfuse_cont1/*'
#            self.execute_cmd(command)
            
            read_back_cont = self.gen_uuid()
            self.run_datamover(
                self.test_id + " (cont1 to cont2)",
                "DAOS", None, self.pool, cont1,
                "DAOS", None, self.pool, read_back_cont)
            read_back_pool = self.pool

#            self.start_dfuse(self.dfuse_hosts, self.pool, read_back_cont, mount_dir='/tmp/dfuse_cont2')
#            command = 'ls -l /tmp/dfuse_cont2/*'
#            self.execute_cmd(command)
        elif tool == 'DSERIAL':
            # Create pool2
            pool2 = self.get_pool()
            # Use dfuse as a shared intermediate for serialize + deserialize
            dfuse_cont = self.add_containers(self.ior_cmd.dfs_oclass.value)
            self.start_dfuse(self.dfuse_hosts, self.pool, dfuse_cont)
            self.serial_tmp_dir = self.dfuse.mount_dir.value
            
            # Serialize/Deserialize cont1 to a new cont2 in pool2
            result = self.run_datamover(
                self.test_id + " (cont1->HDF5->cont2)",
                "DAOS_UUID", None, self.pool, cont1,
                "DAOS_UUID", None, pool2, None)

            # Get the destination cont2 uuid
            read_back_cont = self.parse_create_cont_uuid(result.stdout_text)
            read_back_pool = pool2
        else:
            # copy from daos cont1 to cont2
            self.run_datamover(
                self.test_id + " (cont1 to cont2)",
                "DAOS", "/", self.pool, cont1,
                "DAOS", "/", self.pool, cont2)

        
        if tool in ['FS_COPY', 'DCP']:    
            posix_path = self.new_posix_test_path(shared=True)

            # copy from daos cont2 to posix file system
            self.run_datamover(
                self.test_id + " (cont2 to posix)",
                "DAOS", "/", self.pool, cont2,
                "POSIX", posix_path)

            # create cont3
            self.container.append(self.add_containers(self.ior_cmd.dfs_oclass.value))
            cont3 = self.container[-1]
#        self.create_cont(self.pool[0])

            # copy from posix file system to daos cont3
            self.run_datamover(
                self.test_id + " (posix to cont3)",
                "POSIX", posix_path, None, None,
                "DAOS", "/", self.pool, cont3)
            read_back_cont = cont3
            read_back_pool = self.pool
        # the result is that a NEW directory is created in the destination
#        print("os.path.basename(posix_path): {}".format(os.path.basename(posix_path)))
        print("self.ior_cmd.test_file.value: {}".format(self.ior_cmd.test_file.value))
        if tool == 'FS_COPY':
            daos_path = "/" + os.path.basename(posix_path) + self.ior_cmd.test_file.value.split(":")[1]
#        elif tool == 'CONT_CLONE':
#            daos_path = self.ior_cmd.test_file.value.split(":")[1]
        else:
            daos_path = self.ior_cmd.test_file.value
        # update ior params, read back and verify data from cont3
#        self.run_ior_with_pool(create_pool=False, create_cont=False)
        self.run_ior_with_params(
            "DAOS", daos_path, read_back_pool, read_back_cont,
            flags="-r -R -F -k")

    def test_io_sys_admin(self):
        """
        Test Description: Bundles four tests into one and run in the
                          following sequence - ior_small, mdtest_small,
                          ec_smoke and autotest.
        :avocado: tags=hw,large
        :avocado: tags=deployment,iosysadmin
        """
        # local param
        ec_ior_flags = self.params.get("ec_ior_flags", "/run/ior/*")
        hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/*')

        # run tests
        self.test_dmg_nvme_scan_basic()
#        self.test_dmg_storage_scan_scm()

#        self.test_basic_config()
#        self.test_access_points_single()
#        self.test_access_points_odd()
#        self.test_access_points_invalid()
#        self.test_access_points_same_ap_repeated()
#        self.test_num_engines()
#        self.test_min_ssds()
#        self.test_net_class()

        self.test_largefilecount()
        # create snapshot
        self.container[-1].create_snap()        
        # overwrite the last ior file
        self.ior_cmd.signature.update('456')
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        nvme_free_space_before_snap_destroy = self.get_free_space()[1]
        # delete snapshot
        self.container[-1].destroy_snap(epc=self.container[-1].epoch)
        # Now check if the space is returned back.
        counter = 1
        returned_space = (self.get_free_space()[1] -
                          nvme_free_space_before_snap_destroy)

        data_written = (int(self.processes) * human_to_bytes(self.ior_cmd.block_size.value))
        while returned_space < int(data_written):
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space before snapshot destroy: %s",
                              nvme_free_space_before_snap_destroy)
                self.log.info("Free space when test terminated: %s",
                              self.get_free_space()[1])
                self.fail("Aggregation did not complete as expected")

            time.sleep(60)
            returned_space = (self.get_free_space()[1] -
                              nvme_free_space_before_snap_destroy)
            counter += 1

        self.run_dm_large_file_dep("FS_COPY")
        self.run_dm_large_file_dep("DCP")
#        self.run_dm_large_file_dep("CONT_CLONE")
        self.run_dm_large_file_dep("DSERIAL")

#        self.test_dm_large_file_dcp()
#        self.test_dm_large_file_fs_copy()
#        self.test_ior_small()
#        self.test_mdtest_small()

#        self.ior_cmd.flags.update(ec_ior_flags)
#        self.test_ec()

#        self.container.destroy()
#        self.pool.destroy()

#        self.test_container_autotest()


class IoSysAdminDm(DmvrPosixLargeFile):
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class to wrap datamover test to
                               run as part of basic checkout and verify
                               connectivity for lustre FS
    :avocado: recursive
    """

    def test_io_sys_admin_dm(self):
        """
        Test Description: Datamover test to check connection and datamover
                          functionality with Lustre fs on newly installed
                          server nodes.
        :avocado: tags=hw,large
        :avocado: tags=deployment
        """
        # local param
        dm_ior_options = self.params.get("dm_ior_options", "/run/ior/dm/*")

        # update ior params and run dm test
        self.ior_cmd.flags.update(dm_ior_options[0])
        self.ior_cmd.signature.update(dm_ior_options[1])
        self.ior_cmd.transfer_size.update(dm_ior_options[2])
        self.ior_cmd.block_size.update(dm_ior_options[3])
        self.ior_cmd.dfs_dir_oclass.update(dm_ior_options[4])
        self.ior_cmd.dfs_oclass.update(dm_ior_options[5])
        self.ior_cmd.test_file.update(dm_ior_options[6])
        self.ior_cmd.repetitions.update(dm_ior_options[7])
        self.test_dm_large_file_fs_copy()
