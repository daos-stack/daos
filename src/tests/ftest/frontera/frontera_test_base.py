#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase

class FronteraTestBase(IorTestBase, MdtestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Base Frontera performance class."""

    def print_performance_params(self, cmd):
        """Print performance parameters.

        Args:
            cmd (str): ior or mdtest

        """
        cmd = cmd.lower()

        self.log.info("PERFORMANCE PARAMS START")
        self.log.info("TEST_NAME   : {}".format(self.test_id))
        self.log.info("NUM_SERVERS : {}".format(len(self.hostlist_servers)))
        self.log.info("NUM_CLIENTS : {}".format(len(self.hostlist_clients)))
        self.log.info("PPC         : {}".format(self.processes / len(self.hostlist_clients)))
        self.log.info("PPN         : {}".format(self.processes / len(self.hostlist_clients)))
        
        if cmd == "ior":
            self.log.info("OCLASS      : {}".format(self.ior_cmd.dfs_oclass.value))
            self.log.info("XFER_SIZE   : {}".format(self.ior_cmd.transfer_size.value))
            self.log.info("BLOCK_SIZE  : {}".format(self.ior_cmd.block_size.value))
            self.log.info("SW_TIME     : {}".format(self.ior_cmd.sw_deadline.value))
            self.log.info("CHUNK_SIZE  : {}".format(self.ior_cmd.dfs_chunk.value))
        elif cmd == "mdtest":
            self.log.info("OCLASS      : {}".format(self.mdtest_cmd.dfs_oclass.value))
            self.log.info("DIR_OCLASS  : {}".format(self.mdtest_cmd.dfs_dir_oclass.value))
            self.log.info("SW_TIME     : {}".format(self.mdtest_cmd.stonewall_timer.value))
            self.log.info("CHUNK_SIZE  : {}".format(self.mdtest_cmd.dfs_chunk.value))
        else:
            self.fail("Invalid cmd: {}".format(cmd))

        self.log.info("PERFORMANCE PARAMS END")

    def print_system_status(self):
        """TODO"""
        pass

    def run_performance_ior(self, write_flags=None, read_flags=None):
        """Run an IOR performance test.

        Args:
            write_flags (str, optional): IOR flags for write phase.
                Defaults to ior/write_flags in the config.
            read_flags (str, optional): IOR flags for read phase.
                Defaults to ior/read_flags in the config.

        """
        if write_flags is None:
            write_flags = self.params.get("write_flags", "/run/ior/*")
        if read_flags is None:
            read_flags = self.params.get("read_flags", "/run/ior/*")

        self.print_params("ior")

        # Run IOR write
        self.ior_cmd.flags.update(write_flags)
        self.run_ior_with_pool()

        # Run IOR read
        self.ior_cmd.flags.update(read_flags)
        self.ior_cmd.sw_wearout.update(None)
        self.ior_cmd.sw_deadline.update(None)
        self.run_ior_with_pool(create_cont=False)

    def run_performance_mdtest(self, flags=None):
        """Run an MdTest performance test.

        Args:
            flags (str, optional): MdTest flags.
                Defaults to mdtest/flags in the config.

        """
        if flags is None:
            flags = self.params.get("flags", "/run/mdtest/*")
        self.mdtest_cmd.flags.update(flags)
        self.print_params()
        self.execute_mdtest()