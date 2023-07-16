"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from collections import OrderedDict, defaultdict
import errno
import json
from logging import getLogger
import os
import site

from ClusterShell.NodeSet import NodeSet

from run_utils import run_remote

PROVIDER_KEYS = OrderedDict(
    [
        ("cxi", "ofi+cxi"),
        ("verbs", "ofi+verbs"),
        ("ucx", "ucx+dc_x"),
        ("tcp", "ofi+tcp;ofi_rxm"),
        ("opx", "ofi+opx"),
    ]
)

# Temporary pipeline-lib workaround until DAOS-13934 is implemented
PROVIDER_ALIAS = {
    "ofi+tcp": "ofi+tcp;ofi_rxm"
}


class TestEnvironmentException(Exception):
    """Exception for launch.py execution."""


def get_build_environment():
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Raises:
        TestEnvironmentException: if there is an error obtaining the DAOS build environment

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    build_vars_file = os.path.join(
        os.path.dirname(os.path.realpath(__file__)), "..", "..", ".build_vars.json")
    try:
        with open(build_vars_file, encoding="utf-8") as vars_file:
            return json.load(vars_file)

    except ValueError as error:
        raise TestEnvironmentException("Error obtaining build environment:", str(error)) from error

    except IOError as error:
        if error.errno == errno.ENOENT:
            raise TestEnvironmentException(
                "Error obtaining build environment:", str(error)) from error

    return json.loads(f'{{"PREFIX": "{os.getcwd()}"}}')


def set_path():
    """Set the PATH environment variable for functional testing.

    Raises:
        TestEnvironmentException: if there is an error obtaining the DAOS build environment
    """
    base_dir = get_build_environment()["PREFIX"]
    bin_dir = os.path.join(base_dir, "bin")
    sbin_dir = os.path.join(base_dir, "sbin")

    # /usr/sbin is not setup on non-root user for CI nodes.
    # SCM formatting tool mkfs.ext4 is located under /usr/sbin directory.
    usr_sbin = os.path.join(os.sep, "usr", "sbin")
    path = os.environ.get("PATH")

    # Update PATH
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])


def set_python_environment():
    """Set up the test python environment.

    Args:
        log (logger): logger for the messages produced by this method
    """
    log = getLogger()
    log.debug("-" * 80)
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
        os.path.abspath("cart"),
    ]

    # Include the cart directory paths when running from sources
    for cart_dir in os.listdir(os.path.abspath("cart")):
        cart_path = os.path.join(os.path.abspath("cart"), cart_dir)
        if os.path.isdir(cart_path):
            required_python_paths.append(cart_path)

    required_python_paths.extend(site.getsitepackages())

    # Check the PYTHONPATH env definition
    python_path = os.environ.get("PYTHONPATH")
    if python_path is None or python_path == "":
        # Use the required paths to define the PYTHONPATH env if it is not set
        os.environ["PYTHONPATH"] = ":".join(required_python_paths)
    else:
        # Append any missing required paths to the existing PYTHONPATH env
        defined_python_paths = [
            os.path.abspath(os.path.expanduser(path))
            for path in python_path.split(":")]
        for required_path in required_python_paths:
            if required_path not in defined_python_paths:
                python_path += ":" + required_path
        os.environ["PYTHONPATH"] = python_path
    log.debug("Testing with PYTHONPATH=%s", os.environ["PYTHONPATH"])


def log_environment():
    """Log the current environment variable assignments."""
    log = getLogger()
    log.debug("ENVIRONMENT VARIABLES")
    for key in sorted(os.environ):
        if not key.startswith("BASH_FUNC_"):
            log.debug("  %s: %s", key, os.environ[key])


