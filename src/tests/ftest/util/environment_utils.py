"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
import os
import site

from ClusterShell.NodeSet import NodeSet
# pylint: disable=import-error,no-name-in-module
from util.network_utils import (PROVIDER_ALIAS, SUPPORTED_PROVIDERS, NetworkException,
                                get_common_provider, get_fastest_interface)
from util.run_utils import run_remote


class TestEnvironmentException(Exception):
    """Exception for launch.py execution."""


def get_build_environment(logger, build_vars_file):
    """Obtain DAOS build environment variables from the .build_vars.json file.

    Args:
        logger (Logger): logger for the messages produced by this method
        build_vars_file (str): the full path to the DAOS build_vars.json file

    Raises:
        TestEnvironmentException: if there is an error obtaining the DAOS build environment

    Returns:
        dict: a dictionary of DAOS build environment variable names and values

    """
    logger.debug("Obtaining DAOS build environment from %s", build_vars_file)
    try:
        with open(build_vars_file, encoding="utf-8") as vars_file:
            return json.load(vars_file)

    except Exception as error:      # pylint: disable=broad-except
        raise TestEnvironmentException("Error obtaining build environment:", str(error)) from error


def update_path(logger, build_vars_file):
    """Update the PATH environment variable for functional testing.

    Args:
        logger (Logger): logger for the messages produced by this method
        build_vars_file (str): the full path to the DAOS build_vars.json file

    Raises:
        TestEnvironmentException: if there is an error obtaining the DAOS build environment
    """
    base_dir = get_build_environment(logger, build_vars_file)["PREFIX"]
    bin_dir = os.path.join(base_dir, "bin")
    sbin_dir = os.path.join(base_dir, "sbin")

    # /usr/sbin is not setup on non-root user for CI nodes.
    # SCM formatting tool mkfs.ext4 is located under /usr/sbin directory.
    usr_sbin = os.path.join(os.sep, "usr", "sbin")
    path = os.environ.get("PATH")

    # Update PATH
    os.environ["PATH"] = ":".join([bin_dir, sbin_dir, usr_sbin, path])


