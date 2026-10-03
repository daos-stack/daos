"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from general_utils import journalctl_time
from test_utils_pool import get_size_params


class DmgSystemReformatTest(TestWithServers):
    """Test Class Description:

    Test to verify dmg system erase and storage format reformat work as expected on a
    DAOS system after a controlled shutdown. Exercised across PMem and MD-on-SSD tier-0
    storage configurations (selected via launch.py --nvme) and single/multiple
    management service (MS) replica configurations (selected via the
    mgmt_svc_replicas_qty test yaml setup parameter).

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()
        super().setUp()

    def get_replicas(self, dmg):
        """Get the current set of management service (MS) replica ranks.

        Args:
            dmg (DmgCommand): dmg command object to use to query the system.

        Returns:
            list: sorted list of MS replica ranks reported by the leader.

        """
        leader_info = dmg.system_leader_query()
        return sorted(leader_info["response"]["replicas"])

    def test_dmg_system_reformat(self):
        """
        JIRA ID: DAOS-5415

        Test Description: Test dmg system erase/reformat functionality across both
        PMem and MD-on-SSD storage tier-0 configurations as well as single and
        multiple management service (MS) replica configurations. The storage mode
        (PMem vs. MD-on-SSD) is selected at launch time (e.g. launch.py --nvme=auto
        vs. --nvme=auto_md_on_ssd); the MS replica count is varied via the
        mgmt_svc_replicas_qty test yaml setup parameter.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,dmg,system_reformat,system_erase
        :avocado: tags=DmgSystemReformatTest,test_dmg_system_reformat
        """
        dmg = self.get_dmg_command().copy()

        self.log.info(
            "Testing with %s configured MS replica(s): %s",
            len(self.mgmt_svc_replicas), self.mgmt_svc_replicas)
        pre_erase_replicas = self.get_replicas(dmg)
        self.log.info("MS replica ranks before erase: %s", pre_erase_replicas)

        # Create pool using 90% of the available NVMe capacity
        pools = [self.get_pool(dmg=dmg)]

        self.log.info("Check that new pool will fail with DER_NOSPACE")
        dmg.exit_status_exception = False
        pools.append(self.get_pool(create=False, **get_size_params(pools[0])))
        try:
            pools[-1].create()
        except TestFail as error:
            self.log.info("Pool create failed: %s", str(error))
            if "-1007" not in str(error):
                self.fail("Pool create did not fail due to DER_NOSPACE!")
        dmg.exit_status_exception = True

        self.log.info("Stop running engine instances: 'dmg system stop'")
        dmg.system_stop()
        if dmg.result.exit_status != 0:
            self.fail("Detected issues performing a system stop: {}".format(
                dmg.result.stderr_text))

        # Remove pools and disable removing pools that about to be removed by formatting
        for pool in pools:
            pool.skip_cleanup()
        pools = []

        # Perform a dmg system erase to allow the dmg storage format to succeed
        self.log.info("Perform dmg system erase on all system ranks:")
        dmg.system_erase()
        if dmg.result.exit_status != 0:
            self.fail("Issues performing system erase: {}".format(
                dmg.result.stderr_text))

        self.log.info("Perform dmg storage format on all system ranks:")

        # Calling storage format after system stop too soon would fail, so
        # wait 10 sec and retry up to 4 times.
        count = 0
        while count < 4:
            try:
                dmg.storage_format()
                if dmg.result.exit_status != 0:
                    self.fail(
                        "Issues performing storage format: {}".format(
                            dmg.result.stderr_text))
                break
            except CommandFailure as error:
                self.log.info("Storage format failed. Wait 10 sec and retry. %s", error)
                count += 1
                time.sleep(10)

        # Check that engine starts up again on every host, not just the last one, since
        # a multi-replica MS configuration spans more than one host.
        self.log.info("<SERVER> Waiting for the engines to start")
        start_time = journalctl_time()
        for server_manager in self.server_managers:
            server_manager.manager.timestamps["start"] = start_time
            server_manager.detect_engine_start()

        # Verify the MS replica set survived the erase/reformat cycle intact - this is the
        # critical guarantee relied upon by the raft database erase-then-restart logic.
        post_erase_replicas = self.get_replicas(dmg)
        self.log.info("MS replica ranks after erase/reformat: %s", post_erase_replicas)
        self.assertEqual(
            pre_erase_replicas, post_erase_replicas,
            "MS replica ranks changed as a result of system erase/reformat")

        # Check that we have cleared storage by checking pool list
        if dmg.get_pool_list_uuids():
            self.fail("Detected pools in storage after reformat: {}".format(
                dmg.result.stdout_text))

        # Create last pool now that memory has been wiped.
        pools.append(self.get_pool(connect=False, dmg=dmg))

        # Lastly, verify that last created pool is in the list
        pool_uuids = dmg.get_pool_list_uuids()
        self.assertEqual(
            pool_uuids[0].lower(), pools[-1].uuid.lower(), "{} missing from list".format(pools[-1]))
