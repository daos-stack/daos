"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from multiprocessing import Queue
import time
import threading

from dmg_utils import get_storage_query_device_info
from exception_utils import CommandFailure
from ior_utils import run_ior, thread_run_ior
from job_manager_utils import get_job_manager
from led import VmdLedStatus
from test_utils_pool import add_pool
from write_host_file import write_host_file

class NvmeFaultReintegrate(VmdLedStatus):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME fault and reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.daos_command = self.get_daos_command()
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.dmg_command.exit_status_exception = True

    def verify_dev_led_state(self, device, dev_state="NORMAL", led_state="OFF"):
        """Verify device dev_state and led_state.

        Args:
            device (str): the device description.
            dev_state (str): expect dev_state, default to "NORMAL".
            led_state (str): expect led_state, default to "OFF".

        Return:
            True if the expected dev_state and led_state.

        """
        return self.check_result(self.get_led_status_value(device), dev_state, led_state)

    def check_result(self, result, dev_state, led_state):
        """Check for result of storage device and led states.

        Args:
            result (dict): return from get_led_status_value for a storage device.
            dev_state (str): expect dev_state, default to "NORMAL".
            led_state (str): expect led_state, default to "OFF".

        Return:
            True if the expected dev_state and led_state.

        """
        status = False
        if "response" in result.keys():
            for value in list(result['response']['host_storage_map'].values()):
                if value['storage']['smd_info']['devices']:
                    for device in value['storage']['smd_info']['devices']:
                        if device['dev_state'] == dev_state and device['led_state'] == led_state:
                            status = True
        elif "host_storage_map" in result.keys():
            for value in list(result['host_storage_map'].values()):
                if value['storage']['smd_info']['devices']:
                    for device in value['storage']['smd_info']['devices']:
                        if device['dev_state'] == dev_state and device['led_state'] == led_state:
                            status = True
        else:
            self.log.info("##Unsupported result dictionary %s", result)
        return status

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
            8. Replace the same drive back.
            9. Drive status LED should be off indicating good device is plugged-in.
           10. Reset and cleanup.
        Args:
            oclass (str, optional): object class (eg: RP_2G8, S1,etc). Defaults to None
        """
        # 1. Bring up server and reset led of all devices
        self.log_step("Bring up server and reset led of all devices.")
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        device_info = get_storage_query_device_info(self, self.dmg)
        devices = [device['uuid'] for device in device_info]
        self.log.info("==device_info= %s", devices)
        test_dev = devices[0]
        for device in devices:
            led_identify_result = self.run_vmd_led_identify(device, reset=True)

        # 2. Check drive status led state
        self.log_step("Check drive status led state.")
        err_dev = []
        for device in devices:
            if not self.verify_dev_led_state(device):
                err_dev.append(device)
        if err_dev:
            self.fail("#Device {} not in expected NORMAL, OFF state".format(err_dev))

        # 3. Create the pool and container with RF and start IOR
        self.log_step("Create the pool and container with RF and start IOR")
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
                "transfer_size": ior_test_seq[2],
                "block_size": ior_test_seq[3]
            }
        }
        kwargs.update(ior_kwargs)
        thread.append(threading.Thread(target=thread_run_ior, kwargs=kwargs))

        # Launch the IOR thread
        self.log.info("Start IOR thread : %s", thread)
        thread[0].start()

        # 4. While IOR (EC+GX) in progress put single drive to fault mode
        self.log_step("dmg storage set nvme-faulty and check led state")
        if not self.check_result(self.set_device_faulty(test_dev), "EVICTED", "ON"):
            self.fail("#Result of set_device_fault, device not in expected EVICTED, ON state")
        # check device state after set nvme-faulty
        time.sleep(1)
        if not self.verify_dev_led_state(test_dev, "NORMAL", "ON"):
            self.fail("#After set_device_fault, device not back to NORMAL, ON state")

        # 5. IOR will halt until rebuild is finished.
        self.log_step("IOR continue")
        # 6. Wait until the IOR thread completed and check for error
        self.log_step("IOR completed and check for error")
        thread[0].join()
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

        # 7. Check drive status LED status
        self.log_step("Check drive status and led status")
        if not self.verify_dev_led_state(test_dev, "NORMAL", "ON"):
            self.fail(
                "#After set_device_fault, IOR completed, device not in expected NORMAL, ON state")

        # Verify IOR data and check container
        self.log.info("Verify IOR data")
        ior_kwargs["ior_params"]["flags"] = self.ior_r_flags
        ior_kwargs["log"] = "ior_read_pool_test.log"
        try:
            run_ior(**ior_kwargs)
        except CommandFailure as error:
            self.fail("Error in ior read {}.".format(error))
        kwargs = {"pool": self.pool.uuid,
                  "cont": self.container.uuid}
        output = self.daos_command.container_check(**kwargs)
        self.log.info(output)

        # 8. Replace the same drive back
        self.log_step("Replace the same drive back.")
        result = self.dmg.storage_replace_nvme(old_uuid=test_dev, new_uuid=test_dev)

        # 9. Drive status LED should be dev_state=NORNAL led_state = OFF
        self.log_step("Drive status LED should be off indicating good device is plugged-in.")
        if not self.check_result(result, "NORMAL", "OFF"):
            self.fail("#After storage replace nvme, device not in expected NORMAL, OFF state")

        # 10. Reset and cleanup
        self.log_step("Reset and cleanup")
        for device in devices:
            led_identify_result = self.run_vmd_led_identify(device, reset=True)

    def test_nvme_fault_reintegration(self):
        """Test ID: DAOS-10034.

        Test Description:
            This method is called from the avocado test infrastructure. This method
                invokes NVME set fault, reintegration and led verification test.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,vmd_led,VmdLedStatus
        :avocado: tags=test_nvme_fault_reintegration
        """
        self.run_ior_with_nvme_fault()