def set_python_environment(logger):
    """Set up the test python environment.

    Args:
        logger (Logger): logger for the messages produced by this method
    """
    logger.debug("-" * 80)
    required_python_paths = [
        os.path.abspath("util/apricot"),
        os.path.abspath("util"),
        os.path.abspath("cart"),
        os.path.abspath("."),
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
    logger.debug("Testing with PYTHONPATH=%s", os.environ["PYTHONPATH"])


def log_environment(logger):
    """Log the current environment variable assignments.

    Args:
        logger (Logger): logger for the messages produced by this method
    """
    logger.debug("ENVIRONMENT VARIABLES")
    for key in sorted(os.environ):
        if not key.startswith("BASH_FUNC_"):
            logger.debug("  %s: %s", key, os.environ[key])


class TestEnvironment():
    """Collection of test environment variables."""

    __ENV_VAR_MAP = {
        'app_dir': 'DAOS_TEST_APP_DIR',
        'app_src': 'DAOS_TEST_APP_SRC',
        'log_dir': 'DAOS_TEST_LOG_DIR',
        'shared_dir': 'DAOS_TEST_SHARED_DIR',
        'user_dir': 'DAOS_TEST_USER_DIR',
        'interface': 'DAOS_TEST_FABRIC_IFACE',
        'provider': 'CRT_PHY_ADDR_STR',
        'insecure_mode': 'DAOS_TEST_INSECURE_MODE',
        'bullseye_src': 'DAOS_TEST_BULLSEYE_SRC',
        'bullseye_file': 'COVFILE',
    }

    def __init__(self):
        """Initialize a TestEnvironment object with existing or default test environment values."""
        self.set_defaults(None)

    def set_defaults(self, logger, servers=None, clients=None, provider=None, insecure_mode=None):
        """Set the default test environment variable values with optional inputs.

        Args:
            logger (Logger): logger for the messages produced by this method
            servers (NodeSet, optional): hosts designated for the server role in testing. Defaults
                to None.
            clients (NodeSet, optional): hosts designated for the client role in testing. Defaults
                to None.
            provider (str, optional): provider to use in testing. Defaults to None.
            insecure_mode (bool, optional): whether or not to run tests in insecure mode. Defaults
                to None.

        Raises:
            TestEnvironmentException: if there are any issues setting environment variable default
                values
        """
        all_hosts = NodeSet()
        all_hosts.update(servers)
        all_hosts.update(clients)
        if self.log_dir is None:
            self.log_dir = self.default_log_dir()
        if self.shared_dir is None:
            self.shared_dir = self.default_shared_dir()
        if self.app_dir is None:
            self.app_dir = self.default_app_dir()
        if self.user_dir is None:
            self.user_dir = self.default_user_dir()
        if self.interface is None:
            self.interface = self.default_interface(logger, all_hosts)
        if self.provider is None:
            self.provider = provider
        if self.provider is None:
            self.provider = self.default_provider(logger, servers)
        if self.insecure_mode is None:
            self.insecure_mode = insecure_mode
        if self.insecure_mode is None:
            self.insecure_mode = self.default_insecure_mode()
        if self.bullseye_src is None:
            self.bullseye_src = self.default_bullseye_src()
        if self.bullseye_file is None:
            self.bullseye_file = self.default_bullseye_file()

    def __set_value(self, key, value):
        """Set the test environment variable.

        Args:
            key (str): test environment variable name
            value (str): value to set; None to clear
        """
        if value is not None:
            os.environ[self.__ENV_VAR_MAP[key]] = str(value)
        elif os.environ.get(self.__ENV_VAR_MAP[key]):
            os.environ.pop(self.__ENV_VAR_MAP[key])

    @property
    def app_dir(self):
        """Get the application directory path.

        Returns:
            str: the application directory path
        """
        return os.environ.get(self.__ENV_VAR_MAP['app_dir'])

    @app_dir.setter
    def app_dir(self, value):
        """Set the application directory path.

        Args:
            value (str): the application directory path
        """
        self.__set_value('app_dir', value)

    def default_app_dir(self):
        """Get the default application directory path.

        Returns:
            str: the default application directory path
        """
        return os.path.join(self.shared_dir, "daos_test", "apps")

    @property
    def app_src(self):
        """Get the location from which to copy test applications.

        Returns:
            str: the location from which to copy test applications
        """
        return os.environ.get(self.__ENV_VAR_MAP['app_src'])

    @app_src.setter
    def app_src(self, value):
        """Set the location from which to copy test applications.

        Args:
            value (str): the location from which to copy test applications
        """
        self.__set_value('app_src', value)

    @property
    def log_dir(self):
        """Get the local log directory path.

        Returns:
            str: the local log directory path
        """
        return os.environ.get(self.__ENV_VAR_MAP['log_dir'])

    @log_dir.setter
    def log_dir(self, value):
        """Set the local log directory path.

        Args:
            value (str): the local log directory path
        """
        self.__set_value('log_dir', value)

    @staticmethod
    def default_log_dir():
        """Get the default local log directory path.

        Returns:
            str: the default local log directory path
        """
        return os.path.join(os.sep, "var", "tmp", "daos_testing")

    @property
    def shared_dir(self):
        """Get the shared log directory path.

        Returns:
            str: the shared log directory path
        """
        return os.environ.get(self.__ENV_VAR_MAP['shared_dir'])

    @shared_dir.setter
    def shared_dir(self, value):
        """Set the shared log directory path.

        Args:
            value (str): the shared log directory path
        """
        self.__set_value('shared_dir', value)

    @staticmethod
    def default_shared_dir():
        """Get the default shared log directory path.

        Returns:
            str: the default shared log directory path
        """
        return os.path.expanduser(os.path.join("~", "daos_test"))

    @property
    def user_dir(self):
        """Get the user directory path.

        Returns:
            str: the user directory path
        """
        return os.environ.get(self.__ENV_VAR_MAP['user_dir'])

    @user_dir.setter
    def user_dir(self, value):
        """Set the user directory path.

        Args:
            value (str): the user directory path
        """
        self.__set_value('user_dir', value)

    def default_user_dir(self):
        """Get the default user directory path.

        Returns:
            str: the default user directory path
        """
        return os.path.join(self.log_dir, "user")

    @property
    def interface(self):
        """Get the interface device.

        Returns:
            str: the interface device
        """
        return os.environ.get(self.__ENV_VAR_MAP['interface'])

    @interface.setter
    def interface(self, value):
        """Set the interface device.

        Args:
            value (str): the interface device
        """
        self.__set_value('interface', value)

    def default_interface(self, logger, hosts):
        """Get the default interface.

        Args:
            logger (Logger): logger for the messages produced by this method
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
            logger.debug("Detecting network devices - OFI_INTERFACE not set")
            try:
                interface = get_fastest_interface(logger, hosts)
            except NetworkException as error:
                raise TestEnvironmentException("Error obtaining a default interface!") from error
        return interface

    @property
    def provider(self):
        """Get the provider.

        Returns:
            str: the provider
        """
        return os.environ.get(self.__ENV_VAR_MAP['provider'])

    @provider.setter
    def provider(self, value):
        """Set the provider.

        Args:
            value (str): the provider or provider alias
        """
        alias = PROVIDER_ALIAS.get(value, None)
        if alias is not None:
            self.__set_value('provider', alias)
        else:
            self.__set_value('provider', value)

    def default_provider(self, logger, hosts):
        """Get the default provider.

        Args:
            logger (Logger): logger for the messages produced by this method
            hosts (NodeSet): hosts on which to find a homogeneous provider

        Raises:
            TestEnvironmentException: if there is an error obtaining the default provider when hosts
                are provided

        Returns:
            str: the default provider; can be None
        """
        if not hosts:
            return None

        logger.debug(
            "Detecting provider for %s - %s not set",
            self.interface, self.__ENV_VAR_MAP['provider'])
        provider = None
        supported = list(SUPPORTED_PROVIDERS)

        # Check for a Omni-Path interface
        logger.debug("Checking for Omni-Path devices")
        command = "sudo -n opainfo"
        result = run_remote(logger, hosts, command)
        if result.passed:
            # Omni-Path adapter found; remove verbs as it will not work with OPA devices.
            logger.debug("  Excluding verbs provider for Omni-Path adapters")
            supported = list(filter(lambda x: 'verbs' not in x, supported))

        # Detect all supported providers for this interface that are common to all of the hosts
        common_providers = get_common_provider(logger, hosts, self.interface, supported)
        if common_providers:
            # Select the preferred found provider based upon SUPPORTED_PROVIDERS order
            logger.debug("Supported providers detected: %s", common_providers)
            for key in supported:
                if key in common_providers:
                    provider = key
                    break

        # Report an error if a provider cannot be found
        if not provider:
            raise TestEnvironmentException(
                f"Error obtaining a supported provider for {self.interface} from: {supported}")

        logger.debug("  Found %s provider for %s", provider, self.interface)
        return provider

    @property
    def insecure_mode(self):
        """Get the insecure_mode.

        Returns:
            str: the insecure_mode
        """
        return os.environ.get(self.__ENV_VAR_MAP['interface'])

    @insecure_mode.setter
    def insecure_mode(self, value):
        """Set the insecure_mode.

        Args:
            value (str, bool): the insecure mode
        """
        self.__set_value('insecure_mode', value)

    @staticmethod
    def default_insecure_mode():
        """Get the default insecure mode.

        Returns:
            str: the default insecure mode
        """
        return "True"

    @property
    def bullseye_src(self):
        """Get the bullseye source file.

        Returns:
            str: the bullseye source file
        """
        return os.environ.get(self.__ENV_VAR_MAP['bullseye_src'])

    @bullseye_src.setter
    def bullseye_src(self, value):
        """Set the bullseye source file.

        Args:
            value (str, bool): the bullseye source file
        """
        self.__set_value('bullseye_src', value)

    @staticmethod
    def default_bullseye_src():
        """Get the default bullseye source file.

        Returns:
            str: the default bullseye source file
        """
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), "test.cov")

    @property
    def bullseye_file(self):
        """Get the bullseye file.

        Returns:
            str: the bullseye file
        """
        return os.environ.get(self.__ENV_VAR_MAP['bullseye_file'])

    @bullseye_file.setter
    def bullseye_file(self, value):
        """Set the bullseye file.

        Args:
            value (str, bool): the bullseye file
        """
        self.__set_value('bullseye_file', value)

    @staticmethod
    def default_bullseye_file():
        """Get the default bullseye file.

        Returns:
            str: the default bullseye file
        """
        return os.path.join(os.sep, "tmp", "test.cov")


def set_test_environment(logger, test_env=None, servers=None, clients=None, provider=None,
                         insecure_mode=False, details=None):
    """Set up the test environment.

    Args:
        logger (Logger): logger for the messages produced by this method
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
    logger.debug("-" * 80)
    logger.debug("Setting up the test environment variables")

    if test_env:
        # Update the PATH environment variable
        build_vars_file = os.path.join(
            os.path.dirname(os.path.realpath(__file__)), "..", "..", "..", ".build_vars.json")
        update_path(logger, build_vars_file)

        # Get the default fabric interface and provider
        test_env.set_defaults(logger, servers, clients, provider, insecure_mode)
        logger.info("Testing with interface:   %s", test_env.interface)
        logger.info("Testing with provider:    %s", test_env.provider)

        if details:
            details["interface"] = test_env.interface
            details["provider"] = test_env.provider

        # Assign additional DAOS environment variables used in functional testing
        os.environ["D_LOG_FILE"] = os.path.join(test_env.log_dir, "daos.log")
        os.environ["D_LOG_FILE_APPEND_PID"] = "1"
        os.environ["D_LOG_FILE_APPEND_RANK"] = "1"
        os.environ["CRT_CTX_SHARE_ADDR"] = "0"

    # Python paths required for functional testing
    set_python_environment(logger)

    # Log the environment variable assignments
    log_environment(logger)
