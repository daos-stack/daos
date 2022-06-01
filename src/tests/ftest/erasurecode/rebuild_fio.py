#!/usr/bin/python3
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ec_utils import ErasureCodeFio, check_aggregation_status
from apricot import skipForTicket

class EcodFioRebuild(ErasureCodeFio):
    # pylint: disable=too-many-ancestors
    # pylint: disable=protected-access
    """Test class Description: Runs Fio with EC object type over POSIX and
        verify on-line, off-line for rebuild and verify the data.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a EcodFioRebuild object."""
        super().__init__(*args, **kwargs)
        self.set_online_rebuild = False
        self.rank_to_kill = None
        self.read_option = self.params.get("rw_read", "/run/fio/test/read_write/*")

    def execution(self, rebuild_mode):
        """
        Test execution

        Args:
            rebuild_mode: On-line or off-line rebuild mode
        """
        # Kill last server rank first
        self.rank_to_kill = self.server_count - 1

        if 'on-line' in rebuild_mode:
            # Enabled on-line rebuild for the test
            self.set_online_rebuild = True

        # Write the Fio data and kill server if rebuild_mode is on-line
        self.start_online_fio()

        # Verify Aggregation should start for Partial stripes IO
        if not any(check_aggregation_status(self.pool, attempt=60).values()):
            self.fail("Aggregation failed to start..")

        if 'off-line' in rebuild_mode:
            self.server_managers[0].stop_ranks(
                [self.server_count - 1], self.d_log, force=True)

        # Adding unlink option for final read command
        if int(self.container.properties.value.split(":")[1]) == 1:
            self.fio_cmd._jobs['test'].unlink.value = 1

        # Read and verify the original data.
        self.fio_cmd._jobs['test'].rw.value = self.read_option
        self.fio_cmd.run()

        # If RF is 2 kill one more server and validate the data is not corrupted.
        if int(self.container.properties.value.split(":")[1]) == 2:
            self.fio_cmd._jobs['test'].unlink.value = 1
            self.log.info("RF is 2,So kill another server and verify data")
            # Kill one more server rank
            self.server_managers[0].stop_ranks([self.server_count - 2],
                                               self.d_log, force=True)
            # Read and verify the original data.
            self.fio_cmd.run()

    def test_ec_online_rebuild_fio(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Verify the EC works for Fio during on-line rebuild.

        Use Cases:
            Create the container with RF:1 or 2.
            Create the Fio data file with verify pattern over Fuse.
            Kill the server when Write is in progress.
            Verify the Fio write finish without any error.
            Wait and verify Aggregation is getting triggered.
            Read and verify the data after Aggregation.
            Kill one more rank and verify the data after rebuild finish.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,fio,ec_online_rebuild
        :avocado: tags=ec_online_rebuild_fio
        """
        self.execution('on-line')

    @skipForTicket("DAOS-8640")
    def test_ec_offline_rebuild_fio(self):
        """Jira ID: DAOS-7320.

        Test Description:
            Verify the EC works for Fio, for off-line rebuild.

        Use Cases:
            Create the container with RF:1 or 2.
            Create the Fio data file with verify pattern over Fuse.
            Kill the server and wait for rebuild to finish.
            Wait and verify Aggregation is getting triggered.
            Kill one more rank and verify the data after rebuild finish.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,fio,ec_offline_rebuild
        :avocado: tags=ec_offline_rebuild_fio
        """
        self.execution('off-line')
