"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import base64
import traceback

from general_utils import check_ping, check_ssh, get_random_bytes, wait_for_result
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from pydaos.raw import DaosApiError
from run_utils import run_local, run_remote
from server_utils_base import DaosServerCommand
from storage_utils import StorageException, StorageInfo


class MultiEnginesPerSocketTest(IorTestBase, MdtestBase):
    """Daos server configuration tests.
    Test Class Description:
        Tests to verify that the multiple engines per socket on daos_server.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a MultiEnginesPerSocketTest object."""
        super().__init__(*args, **kwargs)
        self.start_agents_once = False
        self.start_servers_once = False
        self.setup_start_agents = False
        self.setup_start_servers = False

    def create_data_set(self, num_attributes):
        """Create the large attribute dictionary.

        Args:
            num_attributes (int): number of attributes to be created on container.

        Returns:
            dict: a large attribute dictionary
        """
        data_set = {}
        for index in range(num_attributes):
            size = self.random.randint(1, 10)
            key = str(index).encode("utf-8")
            data_set[key] = get_random_bytes(size)
        return data_set

    def verify_list_attr(self, indata, attributes_list):
        """Verify the length of the Attribute names.

        Args:
            indata (dict): Dict used to set attr
            attributes_list (list): List obtained from list attr
        """
        length = sum(map(len, indata.keys()))
        size = sum(map(len, attributes_list))

        self.log.info("Verifying list_attr output:")
        self.log.info("  set_attr names:  %s", list(indata.keys()))
        self.log.info("  set_attr size:   %s", length)
        self.log.info("  list_attr names: %s", attributes_list)
        self.log.info("  list_attr size:  %s", size)

        if length != size:
            self.fail(f"Container attribute list size mismatch: expected {length}, received {size}")

        # verify the Attributes names in list_attr retrieve
        for key in indata.keys():
            if key.decode() not in attributes_list:
                self.fail(f"Unexpected container attribute received: {key}")

    def verify_get_attr(self, indata, outdata):
        """verify the Attributes value after get_attr.

        Args:
             indata (dict): In data item of container get_attr.
             outdata (dict): Out data from container get_attr.
        """
        decoded = {}
        for key, val in outdata.items():
            if isinstance(val, bytes):
                decoded[key.decode()] = val
            else:
                decoded[key] = base64.b64decode(val)

        self.log.info("Verifying get_attr output:")
        self.log.info("  get_attr data: %s", indata)
        self.log.info("  set_attr data: %s", decoded)

        for attr, value in indata.items():
            received = decoded.get(attr.decode(), None)
            if value != received:
                self.fail(
                    f"Unexpected value for container attribute {attr}: expected {value}, "
                    f"received {received}")

    def daos_server_scm_reset(self):
        """Perform daos_server scm reset."""
        cmd = DaosServerCommand()
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("scm")
        cmd.sub_command_class.set_sub_command("reset")
        cmd.sub_command_class.sub_command_class.force.value = True
        self.log_step("Resetting server PMem")
        results = run_remote(self.log, self.hostlist_servers, str(cmd), timeout=180)
        if not results.passed:
            self.fail("Error resetting server PMem - ensure servers are equipped with PMem modules")

    def daos_server_scm_prepare_ns(self, engines_per_socket=1):
        """Perform daos_server scm prepare --scm-ns-per-socket.

        Args:
             engines_per_socket (int): number of engines per socket.
        """
        cmd = DaosServerCommand()
        cmd.sudo = False
        cmd.debug.value = False
        cmd.set_sub_command("scm")
        cmd.sub_command_class.set_sub_command("prepare")
        cmd.sub_command_class.sub_command_class.scm_ns_per_socket.value = engines_per_socket
        cmd.sub_command_class.sub_command_class.force.value = True
        self.log_step(f"Preparing server PMem for {engines_per_socket} engines per socket")
        results = run_remote(self.log, self.hostlist_servers, str(cmd), timeout=180)
        if not results.passed:
            self.fail(f"Error preparing server PMem for {engines_per_socket} engines per socket")

    def host_reboot(self, hosts):
        """To reboot the hosts.

        Args:
             hosts (NodeSet): hosts set to be rebooted.
        """
        cmd = "sudo shutdown -r now"
        run_remote(self.log, hosts, cmd, timeout=210)
        self.log.info("==Server %s rebooting... \n", hosts)

        if not wait_for_result(self.log, check_ping, 600, 5, True, host=hosts[0],
                               expected_ping=False, cmd_timeout=60, verbose=True):
            self.fail("Shutdown not detected within 600 seconds.")
        if not wait_for_result(self.log, check_ping, 600, 5, True, host=hosts[0],
                               expected_ping=True, cmd_timeout=60, verbose=True):
            self.fail("Reboot not detected within 600 seconds.")
        if not wait_for_result(self.log, check_ssh, 300, 2, True, hosts=hosts,
                               cmd_timeout=30, verbose=True):
            self.fail("All hosts not responding to ssh after reboot within 300 seconds.")

    def check_pmem(self, hosts, count):
        """check for server pmem.

        Args:
            hosts (NodeSet): hosts set to be checked.
            count (int): expected number of pmem storage device.

        Returns:
            bool: True if number of pmem devices on the server hosts.
        """
        storage = StorageInfo(self.log, hosts)
        try:
            storage.scan()
        except StorageException:
            self.log.debug("==Problem running StorageInfo.scan()")
        return bool(len(storage.pmem_devices) == count)

    def storage_format(self):
        """Perform storage format."""
        if not run_local(self.log, "dmg storage format").passed:
            self.fail("dmg storage format failed")

    def test_multi_engines_per_socket(self):
        """Test ID: DAOS-12076.

        Test description: Test multiple engines/sockets.
            (1) Scm reset and prepare --scm-ns-per-socket
            (2) Start server
            (3) Start agent
            (4) Dmg system query
            (5) Pool create
            (6) Container create and attributes test
            (7) IOR test
            (8) MDTEST

        To launch test:
            (1) Make sure server is equipped with PMem
            (2) ./launch.py test_multi_engines_per_socket -ts <servers> -tc <agent>

        :avocado: tags=manual
        :avocado: tags=server
        :avocado: tags=MultiEnginesPerSocketTest,test_multi_engines_per_socket
        """
        server_namespace = "/run/server_config/*"
        num_attributes = self.params.get("num_attributes", '/run/container/*')
        _engines_per_socket = self.params.get("engines_per_socket", server_namespace, 1)
        _num_pmem = self.params.get("number_pmem", server_namespace, 1)

        # Configure PMem for multiple engines per socket
        self.daos_server_scm_reset()
        self.host_reboot(self.hostlist_servers)
        self.daos_server_scm_prepare_ns(_engines_per_socket)
        self.host_reboot(self.hostlist_servers)
        self.daos_server_scm_prepare_ns(_engines_per_socket)
        if not wait_for_result(self.log, self.check_pmem, 160, 1, False,
                               hosts=self.hostlist_servers, count=_num_pmem):
            self.fail(f"Error {_num_pmem} PMem devices not found on all hosts.")

        # Start servers
        self.log_step("Starting servers")
        run_remote(self.log, self.hostlist_servers, 'lsblk|grep -E "NAME|pmem"')
        self.start_servers()

        # Start agents
        self.log_step("Starting agents")
        self.start_agents()

        # Run some dmg commands
        self.log_step("Query the storage usage")
        dmg = self.get_dmg_command()
        # dmg.storage_query_usage()
        dmg.storage_query_list_devices()

        # Create a pool
        self.log_step("Create a pool")
        self.add_pool(connect=False)

        # (6) Container create and attributes test
        self.log_step("Create a container and verify the attributes")
        self.add_container(self.pool)
        self.container.open()
        attr_dict = self.create_data_set(num_attributes)
        try:
            self.container.container.set_attr(data=attr_dict)
            data = self.container.list_attrs(verbose=False)
            self.verify_list_attr(attr_dict, data['response'])
            data = self.container.list_attrs(verbose=True)
            self.verify_get_attr(attr_dict, data['response'])
        except DaosApiError as error:
            self.log.info(error)
            self.log.info(traceback.format_exc())
            self.fail("Error setting and verify container attributes")
        self.container.close()
        self.pool.disconnect()

        # (7) IOR test
        self.log_step("Run ior")
        ior_timeout = self.params.get("ior_timeout", '/run/ior/*')
        self.run_ior_with_pool(
            timeout=ior_timeout, create_pool=True, create_cont=True, stop_dfuse=True)

        # (8) MDTEST
        self.log_step("Run mdtest")
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")
        self.run_mdtest_multiple_variants(mdtest_params)
        self.log.info("Test passed")