def get_available_interfaces(hosts):
    # pylint: disable=too-many-nested-blocks,too-many-branches,too-many-locals
    """Get a dictionary of active available interfaces and their speeds.

    Args:
        hosts (NodeSet): hosts on which to find a homogeneous interface

    Raises:
        TestEnvironmentException: if there is a problem finding active network interfaces

    Returns:
        dict: a dictionary of speeds with the first available active interface providing that
            speed

    """
    log = getLogger()
    available_interfaces = {}

    # Find any active network interfaces on the server or client hosts
    net_path = os.path.join(os.path.sep, "sys", "class", "net")
    operstate = os.path.join(net_path, "*", "operstate")
    command = [f"grep -l 'up' {operstate}", "grep -Ev '/(lo|bonding_masters)/'", "sort"]

    result = run_remote(hosts, " | ".join(command))
    if not result.passed:
        raise TestEnvironmentException(
            f"Error obtaining a default interface on {str(hosts)} from {net_path}")

    # Populate a dictionary of active interfaces with a NodSet of hosts on which it was found
    active_interfaces = {}
    for data in result.output:
        for line in data.stdout:
            try:
                interface = line.split("/")[-2]
                if interface not in active_interfaces:
                    active_interfaces[interface] = data.hosts
                else:
                    active_interfaces[interface].update(data.hosts)
            except IndexError:
                pass

    # From the active interface dictionary find all the interfaces that are common to all hosts
    log.debug("Active network interfaces detected:")
    common_interfaces = []
    for interface, node_set in active_interfaces.items():
        log.debug("  - %-8s on %s (Common=%s)", interface, node_set, node_set == hosts)
        if node_set == hosts:
            common_interfaces.append(interface)

    # Find the speed of each common active interface in order to be able to choose the fastest
    interface_speeds = {}
    for interface in common_interfaces:
        # Check for a virtual interface
        module_path = os.path.join(net_path, interface, "device", "driver", "module")
        command = [f"readlink {module_path}", "grep 'virtio_net'"]
        result = run_remote(hosts, " | ".join(command))
        if result.passed and result.homogeneous:
            interface_speeds[interface] = 1000
            continue

        # Verify each host has the same speed for non-virtual interfaces
        command = " ".join(["cat", os.path.join(net_path, interface, "speed")])
        result = run_remote(hosts, command)
        if result.passed and result.homogeneous:
            for line in result.output[0].stdout:
                try:
                    interface_speeds[interface] = int(line.strip())
                except ValueError:
                    # Any line not containing a speed (integer)
                    pass
        elif not result.homogeneous:
            log.error("Non-homogeneous interface speed detected for %s on %s.", interface, hosts)
        else:
            log.error("Error detecting speed of %s on %s", interface, hosts)

    if interface_speeds:
        log.debug("Active network interface speeds on %s:", hosts)

    for interface, speed in interface_speeds.items():
        log.debug("  - %-8s (speed: %6s)", interface, speed)
        # Only include the first active interface for each speed - first is
        # determined by an alphabetic sort: ib0 will be checked before ib1
        if speed is not None and speed not in available_interfaces:
            available_interfaces[speed] = interface

    log.debug("Available interfaces on %s: %s", hosts, available_interfaces)
    return available_interfaces


