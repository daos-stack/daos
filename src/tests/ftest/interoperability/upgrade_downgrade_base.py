'''
  (C) Copyright 2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import time

from agent_utils import include_local_host
from avocado.utils.process import CmdResult
from ClusterShell.NodeSet import NodeSet
from dfuse_utils import Dfuse
from dmg_utils import DmgCommand
from exception_utils import CommandFailure
from general_utils import dict_to_str, get_random_string, report_errors
from ior_test_base import IorTestBase
from package_utils import Version, install_packages
from run_utils import command_as_user, run_remote

# There are several issues that need addressing before this test case can be fully automated
# pylint: disable=fixme

# TODO launch.py _faults_enabled


class UpgradeDowngradeBase(IorTestBase):
    # pylint: disable=too-many-public-methods
    """
    Tests DAOS container attribute get/set/list.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ContainerAttributeTest object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None
        self.old_version = None
        self.new_version = None
        self.first_server = NodeSet()
        self.first_client = NodeSet()
        self.current_server_version = None
        self.current_client_version = None

    def setUp(self):
        """Set up each test case."""
        # Get params needed by setUp
        self.old_version = self.params.get("old_version", '/run/interop/*')
        self.new_version = self.params.get("new_version", '/run/interop/*')
        self.assertTrue(
            self.old_version and self.new_version, "old_version and new_version are required")
        self.current_server_version = Version(self.old_version)
        self.current_client_version = Version(self.old_version)
        super().setUp()
        self.first_server = NodeSet(self.hostlist_servers[0])
        self.first_client = NodeSet(self.hostlist_clients[0])

    def start_servers(self, server_groups=None, force=False):
        """Start the daos_server processes.

        Override base method to use a RemoteDmgCommand to run dmg on a server host.

        Args:
            server_groups (dict, optional): dictionary of dictionaries,
                containing the list of hosts on which to start the daos server
                and the list of access points, using a unique server group name
                key. Defaults to None which will use the server group name, all
                of the server hosts, and the access points from the test's yaml
                file to define a single server group entry.
            force (bool, optional): whether or not to force starting the
                servers. Defaults to False.

        Returns:
            bool: whether or not to force the starting of the agents

        Raises:
            avocado.core.exceptions.TestFail: if there is an error starting the
                servers

        """
        self.setup_servers(server_groups, self.current_server_version)
        if not self.server_managers:
            return False
        self.server_managers[0].dmg = RemoteDmgCommand(
            NodeSet(self.hostlist_servers[0]),
            self.server_managers[0].dmg.temporary_file,
            self.server_managers[0].dmg.command_path,
            self.server_managers[0].dmg.yaml,
            self.server_managers[0].dmg.hostlist_suffix)

        return self.start_server_managers(force)

    def system_stop(self, servers=True, agents=True):
        """Stop all servers and/or agents."""
        if servers:
            self.get_dmg_command().system_stop()
        errors = []
        if agents:
            errors.extend(self._stop_managers(self.agent_managers, "agents"))
        if servers:
            errors.extend(self._stop_managers(self.server_managers, "servers"))
        if errors:
            report_errors(self, errors)
        # TODO instead of arbitrarily sleeping, we should just check that they are stopped
        self.log.info("Sleeping 30 seconds after stopping servers/agents")
        time.sleep(30)

    def pool_set_attr(self, pool, attrs):
        """Call daos pool set-attr on a client node.

        Args:
            pool (TestPool): pool object
            attrs (dict): attribute name, value pairs
        """
        attrs_str = dict_to_str(attrs, joiner=",", items_joiner=":")
        command = f'daos pool set-attr {pool.identifier} {attrs_str}'
        if not run_remote(self.log, self.first_client, command).passed:
            self.fail('Failed to set pool attributes')

    def pool_list_attrs(self, pool):
        """Call daos pool list-attrs on a client node.

        Cannot use json output due to DAOS-13173.
        Even after resolution, this would need to be backward compatible.

        Args:
            pool (TestPool): pool object

        Returns:
            dict: attribute name, value pairs
        """
        cmd = f'daos pool list-attrs {pool.identifier} --verbose'
        result = run_remote(self.log, self.first_client, cmd)
        if not result.passed:
            self.fail('Failed to list pool attributes')
        attrs = {}
        for stdout in result.all_stdout.values():
            for line in stdout.split('\n')[3:]:
                key, val = map(str.strip, line.split(' ', maxsplit=1))
                attrs[key] = val
        return attrs

    def create_attr_dict(self, num_attributes):
        """Create the large attribute dictionary.

        Args:
            num_attributes (int): number of attributes to be created on container.
        Returns:
            dict: a large attribute dictionary
        """
        data_set = {}
        for index in range(num_attributes):
            size = self.random.randint(1, 10)
            attr_name = f'attr{str(index).rjust(2, "0")}'
            attr_val = str(get_random_string(size))
            data_set[attr_name] = attr_val
        return data_set

    def verify_pool_attrs(self, pool, attrs_set):
        """"Verify pool attributes.

        Args:
            pool (TestPool): pool to verify
            attrs_set (dict): expected pool attributes data.
        """
        attrs_list = self.pool_list_attrs(pool)
        self.log.info("==Verifying list_attr output:")
        self.log.info("  attributes from set-attr:  %s", attrs_set)
        self.log.info("  attributes from list-attr:  %s", attrs_list)
        self.assertEqual(attrs_set, attrs_list, "pool attrs from set-attr do not match list-attr")

    def verify_server_client_comm(self, pool):
        """Verify server/client communication.

        Args:
            pool (TestPool): pool to verify with.
        """
        self.get_dmg_command().pool_list(verbose=True)
        self.get_dmg_command().pool_query(pool=pool.identifier)
        self.daos_cmd.pool_query(pool=pool.identifier)

    def show_daos_version(self, all_hosts, hosts_client):
        """show daos version

        Args:
            all_hosts (NodeSet): all hosts.
            hosts_client (NodeSet): client hosts to show daos and dmg version.
        """
        result = run_remote(
            self.log, all_hosts, 'rpm -qa | grep -E "daos-|libfabric|mercury" | sort')
        if not result.passed:
            self.fail("Failed to check daos RPMs")
        if not run_remote(self.log, NodeSet(hosts_client[0]), "dmg version").passed:
            self.fail("Failed to check dmg version")
        if not run_remote(self.log, hosts_client, "daos version").passed:
            self.fail("Failed to check daos version")

    def verify_rpms(self):
        """Verify RPMs are available for self.old_version and self.new_version.
           Verify self.old_version is installed.
        """
        base_packages = [
            'daos', 'daos-admin', 'daos-client', 'daos-client-tests',
            'daos-server', 'daos-tests']
        old_packages = [
            f'{package}-{self.old_version}'
            for package in base_packages]
        new_packages = [
            f'{package}-{self.new_version}'
            for package in base_packages]
        all_hosts = self.hostlist_clients | self.hostlist_servers
        command = command_as_user(f'dnf list {" ".join(old_packages)}', 'root')
        if not run_remote(self.log, all_hosts, command).passed:
            self.fail('Old RPMs not available on all nodes')
        command = command_as_user(f'dnf list {" ".join(new_packages)}', 'root')
        if not run_remote(self.log, all_hosts, command).passed:
            self.fail('New RPMs not available on all nodes')
        command = command_as_user(f'dnf list installed {" ".join(old_packages)}', 'root')
        if not run_remote(self.log, all_hosts, command).passed:
            self.fail('Old RPMs are not installed on all nodes')

    def install_daos(self, version, servers, clients):
        """Install a version of DAOS.

        Args:
            version (str): the version to install
            servers (NodeSet): servers to install on
            clients (NodeSet): clients to install on
        """
        # Stop servers and/or agents first
        if servers:
            self.log.info('Stopping servers before installing version %s', version)
        if clients:
            self.log.info('Stopping agents before installing version %s', version)
        self.system_stop(servers=bool(servers), agents=bool(clients))

        self.log.info('RPMs before installing version %s', version)
        cmd = 'rpm -qa | grep "daos-" | sort'
        all_nodes = servers or NodeSet() | clients or NodeSet()
        if not run_remote(self.log, all_nodes, cmd).passed:
            self.fail('Failed to show current RPMs')

        # Install DAOS client packages on each node.
        # Install ior on just client nodes.
        # TODO install only server packages on server and same for clients
        packages = [
            f'{package}-{version}'
            for package in ['daos-client-tests']]

        # Make sure the installed libfabric matches the shipped versions
        # TODO need a more proper and generic solution
        #      MAYBE "dnf --repo=daos-packages-2.2.0 list --available"
        #      MAYBE python "dnf" module
        # TODO this doesn't work because of libfabric incompatibility
        # if Version(version) == '2.2.0':
        #     packages.append('libfabric-1.15.1-3.el8')
        # elif Version(version) == '2.4.1':
        #     packages.append('libfabric-1.19.0-1.el8')

        server_packages = packages.copy()
        client_packages = packages + ['ior']

        if servers:
            self.log.info("Installing version %s on servers, %s", version, servers)
            if not install_packages(self.log, servers, server_packages, 'root').passed:
                self.fail(f"Failed to install version {version} on servers")
            self.current_server_version = Version(version)
            result = run_remote(self.log, NodeSet(servers[0]), 'dmg version')
            if not result.passed:
                self.fail(f"dmg version failed after installing server version {version}")
            if not result.homogeneous:
                self.fail(f"dmg version inconsistent after installing server version {version}")
            self.log.info("Successfully installed version %s on servers", version)

        # Install on clients
        if clients:
            self.log.info("Installing version %s on clients, %s", version, clients)
            if not install_packages(self.log, clients, client_packages, 'root').passed:
                self.fail(f"Failed to install version {version} on clients")
            self.current_client_version = Version(version)
            result = run_remote(self.log, clients, 'daos version')
            if not result.passed:
                self.fail(f"daos version failed after installing client version {version}")
            if not result.homogeneous:
                self.fail(f"daos version inconsistent after installing client version {version}")
            self.log.info("Successfully installed version %s on clients", version)

        # Restart servers and/or agents
        if servers:
            self.log.info("Restarting servers after installing %s", version)
            # TODO try DaosServerManager.restart
            errors = self.restart_servers(stop=False)
            if errors:
                report_errors(self, errors)
            self.log.info("Sleeping 5 seconds after restarting servers")
            time.sleep(5)

        if clients:
            self.log.info('Restarting agents after installing %s', version)
            self._start_manager_list("agent", self.agent_managers)

        self.log.info("Sleeping 30 seconds after restarting servers/agents")
        time.sleep(30)

    def verify_daos_libdaos(self, step, hosts, cmd, positive_test, agent_server_ver, exp_err=None):
        """Verify daos and libdaos interoperability between different version of agent and server.

        Args:
            step (str): test step for logging.
            hosts (NodeSet): hosts to run command on.
            cmd (str): command to run.
            positive_test (bool): True for positive test, false for negative test.
            agent_server_ver (str): agent and server version.
            exp_err (str, optional): expected error message for negative testcase.
                Defaults to None.
        """
        if positive_test:
            self.log.info("==(%s)Positive_test: %s, on %s", step, cmd, agent_server_ver)
        else:
            self.log.info("==(%s)Negative_test: %s, on %s", step, cmd, agent_server_ver)
        result = run_remote(self.log, hosts, cmd)
        if positive_test:
            if not result.passed:
                self.fail("##({0})Test failed, {1}, on {2}".format(step, cmd, agent_server_ver))
        else:
            if result.passed_hosts:
                self.fail("##({0})Test failed, {1}, on {2}".format(step, cmd, agent_server_ver))
            for stdout in result.all_stdout.values():
                if exp_err not in stdout:
                    self.fail("##({0})Test failed, {1}, on {2}, expect_err {3} "
                              "not shown on stdout".format(step, cmd, agent_server_ver, exp_err))

        self.log.info("==(%s)Test passed, %s, on %s", step, cmd, agent_server_ver)

    def has_fault_injection(self, hosts):
        """Check if RPMs with fault-injection function.

        Args:
            hosts (string, list): client hosts to execute the command.

        Returns:
            bool: whether RPMs have fault-injection.
        """
        result = run_remote(self.log, hosts, "daos_debug_set_params -v 67174515")
        if not result.passed:
            self.fail("Failed to check if fault-injection is enabled")
        for stdout in result.all_stdout.values():
            if not stdout.strip():
                return True
        self.log.info("#Host client rpms did not have fault-injection")
        return False

    def wait_for_pool_upgrade(self, pool, status, fail_on_status=None, timeout=300):
        """Wait for the pool upgrade status.

        Args:
            pool (TestPool): pool to wait for upgrade.
            status (list): pool upgrade expected status(es).
            fail_on_status (list, optional): status(es) to fail on if found. Defaults to None.
            timeout (int, optional): seconds to wait for upgrade. Defaults to 300.

        """
        self.log.info('Waiting for %s upgrade status to be %s', str(pool), status)

        start = time.time()
        while True:
            actual_status = pool.get_prop("upgrade_status")['response'][0]['value']
            if actual_status in status:
                break
            if fail_on_status and actual_status in fail_on_status:
                self.fail(f'pool upgrade status is {actual_status}; expected {status}')
            if time.time() - start > timeout:
                self.fail(
                    f'TIMEOUT detected after {timeout} seconds waiting for pool upgrade status '
                    f'to be {status}')
            time.sleep(3)

    def pool_upgrade(self, pool, with_fault=False):
        """Run and verify dmg pool upgrade.

        Args:
            pool (TestPool): pool to be upgraded
            with_fault (bool): whether to use and verify fault injection
        """
        # Make sure an upgrade isn't already in progress
        self.wait_for_pool_upgrade(pool, ["not started", "completed"], fail_on_status=["failed"])

        self.log.info('Check the layout version before upgrading')
        response = self.get_dmg_command().pool_query(pool=pool.identifier)['response']
        pre_pool_layout_ver = int(response['pool_layout_ver'])
        pre_upgrade_layout_ver = int(response['upgrade_layout_ver'])
        if pre_pool_layout_ver == pre_upgrade_layout_ver:
            self.log.info('Pool does not need upgrade. Skipping.')
            return

        if with_fault:
            # Stop a rank right before upgrade
            self.get_dmg_command().system_stop(force=True, ranks="1")

        # Pool upgrade
        self.pool.upgrade()

        if with_fault:
            # Upgrade should fail because a rank is stopped/stopping
            self.wait_for_pool_upgrade(pool, ["failed"], fail_on_status=["completed"])
            # Restart the rank
            self.get_dmg_command().system_start(ranks="1")
            self.log.info("Sleeping for 30 seconds after system start")
            time.sleep(30)

        # Verify pool upgrade completed
        self.wait_for_pool_upgrade(pool, ["completed"], fail_on_status=["failed"])

        self.log.info('Sleep 5 seconds after upgrade is complete')
        time.sleep(5)
        self.log.info('Verify the layout version increased after upgrading')
        response = self.get_dmg_command().pool_query(pool=pool.identifier)['response']
        post_pool_layout_ver = int(response['pool_layout_ver'])
        if post_pool_layout_ver <= pre_pool_layout_ver:
            self.fail(
                'Expected pool_layout_ver to increase. '
                f'pre={pre_pool_layout_ver}, post={post_pool_layout_ver}')
        # TODO verify they are EQUAL

        # TODO also check [rebuild][state]

    def verify_write_read(self, clients, container, write=True, read=True):
        """Verify write and/or read with IOR.

        Args:
            clients (NodeSet): clients to run IOR on
            container (TestContainer): container to use
        """
        self.pool = container.pool
        self.container = container
        self.ior_cmd.set_daos_params(container.pool, container.identifier)
        ior_api = self.ior_cmd.api.value
        ior_timeout = self.params.get("ior_timeout", self.ior_cmd.namespace)
        ior_write_flags = self.params.get("write_flags", self.ior_cmd.namespace)
        ior_read_flags = self.params.get("read_flags", self.ior_cmd.namespace)
        testfile = None
        testfile_sav = None
        testfile_sav2 = None
        symlink_testfile = None

        # TODO get this from self.dfuse.mount_dir.value?
        # Start dfuse if needed
        if ior_api in ('POSIX', 'HDF5'):
            dfuse_mount_dir = '/tmp/daos_dfuse'
            if ior_api == 'HDF5':
                dfuse_mount_dir = os.path.join(dfuse_mount_dir, 'sub_path')
            if not run_remote(self.log, clients, f"mkdir -p {dfuse_mount_dir}").passed:
                self.fail("Failed to create dfuse mount directory")
            testfile = os.path.join(dfuse_mount_dir, "testfile")
            testfile_sav = os.path.join(dfuse_mount_dir, "testfile_sav")
            testfile_sav2 = os.path.join(dfuse_mount_dir, "testfile_sav2")
            symlink_testfile = os.path.join(dfuse_mount_dir, "symlink_testfile")
            self.log.info("Mounting dfuse")
            cmd = " ".join([
                "/usr/bin/dfuse",
                "--disable-caching",
                dfuse_mount_dir,
                container.pool.identifier,
                container.identifier
            ])
            if not run_remote(self.log, clients, cmd).passed:
                self.fail("Failed to mount dfuse")

            # HACK because ior test base hard-codes self.dfuse
            self.dfuse = Dfuse(clients, self.tmp, path=self.bin)
            self.dfuse.mount_dir.value = dfuse_mount_dir

        if ior_api in ("DFS", "POSIX"):
            if write:
                self.log.info("Running IOR write - %s", ior_api)
                self.ior_cmd.update_params(flags=ior_write_flags)
                self.run_ior_with_pool(
                    timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False,
                    display_space=False)
            if read:
                self.log.info("Running IOR read - %s", ior_api)
                self.ior_cmd.update_params(flags=ior_read_flags)
                self.run_ior_with_pool(
                    timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False,
                    display_space=False)

        elif ior_api == "HDF5":
            hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/')
            if write:
                self.log.info("Running IOR write - %s", ior_api)
                self.ior_cmd.update_params(flags=ior_write_flags)
                self.run_ior_with_pool(
                    plugin_path=hdf5_plugin_path, mount_dir=dfuse_mount_dir,
                    timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)
            if read:
                self.log.info("Running IOR read - %s", ior_api)
                self.ior_cmd.update_params(flags=ior_read_flags)
                self.run_ior_with_pool(
                    plugin_path=hdf5_plugin_path, mount_dir=dfuse_mount_dir,
                    timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)

        else:
            self.fail("##(3)Unsupported IOR api {}".format(ior_api))

        # Additional symlink test for POSIX/dfuse
        if ior_api == "POSIX":
            if write:
                self.log.info("Verifying dfuse symlink create")
                cmd_list = [
                    f"cd '{dfuse_mount_dir}'",
                    f"ls -l '{testfile}'",
                    f"cp '{testfile}' '{testfile_sav}'",
                    f"cp '{testfile}' '{testfile_sav2}'",
                    f"ln -vs '{testfile_sav2}' '{symlink_testfile}'"
                ]
                for cmd in cmd_list:
                    if not run_remote(self.log, self.first_client, cmd).passed:
                        self.fail("Failed to setup dfuse symlinks")
            if read:
                self.log.info("Verifying dfuse symlink read")
                cmd_list = [
                    f'diff "{testfile}" "{testfile_sav}"',
                    f'diff "{symlink_testfile}" "{testfile_sav2}"',
                    f'ls -l "{symlink_testfile}"'
                ]
                for cmd in cmd_list:
                    if not run_remote(self.log, self.first_client, cmd).passed:
                        self.fail("Failed to verify dfuse symlinks")

        # Cleanup dfuse
        if ior_api in ('POSIX', 'HDF5'):
            self.log.info("Unmounting dfuse")
            cmd = "fusermount3 -u {}".format(dfuse_mount_dir)
            if not run_remote(self.log, clients, cmd).passed:
                self.fail("Failed to unmount dfuse")
            if not run_remote(self.log, clients, f"rm -rf {dfuse_mount_dir}").passed:
                self.fail("Failed to remove dfuse mount directory")
            self.dfuse = None

    def upgrade_and_downgrade(self, fault_on_pool_upgrade=False):
        """upgrade and downgrade test base.

        Args:
            fault_on_pool_upgrade (bool): Enable fault-injection during pool upgrade.
        """
        do_test_diff_version_server_client = True
        self.log_step("Verify RPMs")
        self.verify_rpms()

        self.log_step("Setup and show rpm, dmg and daos versions on all hosts")
        hosts_client = self.hostlist_clients
        hosts_server = self.hostlist_servers
        all_hosts = include_local_host(hosts_server)
        num_attributes = self.params.get("num_attributes", '/run/attrtests/*')
        self.show_daos_version(all_hosts, hosts_client)

        self.log_step("Create pool with attributes - old client, old server")
        pool = self.pool = self.get_pool(connect=False)
        self.daos_cmd = self.get_daos_command()
        pool_attr_dict = self.create_attr_dict(num_attributes)
        self.pool_set_attr(pool, pool_attr_dict)

        self.log_step("Verify pool attributes - old client, old server, old pool")
        self.verify_pool_attrs(pool, pool_attr_dict)

        self.log_step("Verify IOR write/read - old client, old server, old pool, new data")
        cont1 = self.get_container(pool)
        self.verify_write_read(hosts_client, cont1, write=True, read=True)

        self.log_step(f"Upgrade DAOS clients to version {self.new_version}")
        self.install_daos(self.new_version, None, hosts_client)

        if do_test_diff_version_server_client:
            self.log_step("Verify client/server communication - new client, old server, old pool")
            # TODO these verify_server_client_comm are probably redundant.
            # # Maybe merge with the attr and prop check?
            self.verify_server_client_comm(pool)

            self.log_step("Verify IOR read - new client, old server, old pool, old data")
            self.verify_write_read(hosts_client, cont1, write=False, read=True)

            self.log_step("Verify IOR write/read - new client, old server, old pool, new data")
            tmp_cont = self.get_container(pool)
            self.verify_write_read(hosts_client, tmp_cont, write=True, read=True)
            tmp_cont.destroy()

        self.log_step(f"Upgrade DAOS servers to version {self.new_version}")
        self.install_daos(self.new_version, hosts_server, None)

        self.show_daos_version(all_hosts, hosts_client)

        self.log_step("Verify client/server communication - new client, new server, old pool")
        self.verify_server_client_comm(pool)

        self.log_step("Verify pool attributes - new client, new server, old pool")
        self.verify_pool_attrs(pool, pool_attr_dict)

        self.log_step("Verify dmg pool get-prop - new client, new server, old pool")
        pool.get_prop()

        self.log_step("Verify IOR read - new client, new server, old pool, old data")
        self.verify_write_read(hosts_client, cont1, write=False, read=True)

        self.log_step("Verify IOR write/read - new client, new server, old pool, new data")
        tmp_cont = self.get_container(pool)
        self.verify_write_read(hosts_client, tmp_cont, write=True, read=True)
        tmp_cont.destroy()

        if do_test_diff_version_server_client:
            self.log_step(f"Downgrade DAOS clients to version {self.old_version}")
            self.install_daos(self.old_version, None, hosts_client)

            self.log_step("Verify client/server communication - old client, new server, old pool")
            self.verify_server_client_comm(pool)

            self.log_step("Verify IOR read - old client, new server, old pool, old data")
            self.verify_write_read(hosts_client, cont1, write=False, read=True)

            self.log_step("Verify IOR write/read - old client, new server, old pool, new data")
            tmp_cont = self.get_container(pool)
            self.verify_write_read(hosts_client, tmp_cont, write=True, read=True)
            tmp_cont.destroy()

            # Only verify with a major version change. Minor version change should work here
            if self.current_server_version.major > self.current_client_version.major:
                self.log_step("Verify old client cannot access new pool")
                tmp_pool = self.get_pool(connect=False)
                try:
                    self.daos_cmd.pool_query(pool=tmp_pool.identifier)
                except CommandFailure as error:
                    if 'DER_NOTSUPPORTED' not in str(error):
                        self.fail(
                            'daos pool query expected to fail with DER_NOTSUPPORTED for '
                            f'v{self.current_client_version} client, '
                            f'v{self.current_server_version} pool')
                else:
                    self.fail(
                        'daos pool query expected to fail with DER_NOTSUPPORTED for '
                        f'v{self.current_client_version} client, '
                        f'v{self.current_server_version} pool')
                finally:
                    tmp_pool.destroy()

            self.log_step(f"Upgrade DAOS clients to version {self.new_version}")
            self.install_daos(self.new_version, None, hosts_client)

        if fault_on_pool_upgrade and self.has_fault_injection(hosts_client):
            self.log_step("Verify dmg pool upgrade with fault-injection - new server, old pool")
            self.pool_upgrade(pool, with_fault=True)
        else:
            self.log_step("Verify dmg pool upgrade - new server, old pool")
            self.pool_upgrade(pool)

        self.log_step("Verify dmg pool get-prop - new server, upgraded pool")
        pool.get_prop()

        self.log_step("Verify client/server communication - new client, new server, upgraded pool")
        self.verify_server_client_comm(pool)

        self.log_step("Verify pool attributes - new server, upgraded pool")
        self.verify_pool_attrs(pool, pool_attr_dict)

        self.log_step("Verify IOR read - new client, new server, upgraded pool, old data")
        self.verify_write_read(hosts_client, cont1, write=False, read=True)

        self.log_step("Verify IOR write/read - new client, new server, upgraded pool, new data")
        tmp_cont = self.get_container(pool)
        self.verify_write_read(hosts_client, tmp_cont, write=True, read=True)
        tmp_cont.destroy()

        self.log_step("Destroy current pool and container")
        cont1.destroy()
        pool.destroy(force=True)

        self.log_step("Create a new pool after server upgrade")
        pool = self.pool = self.get_pool(connect=False)

        self.log_step("Verify dmg pool get-prop - new server, new pool")
        pool.get_prop()

        self.log_step("Verify client/server communication - new client, new server, new pool")
        self.verify_server_client_comm(pool)

        self.log_step("Verify IOR write/read - new client, new server, new pool, new data")
        tmp_cont = self.get_container(pool)
        self.verify_write_read(hosts_client, tmp_cont, write=True, read=True)
        tmp_cont.destroy()

        self.log_step("Destroy current pool")
        pool.destroy(force=True)

        self.log_step(f"Downgrade DAOS to {self.old_version}")
        self.install_daos(self.old_version, hosts_server, hosts_client)

        if fault_on_pool_upgrade and not self.has_fault_injection(hosts_client):
            self.fail("Upgraded-rpms did not have fault-injection feature.")

        self.log.info("Test passed")


# TODO remote dmg should eventually be the default behavior in the framework
class RemoteDmgCommand(DmgCommand):
    """A DmgCommand that runs on a remote host."""

    def __init__(self, host, temporary_file, path, yaml_cfg=None, hostlist_suffix=None):
        """Override to accept a host argument.

        Args:
            host (NodeSet): a single host to run dmg on
            path (str): path to the dmg command
            yaml_cfg (DmgYamlParameters, optional): dmg config file
                settings. Defaults to None, in which case settings
                must be supplied as command-line parameters.
            hostlist_suffix (str, optional): Suffix to append to each host name. Defaults to None.
        """
        super().__init__(path, yaml_cfg, hostlist_suffix)
        # Have to include localhost because another dmg command in the framework expects
        # the config to already be created on localhost
        self.temporary_file_hosts = include_local_host(host)
        self.temporary_file = temporary_file
        self._run_host = host

    def run(self, raise_exception=None):
        """Run the command on a remote host.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception
                setting if defined. Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        if self.yaml:
            self.create_yaml_file()

        result = run_remote(
            self.log, self._run_host, self.with_exports, self.verbose, self.timeout, stderr=True)

        # Convert the RemoteCommandResult to a CmdResult for compatibility
        output = result.output[0]
        self.result = CmdResult(
            command=output.command,
            stdout='\n'.join(output.stdout),
            stderr='',
            exit_status=output.returncode)
        self.result.interrupted = output.timeout

        # Handle exceptions similar to base class
        if not result.passed and raise_exception:
            if output.timeout:
                error = "Timeout detected running '{}' with a {}s timeout".format(
                    self.command, self.timeout)
            else:
                error = "Error occurred running '{}'".format(self.command)
            raise CommandFailure(error)

        if raise_exception and not self.check_results():
            # Command failed if its output contains bad keywords
            raise CommandFailure(
                "<{}> command failed: Error messages detected in output".format(self.command))

        return self.result
