"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading
import time
from multiprocessing import Queue

from apricot import TestWithServers
from avocado import fail_on
from ClusterShell.NodeSet import NodeSet
from dmg_utils import get_dmg_response, get_storage_query_device_info
from exception_utils import CommandFailure
from ior_utils import thread_run_ior
from job_manager_utils import get_job_manager
from nvme_utils import set_device_faulty
from run_utils import run_remote


class NvmeHotplug(TestWithServers):
    """Test Class Description: Exercises NVMe hotplug handling, including a basic live
    unplug/re-plug/rebind cycle, and the full disk-replacement procedure (fault, hot-swap,
    replace, LED reset).

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg = self.get_dmg_command()
        self.dmg.hostlist = NodeSet(self.hostlist_servers[0])
        self.skip_if_vmd_enabled()

    def skip_if_vmd_enabled(self):
        """Skip this test if any detected NVMe device is behind a VMD controller.

        The sysfs-based unplug/rescan/rebind sequence used by these tests targets a device's
        own direct PCI address. A VMD-backed device uses a composite address and is serviced
        through its VMD controller instead, so the same sequence is not applicable.
        """
        for device in get_storage_query_device_info(self.dmg):
            pci_addr = device['ctrlr']['pci_addr']
            if len(pci_addr.split(':')[0]) > 4:
                self.cancel(
                    f"Skipping test: device {pci_addr} is behind a VMD controller")

    def get_device_by_rank(self, rank):
        """Get the first storage device assigned to the specified rank.

        Args:
            rank (int): engine rank.

        Returns:
            dict: device information for the first device found on rank, or None if not found.

        """
        for device in get_storage_query_device_info(self.dmg):
            if device['rank'] == rank:
                return device
        return None

    def get_device_by_pci_addr(self, pci_addr, exclude_uuid=None):
        """Get the storage device with the specified PCI address.

        Args:
            pci_addr (str): PCI address, including domain, e.g. "0000:ac:00.0".
            exclude_uuid (str, optional): skip a device with this UUID. Defaults to None.

        Returns:
            dict: device information for the device, or None if not found.

        """
        for device in get_storage_query_device_info(self.dmg):
            if device['ctrlr']['pci_addr'] == pci_addr and device['uuid'] != exclude_uuid:
                return device
        return None

    def wait_for_device_state(self, pci_addr, dev_state, exclude_uuid=None, timeout=90,
                              interval=5):
        """Poll until the device at a PCI address reaches an expected dev_state.

        Args:
            pci_addr (str): PCI address of the device to check.
            dev_state (str): expected dev_state, e.g. NORMAL/NEW/EVICTED/UNPLUGGED.
            exclude_uuid (str, optional): skip a device with this UUID. Defaults to None.
            timeout (int, optional): seconds to wait before giving up. Defaults to 90.
            interval (int, optional): seconds to sleep between checks. Defaults to 5.

        Returns:
            dict: the matching device information, or None if the timeout was reached.

        """
        start = time.time()
        while time.time() - start < timeout:
            device = self.get_device_by_pci_addr(pci_addr, exclude_uuid)
            if device is not None and device['ctrlr']['dev_state'] == dev_state:
                return device
            time.sleep(interval)
        return None

    def simulate_hot_swap(self, host, pci_addr, wipe=False):
        """Simulate a physical hot-swap of an NVMe SSD via sysfs PCI operations.

        Args:
            host (str): host on which to run the sysfs commands.
            pci_addr (str): PCI address of the device to remove and re-plug.
            wipe (bool, optional): wipe the device's existing metadata header after re-plugging,
                to simulate a blank replacement drive, since the same physical device is reused.
                Only safe to use while the owning engine remains running throughout (e.g. a live
                disk-replacement); an offline engine restarted against a wiped device whose SMD
                record no longer matches will fail to start. Defaults to False.

        """
        host = NodeSet(host)
        self.log_step(f"Simulating unplug of device {pci_addr} on {host}")
        remove_cmd = f"echo 1 > /sys/bus/pci/devices/{pci_addr}/remove"
        if not run_remote(self.log, host, remove_cmd).passed:
            self.fail(f"Failed to remove device {pci_addr} on {host}")
        time.sleep(30)

        self.log_step(f"Simulating re-plug of device {pci_addr} via a PCI bus rescan on {host}")
        rescan_cmd = "echo 1 > /sys/bus/pci/rescan"
        if not run_remote(self.log, host, rescan_cmd).passed:
            self.fail(f"Failed to rescan the PCI bus on {host}")

        if wipe:
            self.wipe_device_header(host, pci_addr)

    def wipe_device_header(self, host, pci_addr):
        """Wipe the start of a kernel-bound NVMe namespace block device.

        Erases any existing SPDK blobstore/SMD metadata header so the device at pci_addr is
        attached by the kernel nvme driver, is treated as blank/new when it is subsequently
        rebound to the userspace driver, simulating a real drive replacement without requiring
        a physical swap.

        Args:
            host (NodeSet): host on which to run the wipe command.
            pci_addr (str): PCI address of the device to wipe.

        """
        self.log_step(f"Wiping device {pci_addr} to simulate a blank replacement drive on {host}")
        wipe_cmd = (
            f"dev=$(ls -d /sys/bus/pci/devices/{pci_addr}/nvme/nvme*/nvme*n* 2>/dev/null "
            f"| head -n 1 | xargs -r basename); "
            f"if [ -z \"$dev\" ]; then echo \"No kernel block device found for {pci_addr}\" "
            f">&2; exit 1; fi; "
            f"dd if=/dev/zero of=/dev/$dev bs=1M count=10 oflag=direct status=none")
        if not run_remote(self.log, host, wipe_cmd).passed:
            self.fail(f"Failed to wipe device {pci_addr} on {host}")

    def rebind_device(self, host, pci_addr):
        """Rebind a hot-plugged NVMe SSD from the kernel driver to the userspace driver.

        Args:
            host (str): host on which to issue the dmg storage nvme-rebind command.
            pci_addr (str): PCI address of the device to rebind.

        """
        self.log_step(f"Rebinding device {pci_addr} to the userspace driver on {host}")
        self.dmg.hostlist = NodeSet(host)
        try:
            get_dmg_response(self.dmg.storage_nvme_rebind, pci_address=pci_addr)
        finally:
            self.dmg.hostlist = NodeSet(self.hostlist_servers[0])

    @fail_on(CommandFailure)
    def test_nvme_online_disk_replug(self):
        """Verify a basic, live NVMe hotplug: with the owning engine kept running throughout,
        simulate an unplug/re-plug of one of its devices, rebind it to the userspace driver, and
        verify the engine automatically reattaches it to a 'NORMAL' state on the same rank.

        This models a real maintenance scenario, e.g. a drive being reseated or a transient PCI
        disconnect/reconnect, without requiring the engine to be taken down. Since the same
        physical device is reused (this harness has no way to physically swap a drive) and its
        on-disk metadata is preserved, the engine recognizes it as the device it already owns
        and reattaches it automatically; no rank/target restart is needed or performed.

        Test steps:
            1. Start the DAOS servers with multiple engines per host.
            2. Select a device on a rank, with the owning engine left running.
            3. Simulate an unplug of the device via sysfs.
            4. Simulate a re-plug via a PCI bus rescan.
            5. Rebind the device to the userspace driver with 'dmg storage nvme-rebind'.
            6. Verify 'dmg storage query list-devices' shows the engine automatically
               reattached the device to the same rank, in a 'NORMAL' state.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,hotplug
        :avocado: tags=NvmeHotplug,test_nvme_online_disk_replug
        """
        server_manager = self.server_managers[-1]
        rank_to_host = server_manager.ranks
        rank = sorted(rank_to_host)[0]
        host = rank_to_host[rank]

        self.log_step(f"Selecting a device on rank {rank}")
        device = self.get_device_by_rank(rank)
        if device is None:
            self.fail(f"No device found for rank {rank}")
        pci_addr = device['ctrlr']['pci_addr']
        old_uuid = device['uuid']
        self.log.info("Using device %s (%s) on rank %s", old_uuid, pci_addr, rank)

        self.simulate_hot_swap(host, pci_addr)
        self.rebind_device(host, pci_addr)

        self.log_step("Verifying the engine automatically reattached the device")
        new_device = self.wait_for_device_state(pci_addr, "NORMAL")
        if new_device is None:
            new_device = self.get_device_by_pci_addr(pci_addr)
        if new_device is None:
            self.fail(f"Device {pci_addr} not found after rebind")
        if new_device['uuid'] != old_uuid:
            self.fail(
                f"Device {pci_addr} reattached with unexpected UUID {new_device['uuid']}, "
                f"expected {old_uuid}")
        if new_device['rank'] != rank:
            self.fail(
                f"Device {pci_addr} attributed to rank {new_device['rank']}, expected rank {rank}")

        self.log.info("Test passed")

    @fail_on(CommandFailure)
    def test_nvme_online_disk_replacement(self):
        """Verify the full disk-replacement procedure: fault a device during I/O, simulate a
        physical hot-swap, replace the faulty device with the new one, and verify the pool
        recovers its usable space.

        Test steps:
            1. Create a pool and container, and start an IOR write workload in the background.
            2. While I/O is in progress, identify a device and mark it faulty with
               'dmg storage set nvme-faulty'.
            3. Verify rebuild starts and the device state becomes 'EVICTED'.
            4. Wait for IOR to complete and verify it reported no errors.
            5. Simulate a hot-swap of the faulty device via sysfs, wiping its existing metadata
               header to simulate a blank replacement drive since the same physical device is
               reused.
            6. Verify 'dmg storage query list-devices' shows the old device as 'UNPLUGGED' and
               the new device as 'NEW'.
            7. Replace the old device entry with the new one using 'dmg storage replace nvme'.
            8. Verify the new device reaches the 'NORMAL' state.
            9. Verify the pool's usable NVMe space increases after the replacement.
            10. Reset the LED state of the replaced device.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,hotplug
        :avocado: tags=NvmeHotplug,test_nvme_online_disk_replacement
        """
        host = self.hostlist_servers[0]

        self.log_step("Creating a pool and container, and starting an IOR write workload")
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(self.pool)
        job_manager = get_job_manager(self, subprocess=None, timeout=120)
        thread_queue = Queue()
        ior_kwargs = {
            "thread_queue": thread_queue,
            "job_id": 1,
            "test": self,
            "manager": job_manager,
            "log": "ior_write_hotplug.log",
            "hosts": self.hostlist_clients,
            "path": self.workdir,
            "slots": None,
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
        }
        ior_thread = threading.Thread(target=thread_run_ior, kwargs=ior_kwargs)
        ior_thread.start()

        self.log_step("Identifying a device to mark faulty while I/O is in progress")
        device_info = get_storage_query_device_info(self.dmg)
        old_uuid = device_info[0]['uuid']
        pci_addr = device_info[0]['ctrlr']['pci_addr']
        free_nvme_before = self.pool.get_pool_free_space("nvme")
        self.log.info(
            "Marking device %s (%s) faulty; free NVMe space before replacement: %s",
            old_uuid, pci_addr, free_nvme_before)

        self.log_step("Marking the device faulty and waiting for rebuild to complete")
        set_device_faulty(self, self.dmg, host, old_uuid, self.pool)

        self.log_step("Verifying the device state is 'EVICTED'")
        if self.wait_for_device_state(pci_addr, "EVICTED") is None:
            self.fail(f"Device {old_uuid} not in expected EVICTED state")

        self.log_step("Waiting for IOR to complete and checking for errors")
        ior_thread.join()
        errors = []
        while not thread_queue.empty():
            result = thread_queue.get()
            if result["result"].exit_status != 0:
                errors.append(f"IOR job {result['job_id']} failed: {result['result']}")
        if errors:
            self.fail("Errors running IOR: {}".format("; ".join(errors)))

        self.simulate_hot_swap(host, pci_addr, wipe=True)

        self.log_step("Verifying the old device is 'UNPLUGGED' and the new device is 'NEW'")
        old_device = self.wait_for_device_state(pci_addr, "UNPLUGGED", exclude_uuid=None)
        new_device = self.wait_for_device_state(pci_addr, "NEW", exclude_uuid=old_uuid)
        if old_device is None or old_device['uuid'] != old_uuid:
            self.fail(f"Old device {old_uuid} not in expected UNPLUGGED state")
        if new_device is None:
            self.fail(f"No new device in expected NEW state found at {pci_addr}")
        new_uuid = new_device['uuid']

        self.log_step(f"Replacing device {old_uuid} with {new_uuid}")
        get_dmg_response(
            self.dmg.storage_replace_nvme, host=host, old_uuid=old_uuid, new_uuid=new_uuid)
        self.pool.wait_for_rebuild_to_start()
        self.pool.wait_for_rebuild_to_end()

        self.log_step("Verifying the replaced device reaches the 'NORMAL' state")
        if self.wait_for_device_state(pci_addr, "NORMAL") is None:
            self.fail(f"Device {new_uuid} not in expected NORMAL state after replacement")

        self.log_step("Verifying the pool usable NVMe space increases after replacement")
        if not self.pool.check_free_space(
                expected_nvme=">" + str(free_nvme_before), timeout=120):
            self.fail("Pool usable NVMe space did not increase after device replacement")

        self.log_step(f"Resetting the LED state of device {new_uuid}")
        get_dmg_response(self.dmg.storage_led_identify, reset=True, ids=new_uuid)
        self.log.info("Test passed")