class TestEnvironment():
    """Collection of test environment variables."""

    def __init__(self):
        """Initialize a TestEnvironment object.

        This will set any unset test environment variables with default values.  Unless already set
        externally the interface and provider values will not be set.  Use the set_with_hosts()
        method to define default values for these test environment variables with host information.

        When tests are run through launch.py it will have called set_with_hosts() and a subsequent
        invocation of this class will read the values set via launch.py.
        """
        self.__env_map = OrderedDict()
        self.__env_map['log_dir'] = {
            'name': 'DAOS_TEST_LOG_DIR',
            'method': self.__default_log_dir
        }
        self.__env_map['shared_dir'] = {
            'name': 'DAOS_TEST_SHARED_DIR',
            'method': self.__default_shared_dir
        }
        self.__env_map['app_dir'] = {
            'name': 'DAOS_TEST_APP_DIR',
            'method': self.__default_app_dir
        }
        self.__env_map['user_dir'] = {
            'name': 'DAOS_TEST_USER_DIR',
            'method': self.__default_user_dir
        }
        self.__env_map['interface'] = {
            'name': 'DAOS_TEST_FABRIC_IFACE',
            'method': self.__default_interface,
            'kwargs': {'hosts': None}
        }
        self.__env_map['provider'] = {
            'name': 'CRT_PHY_ADDR_STR',
            'method': self.__default_provider,
            'kwargs': {'hosts': None}
        }
        self.__env_map['bullseye_src'] = {
            'name': 'DAOS_TEST_BULLSEYE_SRC',
            'method': self.__default_bullseye_src
        }
        self.__env_map['bullseye_file'] = {
            'name': 'COVFILE',
            'method': self.__default_bullseye_file
        }
        self.__set_environment()

    @property
    def app_dir(self):
        """Get the application directory path.

        Returns:
            str: the application directory path
        """
        return os.environ[self.__env_map['app_dir']['name']]

    @property
    def log_dir(self):
        """Get the local log directory path.

        Returns:
            str: the local log directory path
        """
        return os.environ[self.__env_map['log_dir']['name']]

    @property
    def shared_dir(self):
        """Get the shared log directory path.

        Returns:
            str: the shared log directory path
        """
        return os.environ[self.__env_map['shared_dir']['name']]

    @property
    def user_dir(self):
        """Get the user directory path.

        Returns:
            str: the user directory path
        """
        return os.environ[self.__env_map['log_dir']['name']]

    @property
    def interface(self):
        """Get the interface device.

        Returns:
            str: the interface device
        """
        return os.environ[self.__env_map['interface']['name']]

    @property
    def provider(self):
        """Get the provider.

        Returns:
            str: the provider
        """
        return os.environ[self.__env_map['provider']['name']]

    @provider.setter
    def provider(self, value):
        """Set the provider.

        Args:
            value (str): value to assign to the provider environment variable
        """
        alias = PROVIDER_ALIAS.get(value)
        if alias:
            os.environ[self.__env_map['provider']['name']] = alias
        elif value:
            os.environ[self.__env_map['provider']['name']] = value

    @property
    def bullseye_src(self):
        """Get the bullseye source file.

        Returns:
            str: the bullseye source file
        """
        return os.environ[self.__env_map['bullseye_src']['name']]

    @property
    def bullseye_file(self):
        """Get the bullseye file.

        Returns:
            str: the bullseye file
        """
        return os.environ[self.__env_map['bullseye_file']['name']]

    def __default_app_dir(self):
        """Get the default application directory path.

        Returns:
            str: the default application directory path
        """
        return os.path.join(
            os.environ[self.__env_map['shared_dir']['env_name']], "daos_test", "apps")

    @staticmethod
    def __default_log_dir():
        """Get the default local log directory path.

        Returns:
            str: the default local log directory path
        """
        return os.path.join(os.sep, "var", "tmp", "daos_testing")

    @staticmethod
    def __default_shared_dir():
        """Get the default shared log directory path.

        Returns:
            str: the default shared log directory path
        """
        return os.path.expanduser(os.path.join("~", "daos_test"))

    @staticmethod
    def __default_user_dir():
        """Get the default user directory path.

        Returns:
            str: the default user directory path
        """
        return os.path.join(os.sep, "var", "tmp", "daos_testing", "user")

    def __default_interface(self, hosts):
        """Get the default interface.

        Args:
            hosts (NodeSet): hosts on which to find a homogeneous interface

        Raises:
            TestEnvironmentException: if there is a problem finding active network interfaces when
                hosts are provided

        Returns:
            str: the default interface; can be None
        """
        interface = os.environ.get("OFI_INTERFACE")
        if interface is None and hosts:
            # Find all the /sys/class/net interfaces on the launch node (excluding lo)
            log = getLogger()
            log.debug("Detecting network devices - OFI_INTERFACE not set")
            available_interfaces = get_available_interfaces(hosts)
            try:
                # Select the fastest active interface available by sorting
                # the speed
                interface = available_interfaces[sorted(available_interfaces)[-1]]
            except IndexError as error:
                raise TestEnvironmentException("Error obtaining a default interface!") from error
        return interface

    def __default_provider(self, hosts):
        """Get the default provider.

        Args:
            hosts (NodeSet): hosts on which to find a homogeneous provider

        Raises:
            TestEnvironmentException: if there is an error obtaining the default provider when hosts
                are provided

        Returns:
            str: the default provider; can be None
        """
        if not hosts:
            return None

        log = getLogger()
        log.debug(
            "Detecting provider for %s - %s not set",
            self.interface, self.__env_map['provider']['name'])

        # Check for a Omni-Path interface
        log.debug("Checking for Omni-Path devices")
        command = "sudo -n opainfo"
        result = run_remote(hosts, command)
        if result.passed:
            # Omni-Path adapter found; remove verbs as it will not work with OPA devices.
            log.debug("  Excluding verbs provider for Omni-Path adapters")
            PROVIDER_KEYS.pop("verbs")

        # Detect all supported providers
        command = f"fi_info -d {self.interface} -l | grep -v 'version:'"
        result = run_remote(hosts, command)
        if result.passed:
            # Find all supported providers
            keys_found = defaultdict(NodeSet)
            for data in result.output:
                for line in data.stdout:
                    provider_name = line.replace(":", "")
                    if provider_name in PROVIDER_KEYS:
                        keys_found[provider_name].update(data.hosts)

            # Only use providers available on all the server hosts
            if keys_found:
                log.debug("Detected supported providers:")
            provider_name_keys = list(keys_found)
            for provider_name in provider_name_keys:
                log.debug("  %4s: %s", provider_name, str(keys_found[provider_name]))
                if keys_found[provider_name] != hosts:
                    keys_found.pop(provider_name)

            # Select the preferred found provider based upon PROVIDER_KEYS order
            log.debug("Supported providers detected: %s", list(keys_found))
            for key in PROVIDER_KEYS:
                if key in keys_found:
                    provider = PROVIDER_KEYS[key]
                    break

        # Report an error if a provider cannot be found
        if not provider:
            raise TestEnvironmentException(
                f"Error obtaining a supported provider for {self.interface} "
                f"from: {list(PROVIDER_KEYS)}")

        log.debug("  Found %s provider for %s", provider, self.interface)
        return provider

    @staticmethod
    def __default_bullseye_src():
        """Get the default bullseye source file.

        Returns:
            str: the default bullseye source file
        """
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), "test.cov")

    @staticmethod
    def __default_bullseye_file():
        """Get the default bullseye file.

        Returns:
            str: the default bullseye file
        """
        return os.path.join(os.sep, "tmp", "test.cov")

    def __set_environment(self):
        """Set the test environment.

        If a value is not set for the environment variable name set a default value
        """
        for value in self.__env_map.values():
            if value['name'] not in os.environ:
                if value.get('kwargs', None):
                    env_value = value['method'](**value['kwargs'])
                else:
                    env_value = value['method']()
                if env_value is not None:
                    os.environ[value['name']] = env_value

    def set_with_hosts(self, servers, clients):
        """Set the test environment with host information.

        If a value does not exist for the interface and provider environment variable names it will
        use the host information to determine a default value.

        Args:
            servers (NodeSet): hosts to be used in testing as a potential server
            clients (NodeSet): hosts to be used in testing as a potential client

        Raises:
            TestEnvironmentException: if there is an error obtaining a default value to set
        """
        hosts = NodeSet()
        hosts.update(servers)
        hosts.update(clients)
        self.__env_map['interface']['kwargs']['hosts'] = hosts
        self.__env_map['provider']['kwargs']['hosts'] = servers
        self.__set_environment()


