#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from collections import defaultdict
import yaml

from apricot import TestWithServers
from exception_utils import CommandFailure
from dmg_utils import DmgCommand


class ConfigGenerateOutput(TestWithServers):
    """Test ID: DAOS-7274

    Verify dmg config generate output.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ConfigGenerateOutput object."""
        super().__init__(*args, **kwargs)
        # Data structure that store expected values.
        self.nvme_socket_to_addrs = defaultdict(list)
        self.scm_socket_to_namespaces = defaultdict(list)
        self.pci_address_set = set()
        self.scm_namespace_set = set()

        self.interface_to_providers = defaultdict(list)
        self.interface_set = set()

    def prepare_expected_data(self):
        """Prepare expected values.

        Call dmg storage scan and network scan to collect expected values.
        """
        dmg = self.get_dmg_command()

        # Call dmg storage scan --json.
        storage_out = dmg.storage_scan()

        # Check the status.
        if storage_out["status"] != 0:
            self.log.error(storage_out["error"])
            self.fail("dmg storage scan failed!")

        # Get nvme_devices and scm_namespaces list that are buried. There's a
        # uint64 hash of the strcut under HostStorage.
        temp_dict = storage_out["response"]["HostStorage"]
        struct_hash = list(temp_dict.keys())[0]
        nvme_devices = temp_dict[struct_hash]["storage"]["nvme_devices"]
        scm_namespaces = temp_dict[struct_hash]["storage"]["scm_namespaces"]

        # Fill in the dictionary and the set for NVMe.
        for nvme_device in nvme_devices:
            socket_id = nvme_device["socket_id"]
            pci_addr = nvme_device["pci_addr"]
            self.nvme_socket_to_addrs[socket_id].append(pci_addr)
            self.pci_address_set.add(pci_addr)

        self.log.info(
            "nvme_socket_to_addrs = %s", self.nvme_socket_to_addrs)
        self.log.info("pci_address_set = %s", self.pci_address_set)

        # Fill in the dictionary and the set for SCM.
        for scm_namespace in scm_namespaces:
            numa_node = scm_namespace["numa_node"]
            blockdev = scm_namespace["blockdev"]
            self.scm_socket_to_namespaces[numa_node].append(blockdev)
            self.scm_namespace_set.add(blockdev)

        self.log.info(
            "scm_socket_to_namespaces = %s", self.scm_socket_to_namespaces)
        self.log.info("scm_namespace_set = %s", self.scm_namespace_set)

        # Call dmg network scan --provider=all --json.
        network_out = dmg.network_scan(provider="all")

        # Check the status.
        if network_out["status"] != 0:
            self.log.error(network_out["error"])
            self.fail("dmg network scan failed!")

        # Get the hash and the Interfaces list.
        temp_dict = network_out["response"]["HostFabrics"]
        struct_hash = list(temp_dict.keys())[0]
        interfaces = temp_dict[struct_hash]["HostFabric"]["Interfaces"]

        # Fill in the dictionary and the set for interface.
        for interface in interfaces:
            provider = interface["Provider"]
            device = interface["Device"]
            self.interface_to_providers[device].append(provider)
            self.interface_set.add(device)

        self.log.info(
            "interface_to_providers = %s", self.interface_to_providers)
        self.log.info("interface_set = %s", self.interface_set)

    def check_errors(self, errors):
        """Check if there's any error in given list. If so, fail the test.

        Args:
            errors (list): List of errors.
        """
        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))

    def verify_access_point(self, host_port_input, failure_expected=None):
        """Run with given AP and verify the AP in the output.

        Args:
            host_port_input (str): Host:Port or just Host. Supports multiple APs
                that are separated by comma.
            failure_expected (str): Expected error message. Set it to None if
                not expecting any error. Defaults to None.

        Returns:
            list: List or errors.
        """
        errors = []
        check = {}

        check["expected"] = host_port_input.split(",")
        if ":" not in host_port_input:
            # dmg automatically sets 10001 if it's not given in the input.
            check["expected"] = [
                "{}:10001".format(host) for host in check["expected"]]

        # Create a new DmgCommand and set its exit_status_exception to False to
        # make it not raise a TestFailure when the command failed. Then we'll be
        # able to check result.exit_status for our testing purpose.
        dmg = DmgCommand(self.bin)
        dmg.exit_status_exception = False

        try:
            result = dmg.config_generate(access_points=host_port_input)
        except CommandFailure as err:
            errors.append("Unexpected failure! {}".format(err))

        if result.exit_status == 0 and failure_expected is None:
            try:
                yaml_data = yaml.safe_load(result.stdout)
                check["actual"] = yaml_data["access_points"]
                if sorted(check["expected"]) != sorted(check["actual"]):
                    errors.append(
                        "Unexpected access point: {} != {}".format(
                            check["expected"], check["actual"]))
            except yaml.YAMLError as error:
                errors.append(
                    "Error loading dmg generated config!: {}".format(error))
        elif result.exit_status == 0 and failure_expected is not None:
            errors.append(
                "dmg command passed when expected to fail!: {}".format(result))
        elif result.exit_status != 0 and failure_expected is not None:
            if failure_expected not in result.stderr_text:
                errors.append(
                    "Missing expected error message in failed dmg command!: " +
                    "{}".format(result))
        else:
            errors.append(
                "dmg command failed when expected to pass!: {}".format(result))

        return errors

    def test_basic_config(self):
        """Test basic configuration.

        Sanity check for the default arguments.

        1. Call dmg config generate (using default values)
        2. Verify the last scm_list value is in the SCM Namespace set.
        e.g., /dev/pmem0 -> Verify that pmem0 is in the set.
        3. Iterate bdev_list address list and verify that it's in the NVMe PCI
        address set.
        4. Get fabric_iface and verify that it's in the interface set.
        5. Repeat for all engines.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,basic_config
        """
        # Get necessary storage and network info.
        self.prepare_expected_data()

        # Call dmg config generate.
        result = self.get_dmg_command().config_generate(access_points="wolf-a")
        generated_yaml = yaml.safe_load(result.stdout)

        errors = []

        scm_found = False
        nvme_found = False

        # Iterate engines and verify scm_list, bdev_list, and fabric_iface.
        engines = generated_yaml["engines"]
        for engine in engines:
            for storage in engine["storage"]:
                if storage["class"] == "dcpm":
                    # Verify the scm_list value is in the SCM Namespace set. e.g.,
                    # if the value is /dev/pmem0, check pmem0 is in the set.
                    scm_list = storage["scm_list"]
                    for scm_dev in scm_list:
                        device_name = scm_dev.split("/")[-1]
                        if device_name not in self.scm_namespace_set:
                            errors.append(
                                "Cannot find SCM device name {} in expected set {}"\
                                    .format(device_name, self.scm_namespace_set))
                        scm_found = True

                # Verify the bdev_list values are in the NVMe PCI address set.
                if storage["class"] == "nvme":
                    bdev_list = storage["bdev_list"]
                    for pci_addr in bdev_list:
                        if pci_addr not in self.pci_address_set:
                            errors.append(
                                "Cannot find PCI address {} in expected set {}".format(
                                    pci_addr, self.pci_address_set))
                        nvme_found = True

            if not scm_found:
                errors.append("No SCM devices found")
            if not nvme_found:
                errors.append("No NVMe devices found")

            # Verify fabric interface values are in the interface set.
            fabric_iface = engine["fabric_iface"]
            if fabric_iface not in self.interface_set:
                errors.append(
                    "Cannot find fabric interface {} in expected set {}".format(
                        fabric_iface, self.interface_set))

        self.check_errors(errors)

    def test_access_points_single(self):
        """Test --access-points with single AP with and without port.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,access_points,ap_single
        """
        errors = []

        # Single AP.
        errors.extend(self.verify_access_point("wolf-a"))

        # Single AP with a valid port.
        errors.extend(self.verify_access_point("wolf-a:12345"))

        self.check_errors(errors)

    def test_access_points_odd(self):
        """Test --access-points with odd number of APs.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,access_points,ap_odd
        """
        errors = []

        # Odd AP.
        errors.extend(self.verify_access_point("wolf-a,wolf-b,wolf-c"))

        # Odd AP with port.
        errors.extend(
            self.verify_access_point(
                "wolf-a:12345,wolf-b:12345,wolf-c:12345"))

        self.check_errors(errors)

    def test_access_points_invalid(self):
        """Test --access-points with invalid port.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,access_points,ap_invalid
        """
        errors = []

        # Even AP.
        errors.extend(self.verify_access_point("wolf-a,wolf-b", "non-odd"))

        # Single AP with an invalid port.
        errors.extend(
            self.verify_access_point(
                "wolf-a:abcd", "invalid access point port"))

        # Odd AP with both valid and invalid port.
        errors.extend(
            self.verify_access_point(
                "wolf-a:12345,wolf-b:12345,wolf-c:abcd",
                "invalid access point port"))

        self.check_errors(errors)

    def test_access_points_same_ap_repeated(self):
        """Test --access-points with the same APs repeated.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,access_points,ap_repeated
        """
        errors = []

        errors.extend(
            self.verify_access_point(
                "wolf-a,wolf-a,wolf-a", "duplicate access points"))

        self.check_errors(errors)

    def test_num_engines(self):
        """Test --num-engines.

        1. Using the NVMe PCI dictionary, find the number of keys. i.e., number
        of Socket IDs. This would determine the maximum number of engines.
        2. Call dmg config generate --num-engines=<1 to max_engine>. Should
        pass.
        3. Call dmg config generate --num-engines=<max_engine + 1> Should fail.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,num_engines
        """
        # Get necessary storage and network info.
        self.prepare_expected_data()

        # Find the maximum number of engines we can use. It's the number of
        # sockets in NVMe. However, I'm not sure if we need to have the same
        # number of interfaces. Go over this step if we have issue with the
        # max_engine assumption.
        max_engine = len(list(self.nvme_socket_to_addrs.keys()))
        self.log.info("max_engine threshold = %s", max_engine)

        dmg = DmgCommand(self.bin)
        dmg.exit_status_exception = False
        errors = []

        # Call dmg config generate --num-engines=<1 to max_engine>
        for num_engines in range(1, max_engine + 1):
            result = dmg.config_generate(
                access_points="wolf-a", num_engines=num_engines)
            generated_yaml = yaml.safe_load(result.stdout)
            actual_num_engines = len(generated_yaml["engines"])

            # Verify the number of engine field.
            if actual_num_engines != num_engines:
                msg = "Unexpected number of engine field! Expected = {}; "\
                    "Actual = {}".format(num_engines, actual_num_engines)
                errors.append(msg)

        # Verify that max_engine + 1 fails.
        result = dmg.config_generate(
            access_points="wolf-a", num_engines=max_engine + 1)
        if result.exit_status == 0:
            errors.append(
                "Host + invalid num engines succeeded with {}!".format(
                    max_engine + 1))

        self.check_errors(errors)

    def test_min_ssds(self):
        """Test --min-ssds.

        1. Iterate the NVMe PCI dictionary and find the key that has the
        shortest list. This would be our min_ssd engine count threshold.
        2. Call dmg config generate --min-ssds=<1 to min_ssd>. Should pass.
        3. Call dmg config generate --min-ssds=<min_ssd + 1>. Should fail.
        4. Call dmg config generate --min-ssds=0. Iterate the engines field and
        verify that there's no bdev_list field.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,min_ssds
        """
        # Get necessary storage and network info.
        self.prepare_expected_data()

        # Iterate the NVMe PCI dictionary and find the key that has the shortest
        # list. This would be our min_ssd engine count threshold.
        socket_ids = list(self.nvme_socket_to_addrs.keys())
        shortest_id = socket_ids[0]
        shortest = len(self.nvme_socket_to_addrs[shortest_id])
        for socket_id in socket_ids:
            if len(self.nvme_socket_to_addrs[socket_id]) < shortest:
                shortest = len(self.nvme_socket_to_addrs[socket_id])
                shortest_id = socket_id

        min_ssd = len(self.nvme_socket_to_addrs[shortest_id])
        self.log.info("Maximum --min-ssds threshold = %d", min_ssd)

        dmg = DmgCommand(self.bin)
        dmg.exit_status_exception = False

        errors = []

        # Call dmg config generate --min-ssds=<1 to min_ssd>. Should pass.
        for num_ssd in range(1, min_ssd + 1):
            result = dmg.config_generate(
                access_points="wolf-a", min_ssds=num_ssd)
            if result.exit_status != 0:
                errors.append(
                    "config generate failed with min_ssd = {}!".format(num_ssd))

        # Call dmg config generate --min_ssds=<min_ssd + 1>. Should fail.
        result = dmg.config_generate(
            access_points="wolf-a", min_ssds=min_ssd + 1)
        if result.exit_status == 0:
            errors.append(
                "config generate succeeded with min_ssd + 1 = {}!".format(
                    min_ssd + 1))

        # Call dmg config generate --min-ssds=0
        result = dmg.config_generate(access_points="wolf-a", min_ssds=0)
        generated_yaml = yaml.safe_load(result.stdout)
        # Iterate the engines and verify that there's no bdev_list field.
        engines = generated_yaml["engines"]
        for engine in engines:
            if "bdev_list" in engine["storage"]:
                errors.append("bdev_list field exists with --min-ssds=0!")

        self.check_errors(errors)

    def test_net_class(self):
        """Test --net-class.

        1. Iterate the interface set and count the number of elements that
        starts with "ib". This would be our ib_count threshold that we can set
        --num-engines with --net-class=infiniband.
        2. Call dmg config generate --net-class=infiniband
        --num-engines=<1 to ib_count> and verify that it works.
        3. In addition, verify provider using the dictionary. i.e., iterate
        "engines" fields and verify "provider" is in the list where key is
        "fabric_iface".
        4. Similarly find eth_count and call dmg config generate
        --net-class=ethernet --num-engines=<1 to eth_count> and verify that it
        works.
        5. As in ib, also verify provider using the dictionary. i.e., iterate
        "engines" fields and verify "provider" is in the list where key is
        "fabric_iface".

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=control,config_generate_entries,net_class
        """
        # Get necessary storage and network info.
        self.prepare_expected_data()

        # Get ib_count threshold.
        ib_count = 0
        for interface in self.interface_set:
            if interface[:2] == "ib":
                ib_count += 1
        self.log.info("ib_count = %d", ib_count)

        dmg = DmgCommand(self.bin)
        dmg.exit_status_exception = False
        errors = []

        # Call dmg config generate --num-engines=<1 to ib_count>
        # --net-class=infiniband. Should pass.
        for num_engines in range(1, ib_count + 1):
            # dmg config generate should pass.
            result = dmg.config_generate(
                access_points="wolf-a", num_engines=num_engines,
                net_class="infiniband")

            if result.exit_status != 0:
                msg = "config generate failed with --net-class=infiniband "\
                    "--num-engines = {}!".format(num_engines)
                errors.append(msg)
            else:
                generated_config = yaml.safe_load(result.stdout)
                for engine in generated_config["engines"]:
                    fabric_iface = engine["fabric_iface"]
                    provider = engine["provider"]
                    # Verify fabric_iface field, e.g., ib0 by checking the
                    # dictionary keys.
                    if not self.interface_to_providers[fabric_iface]:
                        errors.append(
                            "Unexpected fabric_iface! {}".format(fabric_iface))
                    elif provider not in \
                        self.interface_to_providers[fabric_iface]:
                        # Now check the provider field, e.g., ofi+sockets by
                        # checking the corresponding list in the dictionary.
                        msg = "Unexpected provider in fabric_iface! provider ="\
                            " {}; fabric_iface = {}".format(
                                provider, fabric_iface)
                        errors.append(msg)

        # Call dmg config generate --num-engines=<ib_count + 1>
        # --net-class=infiniband. Too many engines. Should fail.
        result = dmg.config_generate(
            access_points="wolf-a", num_engines=ib_count + 1,
            net_class="infiniband")
        if result.exit_status == 0:
            msg = "config generate succeeded with --net-class=infiniband "\
                "num_engines = {}!".format(ib_count + 1)
            errors.append(msg)

        # Get eth_count threshold.
        eth_count = 0
        for interface in self.interface_set:
            if interface[:3] == "eth":
                eth_count += 1
        self.log.info("eth_count = %d", eth_count)

        # Call dmg config generate --num-engines=<1 to eth_count>
        # --net-class=ethernet. Should pass.
        for num_engines in range(1, eth_count + 1):
            # dmg config generate should pass.
            result = dmg.config_generate(
                access_points="wolf-a", num_engines=num_engines,
                net_class="ethernet")

            if result.exit_status != 0:
                msg = "config generate failed with --net-class=ethernet "\
                    "--num-engines = {}!".format(num_engines)
                errors.append(msg)
            else:
                generated_config = yaml.safe_load(result.stdout)
                for engine in generated_config["engines"]:
                    fabric_iface = engine["fabric_iface"]
                    provider = engine["provider"]
                    # Verify fabric_iface field, e.g., eth0 by checking the
                    # dictionary keys.
                    if not self.interface_to_providers[fabric_iface]:
                        errors.append(
                            "Unexpected fabric_iface! {}".format(fabric_iface))
                    elif provider not in \
                        self.interface_to_providers[fabric_iface]:
                        # Now check the provider field, e.g., ofi+sockets by
                        # checking the corresponding list in the dictionary.
                        msg = "Unexpected provider in fabric_iface! provider ="\
                            " {}; fabric_iface = {}".format(
                                provider, fabric_iface)
                        errors.append(msg)

        # Call dmg config generate --num-engines=<eth_count + 1>
        # --net-class=ethernet. Too many engines. Should fail.
        result = dmg.config_generate(
            access_points="wolf-a", num_engines=eth_count + 1,
            net_class="ethernet")
        if result.exit_status == 0:
            msg = "config generate succeeded with --net-class=ethernet, "\
                "num_engines = {}!".format(eth_count + 1)
            errors.append(msg)

        self.check_errors(errors)
