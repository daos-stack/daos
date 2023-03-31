"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from multiprocessing import Queue
import threading

from avocado.core.exceptions import TestFail
from dmg_utils import get_storage_query_device_info, get_dmg_response
from exception_utils import CommandFailure
from ior_utils import run_ior, thread_run_ior
from job_manager_utils import get_job_manager
from led import VmdLedStatus
from nvme_utils import set_device_faulty


class NvmeFaultReintegrate(VmdLedStatus):
    """
    Test Class Description: This test runs
    NVME fault and reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.daos_command = self.get_daos_command()

    def verify_dev_led_state(self, device, dev_state="NORMAL", led_state="OFF"):
        """Verify device dev_state and led_state.

        Args:
            device (str): the device description.
            dev_state (str): expect dev_state, default to "NORMAL".
            led_state (str): expect led_state, default to "OFF".

        Returns:
            bool: True if the expected dev_state and led_state.

        """
        return self.check_result(self.get_led_status_value(device), dev_state, led_state)

    def check_result(self, result, dev_state, led_state):
        """Check for result of storage device and led states.

        Args:
            result (dict): return from get_led_status_value for a storage device.
            dev_state (str): expect dev_state.
            led_state (str): expect led_state.

        Return:
            True if the expected dev_state and led_state.

        """
        if "response" in result.keys():
            for value in list(result['response']['host_storage_map'].values()):
                if value['storage']['smd_info']['devices']:
                    for device in value['storage']['smd_info']['devices']:
                        if device['dev_state'] == dev_state and device['led_state'] == led_state:
                            return True
        elif "host_storage_map" in result.keys():
            for value in list(result['host_storage_map'].values()):
                if value['storage']['smd_info']['devices']:
                    for device in value['storage']['smd_info']['devices']:
                        if device['dev_state'] == dev_state and device['led_state'] == led_state:
                            return True
        else:
            self.log.debug(
                "Neither 'response' nor 'host_storage_map' found in dmg command json output:")
            self.log.debug('  %s', result)
        return False

    def reset_fault_device(self, device):
        """Call dmg storage led identify to reset the device.

        Args:
            device (str): device to reset

        Returns:
            list: a list of any errors detected when removing the pool
        """
        error_list = []
        try:
            get_dmg_response(self, self.dmg.storage_led_identify, reset=True, ids=device)
        except TestFail as error:
            self.log.info("#  %s", error)
            error_list.append("Error resetting device {}: {}".format(device, error))
        return error_list

    def run_ior_with_nvme_fault(self, oclass=None):
        """Perform ior write with nvme fault testing.

        Test steps:
            1. Start the DAOS servers.
            2. Verify the drive status LED.
            3. Create the pool and container with RF.
            4. While IOR (EC+GX) in progress put single drive to fault mode.
            5. IOR will halt until rebuild is finished.
            6. IOR should complete without any error.
            7. Drive status LED should be solid Amber ON indicating it’s faulty.
            8. Verify IOR data and check container.
            9. Replace the same drive back.
            10. Drive status LED should be off indicating good device is plugged-in.
        Args:
            oclass (str, optional): object class (eg: RP_2G8, S1,etc). Defaults to None
        """
        # 1.
        self.log_step("Bring up server and reset led of all devices.")
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        device_info = get_storage_query_device_info(self, self.dmg)
        devices = [device['uuid'] for device in device_info]
        self.log.info("Device information")
        self.log.info("==================")
        for index, entry in enumerate(device_info):
            self.log.info("Device %s:", index)
            for key in sorted(entry):
                self.log.info("  %s: %s", key, entry[key])
        test_dev = devices[0]
        for device in devices:
            get_dmg_response(self, self.dmg.storage_led_identify, reset=True, ids=device)
        # 2.
        self.log_step("Verify that each device is in a 'NORMAL' state and its LED is 'OFF'")
        err_dev = []
        for device in devices:
            if not self.verify_dev_led_state(device, 'NORMAL', 'OFF'):
                err_dev.append(device)
        if err_dev:
            self.fail("#Device {} not in expected NORMAL, OFF state".format(err_dev))

        # 3.
        self.log_step("Creating a pool and container with RF and starting IOR in a thread")
        self.add_pool(connect=False)
        self.add_container(self.pool)

        job_manager = get_job_manager(self, subprocess=None, timeout=120)
        thread_queue = Queue()

        ior_test_seq = self.params.get("ior_test_sequence", "/run/ior/iorflags/*")[0]
        thread = []
        kwargs = {"thread_queue": thread_queue, "job_id": 1}
        ior_kwargs = {
            "test": self,
            "manager": job_manager,
            "log": "ior_thread_write_pool_test.log",
            "hosts": self.hostlist_clients,
            "path": self.workdir,
            "slots": None,
            "group": self.server_group,
            "pool": self.pool,
            "container": self.container,
            "processes": self.params.get("np", "/run/ior/client_processes/*"),
            "ppn": self.params.get("ppn", "/run/ior/client_processes/*"),
            "intercept": None,
            "plugin_path": None,
            "dfuse": None,
            "display_space": True,
            "fail_on_warning": False,
            "namespace": "/run/ior/*",
            "ior_params": {
                "oclass": oclass,
                "flags": self.ior_w_flags,
                "transfer_size": ior_test_seq[0],
                "block_size": ior_test_seq[1]
            }
        }
        kwargs.update(ior_kwargs)
        thread.append(threading.Thread(target=thread_run_ior, kwargs=kwargs))
        # Launch the IOR thread
        self.log.info("Start IOR thread : %s", thread)
        thread[0].start()

        # 4.
        self.log_step(
            "Marking the {} device as faulty and verifying it is 'EVICTED' and its "
            "LED is 'ON'".format(test_dev))
        check = self.check_result(
            set_device_faulty(self, self.dmg, self.hostlist_servers[0], test_dev, self.pool),
            "EVICTED", "ON")

        if not check:
            self.fail("#Result of set_device_fault, device not in expected EVICTED, ON state")

        # check device state after set nvme-faulty
        if not self.verify_dev_led_state(test_dev, "NORMAL", "ON"):
            self.fail("#After set_device_fault, device not back to NORMAL, ON state")

        # 5.
        self.log_step("Waiting for IOR to complete")

        # 6.
        thread[0].join()
        self.log_step("IOR completed and check for error")
        errors = 0
        while not thread_queue.empty():
            result = thread_queue.get()
            self.log.debug("Results from thread %s (log %s)", result["job_id"],
                           result["log"])
            for name in ("command", "exit_status", "interrupted", "duration"):
                self.log.debug("  %s: %s", name, getattr(result["result"], name))
            for name in ("stdout", "stderr"):
                self.log.debug("  %s:", name)
                for line in getattr(result["result"], name).splitlines():
                    self.log.debug("    %s:", line)
            if result["result"].exit_status != 0:
                errors += 1
        if errors:
            self.fail("Errors running IOR {} thread".format(errors))

        # 7.
        self.log_step("Check drive status as 'NORMAL' and led status is 'ON'")
        if not self.verify_dev_led_state(test_dev, "NORMAL", "ON"):
            self.fail(
                "#After set_device_fault, IOR completed, device not in expected NORMAL, ON state")

        # 8. Verify IOR data and check container
        self.log_step("Verify IOR data and check container")
        ior_kwargs["ior_params"]["flags"] = self.ior_r_flags
        ior_kwargs["log"] = "ior_read_pool_test.log"
        try:
            run_ior(**ior_kwargs)
        except CommandFailure as error:
            self.fail("Error in ior read {}.".format(error))
        kwargs = {"pool": self.pool.identifier,
                  "cont": self.container.identifier}
        output = self.daos_command.container_check(**kwargs)
        self.log.info(output)

        # 9.
        self.log_step("Replace the same drive back.")
        result = self.dmg.storage_replace_nvme(old_uuid=test_dev, new_uuid=test_dev)
        # Wait for rebuild to start
        self.pool.wait_for_rebuild_to_start()
        # Wait for rebuild to complete
        self.pool.wait_for_rebuild_to_end()
        # check device state after set nvme-faulty

        # 10.
        self.log_step("Drive status LED should be off indicating good device is plugged-in.")
        if not self.check_result(result, "NORMAL", "OFF"):
            self.register_cleanup(self.reset_fault_device, device=test_dev)
            self.fail("#After storage replace nvme, device not in expected NORMAL, OFF state")
        self.log.info("Test passed")

    def test_nvme_fault_reintegration(self):
        """Test ID: DAOS-10034.

        Test Description:
            This method is called from the avocado test infrastructure. This method
                invokes NVME set fault, reintegration and led verification test.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led,VmdLedStatus
        :avocado: tags=NvmeFaultReintegrate,test_nvme_fault_reintegration
        """
        self.run_ior_with_nvme_fault()