def set_test_environment(test_env, servers=None, clients=None, provider=None, insecure_mode=False,
                         details=None):
    """Set up the test environment.

    Args:
        test_env (TestEnvironment, optional): the current test environment. Defaults to None.
        servers (NodeSet, optional): hosts designated for the server role in testing. Defaults to
            None.
        clients (NodeSet, optional): hosts designated for the client role in testing. Defaults to
            None.
        provider (str, optional): provider to use in testing. Defaults to None.
        insecure_mode (bool, optional): whether or not to run tests in insecure mode. Defaults to
            False.
        details (dict, optional): dictionary to update with interface and provider settings if
            provided. Defaults to None.

    Raises:
        TestEnvironmentException: if there is a problem setting up the test environment

    """
    set_path()

    if test_env:
        # Get the default fabric interface and provider
        test_env.provider(provider)
        test_env.set_with_hosts(servers, clients)

        log = getLogger()
        log.info("Testing with interface:   %s", test_env.interface)
        log.info("Testing with provider:    %s", test_env.provider)

        if details:
            details["interface"] = test_env.interface
            details["provider"] = test_env.provider

        # Assign additional DAOS environment variables used in functional testing
        os.environ["D_LOG_FILE"] = os.path.join(test_env.log_dir, "daos.log")
        os.environ["D_LOG_FILE_APPEND_PID"] = "1"
        os.environ["DAOS_INSECURE_MODE"] = str(insecure_mode)
        os.environ["CRT_CTX_SHARE_ADDR"] = "0"

    # Python paths required for functional testing
    set_python_environment()

    # Log the environment variable assignments
    log_environment()
