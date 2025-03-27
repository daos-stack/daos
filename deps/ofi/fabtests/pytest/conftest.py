import builtins
import os
import re
import shlex

import yaml

import pytest


def get_option_longform(option_name, option_params):
    '''
        get the long form command line option name of an option
    '''
    return option_params.get("longform", "--" + option_name.replace("_", "-"))

def pytest_addoption(parser):
    parser.addoption("--provider", dest="provider", help="libfabric provider")
    parser.addoption("--client-id", dest="client_id", help="client IP address or hostname")
    parser.addoption("--server-id", dest="server_id", help="server IP address or hostname")

    options = yaml.safe_load(open("options.yaml"))
    for option_name in options.keys():
        option_params = options[option_name]
        option_longform = get_option_longform(option_name, option_params)
        option_type = option_params["type"]
        option_helpmsg = option_params["help"]
        option_default = option_params.get("default")
        if option_type == "int" and not (option_default is None):
            option_default = int(option_default)

        if option_type == "bool" or option_type == "boolean":
            parser.addoption(option_longform, dest=option_name, action="store_true",
                             help=option_helpmsg, default=option_default)
        else:
            assert option_type == "str" or option_type == "int"
            parser.addoption(option_longform, dest=option_name, type=getattr(builtins, option_type),
                             help=option_helpmsg, default=option_default)

# base ssh command
bssh = "ssh -n -o StrictHostKeyChecking=no -o ConnectTimeout=30 -o BatchMode=yes"

class CmdlineArgs:

    def __init__(self, request):
        self.provider = request.config.getoption("--provider")
        if self.provider is None:
            raise RuntimeError("Error: libfabric provider is not specified")

        self.server_id = request.config.getoption("--server-id")
        if self.server_id is None:
            raise RuntimeError("Error: server is not specified")

        self.client_id = request.config.getoption("--client-id")
        if self.client_id is None:
            raise RuntimeError("Error: client is not specified")

        options = yaml.safe_load(open("options.yaml"))
        for option_name in options.keys():
            option_params = options[option_name]
            option_longform = get_option_longform(option_name, option_params)
            setattr(self, option_name, request.config.getoption(option_longform))

        self._exclusion_patterns = []
        if self.exclusion_list:
            self._add_exclusion_patterns_from_list(self.exclusion_list)

        if self.exclusion_file:
            self._add_exclusion_patterns_from_file(self.exclusion_file)

        if self.client_interface is None:
            self.client_interface = self.client_id

        if self.server_interface is None:
            self.server_interface = self.server_id

    def append_environ(self, environ):
        if self.environments:
            self.environments += " " + environ
        else:
            self.environments = environ[:]

    def populate_command(self, base_command, host_type, timeout=None, additional_environment=None):
        '''
            populate base command with informations in command line: provider, environments, etc
        '''
        assert host_type in ("host", "server", "client")

        command = base_command
        # use binpath if specified
        if not (self.binpath is None):
            command = self.binpath + "/" + command

        if timeout is None:
            timeout = self.timeout

        # set environment variables if specified
        if not (self.environments is None):
            command = self.environments + " " + command

        if additional_environment:
            command = additional_environment + " " + command

        if command.find("fi_ubertest") != -1:
            command = self._populate_ubertest_command(command, host_type)
        elif command.find("fi_multinode") != -1:
            command = self._populate_multinode_command(command, host_type)
        else:
            command = self._populate_normal_command(command, host_type)

        host_id = self.client_id if host_type == "client" else self.server_id

        command = f"timeout {timeout} /bin/bash --login -c {shlex.quote(command)}"
        command = f"{bssh} {host_id} {shlex.quote(command)}"

        return command

    def is_test_excluded(self, test_base_command, test_is_negative=False):
        if test_is_negative and self.exclude_negative_tests:
            return True

        for pattern in self._exclusion_patterns:
            if pattern.search(test_base_command):
                return True

        return False

    def _add_exclusion_patterns_from_list(self, exclusion_list):
        pattern_strs = exclusion_list.split(",")
        for pattern_str in pattern_strs:
            self._exclusion_patterns.append(re.compile(pattern_str))

    def _add_exclusion_patterns_from_file(self, exclusion_file):
        
        ifs = open(exclusion_file)
        line = ifs.readline()
        while len(line) > 0:
            line = line.strip()
            if len(line)>0 and line[0] != '#':
                self._exclusion_patterns.append(re.compile(line))
            line = ifs.readline()

    def _populate_normal_command(self, command, host_type):
        # setup provider
        assert self.provider
        command = command + " -p " + self.provider

        if host_type == "host":
            return command

        if ("PYTEST_XDIST_WORKER" in os.environ) and (not self.oob_address_exchange):
            raise RuntimeError("Parallel run currently only supports OOB address exchange. "
                               "Please run runfabtests.py with -b option")

        if self.oob_address_exchange:
            oob_argument = "-E"
            if "PYTEST_XDIST_WORKER" in os.environ:
                oob_port = 9228 + int(os.environ["PYTEST_XDIST_WORKER"].replace("gw", ""))
                oob_argument += "={}".format(oob_port)

        if host_type == "server":
            if self.oob_address_exchange:
                command += " " + oob_argument
            else:
                command += " -s " + self.server_interface

            if self.additional_server_arguments:
                command += " " + self.additional_server_arguments

            return command

        assert host_type == "client"
        if self.oob_address_exchange:
            command += " " + oob_argument + " " + self.server_id
        else:
            command += " -s " + self.client_interface + " " + self.server_interface

        if self.additional_client_arguments:
            command += " " + self.additional_client_arguments

        return command

    def _populate_ubertest_command(self, command, host_type):
        assert command.find("ubertest") != -1
        if host_type == "server":
            return command + " -x"

        assert host_type == "client"
        assert self.ubertest_config_file
        assert self.server_id
        return command + " -u " + self.ubertest_config_file + " " + self.server_id

    def _populate_multinode_command(self, command, host_type):
        assert command.find("fi_multinode") != -1

        command += " -p " + self.provider

        command += " -s " + self.server_interface

        if self.additional_server_arguments:
                command += " " + self.additional_server_arguments

        return command

@pytest.fixture
def cmdline_args(request):
    return CmdlineArgs(request)

@pytest.fixture
def good_address(cmdline_args):

    if cmdline_args.good_address:
        return cmdline_args.good_address

    if "GOOD_ADDR" in os.environ:
        return os.environ["GOOD_ADDR"]

    if cmdline_args.server_interface:
        return cmdline_args.server_interface

    return cmdline_args.server_id

@pytest.fixture
def server_address(cmdline_args, good_address):
    if cmdline_args.oob_address_exchange:
        return good_address

    return cmdline_args.server_id

@pytest.fixture(scope="module", params=["transmit_complete", "delivery_complete"])
def completion_semantic(request):
    return request.param

@pytest.fixture(scope="module", params=["queue", "counter"])
def completion_type(request):
    return request.param

@pytest.fixture(scope="module", params=["with_prefix", "wout_prefix"])
def prefix_type(request):
    return request.param

@pytest.fixture(scope="module", params=["with_datacheck", "wout_datacheck"])
def datacheck_type(request):
    return request.param
