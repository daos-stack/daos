"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import time

from avocado import fail_on
from dmg_utils import get_dmg_response, get_storage_query_device_info
from exception_utils import CommandFailure
from general_utils import list_to_str
from nvme_utils import set_device_faulty
from osa_utils import OSAUtils
from test_utils_pool import add_pool


class DiskFailureTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    # pylint: disable=attribute-defined-outside-init
    # pylint: disable=invalid-name
    """Test class Description: Verify disk failure is properly handled.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.targets = self.params.get("targets", "/run/server_config/servers/0/*")
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/*')

    def test_disk_failure_w_rf(self):
        """Jira ID: DAOS-11284.

        Test disk failures during the IO operation.

        :avocado: tags=all,fuill_regression
        :avocado: tags=hw,hw_vmd,medium
        :avocado: tags=deployment,disk_failure
        :avocado: tags=DiskFailureTest,test_disk_failure_w_rf
        """
        num_pools = self.params.get("num_pool", "/run/test_options/*", 1)
        pool = {}

        # Get the device information.
        self.log_step("Getting device information")
        device_info = get_storage_query_device_info(self.dmg_command)
        self.log.info("Device information")
        self.log.info("------------------")
        for index, entry in enumerate(device_info):
            self.log.info("Device %s:", index)
            for key in sorted(entry):
                self.log.info("  %s: %s", key, entry[key])

        for val in range(0, num_pools):
            self.log_step(f"Loop {val + 1}/{num_pools}: Starting loop / Creating pool")
            pool[val] = add_pool(self, connect=False)
            threads = []
            self.pool = pool[val]
            # The following thread runs while raising disk faults
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": self.ior_cmd.dfs_oclass.value,
                                                    "test": self.ior_test_sequence[0],
                                                    "fail_on_warning": False}))
            # Launch the IOR threads
            self.log_step(f"Loop {val + 1}/{num_pools}: Starting ior thread")
            for thread in threads:
                self.log.info("Thread : %s", thread)
                thread.start()
                time.sleep(5)

            # Evict a random target from the system
            evict_device = self.random.choice(device_info)
            self.log_step(
                f"Loop {val + 1}/{num_pools}: Evicting random target {evict_device['uuid']}")
            try:
                set_device_faulty(self, self.dmg_command, evict_device["hosts"].split(":")[0],
                                  evict_device["uuid"], None, evict_device["has_sys_xs"])

                # get_dmg_response(self.dmg_command.storage_set_faulty,
                #                     host=evict_device["hosts"].split(":")[0],
                #                     uuid=evict_device["uuid"])
            except CommandFailure:
                self.fail(f"Error evicting target {evict_device['uuid']}")

            self.log_step(f"Loop {val + 1}/{num_pools}: Waiting for rebuild to complete")
            done = "Completed setting all devices to fault"
            self.print_and_assert_on_rebuild_failure(done)

            self.log_step(f"Loop {val + 1}/{num_pools}: Waiting for ior thread to complete")
            for thread in threads:
                thread.join()

            if not evict_device["has_sys_xs"]:
                # Now replace the faulty NVME device.
                self.log_step(
                    f"Loop {val + 1}/{num_pools}: Replacing evicted target {evict_device['uuid']}")
                try:
                    get_dmg_response(
                        self.dmg_command.storage_replace_nvme,
                        host=evict_device["hosts"].split(":")[0],
                        old_uuid=evict_device["uuid"],
                        new_uuid=evict_device["uuid"])
                except CommandFailure as error:
                    self.fail(str(error))
                time.sleep(10)
                self.log_step(
                    f"Loop {val + 1}/{num_pools}: Reintegrating evicted target: {evict_device}")
                self.pool.reintegrate(evict_device["rank"], list_to_str(evict_device["tgt_ids"]))
                time.sleep(15)

                self.log_step(f"Loop {val + 1}/{num_pools}: Waiting for rebuild to complete")
                done = "Faulty NVMEs replaced"
                self.print_and_assert_on_rebuild_failure(done)
                self.log.info("Loop %s/%s: Rebuild completed / Loop done", val + 1, num_pools)

        # After completing the test, check for container integrity
        self.log_step("Checking pool space and container integrity")
        for val in range(0, num_pools):
            display_string = f"Pool{val} space at the End"
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            self.run_ior_thread("Read", oclass=self.ior_cmd.dfs_oclass.value,
                                test=self.ior_test_sequence[0])
            self.container = self.pool_cont_dict[self.pool][0]
            self.container.check()

        self.log.info("Test passed")

    @fail_on(CommandFailure)
    def test_disk_fault_to_normal(self):
        """Jira ID: DAOS-11284
        Test a disk inducing faults and resetting is back to normal state.

        :avocado: tags=all,manual
        :avocado: tags=deployment,disk_failure
        :avocado: tags=DiskFailureTest,test_disk_fault_to_normal
        """
        device_info = get_storage_query_device_info(self.dmg_command)
        for index, device in enumerate(device_info):
            host = device["hosts"].split(":")[0]
            self.log.info("Device %s on host %s:", index, host)
            for key in sorted(device):
                self.log.info("  %s: %s", key, device[key])
            try:
                # Set the device as faulty
                get_dmg_response(self.dmg_command.storage_set_faulty, host=host,
                                 uuid=device["uuid"])
                # Replace the device with same uuid.
                passed = False
                for _ in range(10):
                    data = self.dmg_command.storage_replace_nvme(host=host,
                                                                 old_uuid=device["uuid"],
                                                                 new_uuid=device["uuid"])
                    if not data['error'] and len(data['response']['host_errors']) == 0:
                        passed = True
                        break
                    time.sleep(5)
                if not passed:
                    self.fail('Replacing faulty device did not pass after 10 retries')
            except CommandFailure as error:
                self.fail(str(error))
