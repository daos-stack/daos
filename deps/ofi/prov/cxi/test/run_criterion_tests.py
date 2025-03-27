#!/usr/bin/env python3

"""
Executes criterion tests using parameters defined in an 
input yaml file and outputs results to a file in TAP format
"""

import pexpect
import sys
import yaml
import re

from enum import Enum
from argparse import ArgumentParser
from contextlib import ExitStack


class Result(Enum):
    """
    Enum for test results
    """
    PASSED = 1
    FAILED = 2
    SKIPPED = 3


class Node:
    """
    Class for managing a node's SSH connection
    """
    def __init__(self, node_name):
        """
        Args:
            node_name: the name of the node
        """
        self.name = node_name
        self.ssh = open_ssh(self.name, prompt=PROMPT)
        self.ssh.logfile = sys.stdout


class TestSet:
    """
    Set of tests that use common runtime parameters and CSR settings
    """
    def __init__(self, desc, test_filter=None, runtime_params=None, csr_list=None):
        """
        Args:
            desc: description of the test set
            test_filter: tests to run
            runtime_params: runtime parameters to use with this test set
            csr_list: csrs to set prior to running the tests
        """
        self.description = desc
        self.filter = test_filter
        self.runtime_params = runtime_params
        self.csr_list = csr_list
        self.csr_list_initial_vals = []
        self.test_list = []

        # generate the list of tests
        self.generate_testlist()

    def set_csrs_for_test_set(self):
        """
        capture original csr values and set csrs to new values

        """
        if self.csr_list is not None:
            send(node, "cd {}".format(PYCXI_DIR))

            for c in self.csr_list:
                csr = c[0]
                field = c[1]
                new_value = c[2]

                # capture initial values
                orig_val = get_csr_value(csr, field)
                self.csr_list_initial_vals.append([csr, field, orig_val])

                # set new values
                set_csr_value(csr, field, new_value)

    def restore_csrs(self):
        """
        Restore csrs to their original values

        """
        if self.csr_list is not None:
            print("Restoring CSRs...")
            send(node, "cd {}".format(PYCXI_DIR))

            for c in self.csr_list_initial_vals:
                field = c[1]
                if isinstance(c[2], dict):
                    # csr is an array, so set each member of the array
                    for csr, orig_value in c[2].items():
                        set_csr_value(csr, field, orig_value)
                else:
                    csr = c[0]
                    orig_value = c[2]
                    set_csr_value(csr, field, orig_value)

    def generate_testlist(self):
        """
        generate a list of tests to run based on the provided filter

        """
        send(node, "cd {}".format(TEST_DIR))

        # create criterion test list
        send(node, './cxitest -l > testlist 2>&1 && sleep 1 && echo "DONE"', resp_1="DONE")
        f_name = "{}/testlist".format(TEST_DIR)
        with open(f_name) as file:
            all_lines = [line.rstrip() for line in file.readlines()]

        # create regex instance for filter (if needed)
        regex_filter = None
        if self.filter is not None:
            regex_filter = re.compile(self.filter, re.IGNORECASE)

        # parse testlist and create test objects for this test set
        area = None
        for line in all_lines:
            if ':' in line:
                area = line.split()[0].replace(":", "")
            else:
                tst_name = line.split()[1]

                # indicates "disabled" flag was set in Criterion test
                skip_test = "skipped" in line

                # create test objects for this test set based on the filter (if provided)
                if self.filter is None or (
                        regex_filter is not None and regex_filter.match("{}/{}".format(area, tst_name))):
                    tst = Test(area, tst_name, self.description, self.runtime_params, skip=skip_test)
                    self.test_list.append(tst)

    def execute_tests(self):
        """
        Executes the tests in the test set and capture the output
        """
        with ExitStack() as cleanup:

            # restore CSRs on exit
            cleanup.callback(self.restore_csrs)

            # set CSRs for test set
            self.set_csrs_for_test_set()

            send(node, "cd {}".format(TEST_DIR))

            # execute tests in test list
            for te in self.test_list:
                sys.stdout.flush()
                cmd = '{} > tmp_result 2>&1 && echo "DONE"'.format(te.test_cmd)

                # execute test
                send(node, cmd, resp_1="DONE", timeout=60)

                results_index = 0
                enable_logging = False

                # process raw results file
                with open("{}/tmp_result".format(TEST_DIR)) as file:
                    all_lines = [line.strip() for line in file.readlines()]

                # capture all output related to this test
                for ln in all_lines:
                    line = ansi_escape.sub('', ln).rstrip()
                    test_str = " {}::{}".format(te.test_area, te.test_name)
                    if test_str in line:
                        if "RUN" in line and line.endswith(test_str):
                            # start capturing output for this test
                            enable_logging = True

                            # create a TestResult instance for this test
                            te.results.append(TestResult(results_index))

                            # if CSRs were modified, include that in the log:
                            if self.csr_list is not None:
                                te.results[results_index].log.append("Modified CSRs: {}".format(self.csr_list))

                            # log the actual Criterion test command
                            te.results[results_index].log.append("Test cmd: {}".format(te.test_cmd))

                            # log the "RUN" output
                            te.results[results_index].log.append(line)
                        elif "{}:".format(test_str) in line:

                            # set the test result
                            te.results[results_index].result = get_result(line)

                            # capture the entire result line
                            te.results[results_index].log.append(line)

                            # the test is finished, so stop capturing output for this test
                            enable_logging = False

                            # increment index (multiple results for Test instance indicates a parameterized tests)
                            results_index += 1

                    elif enable_logging:
                        # test is in process, so capture all console output that occurs
                        te.results[results_index].log.append(line)

                # display all logged output belonging to this particular test
                for res in te.results:
                    print("\n-------------------------------------------------------")
                    for s in res.log:
                        print(s)
                    print("-------------------------------------------------------\n")
    

class Test:
    """
    An individual test, which may contain multiple TestResult objects if the test is parameterized
    """
    def __init__(self, test_area, test_name, desc, t_params=None, skip=False):
        """
        Args:
            test_area: the test area
            test_name: the test name
            desc: description of the test
            t_params: runtime parameters for this test
            skip: flag to indicate if the test should be skipped
        """
        self.test_area = test_area
        self.test_name = test_name
        self.desc = desc
        self.skip = skip
        self.results = []

        # create the runtime parameters string for this test
        param_str = ""
        if t_params is not None:
            for pa, v in t_params.items():
                param_str += "{}={} ".format(pa, v)

        self.test_params = param_str

        # create the test cmd
        self.test_cmd = \
            '{} ./cxitest --filter="{}/{}" --verbose=1 -j1 --ascii'.format(param_str, test_area, test_name)

        # create TestResult for skipped test
        if self.skip:
            st = TestResult()
            st.result = Result.SKIPPED
            self.results.append(st)

    def create_tap_results(self):
        """
        Parse results log and create TAP results for this test
        """
        for res in self.results:
            # get test number for this test
            test_num = get_current_test_count_and_inc()

            # determine TAP result based on test result
            tap_result = "ok {}".format(test_num) if res.result != Result.FAILED else "not ok {}".format(test_num)

            # construct the TAP test name
            t_name = "{}::{}".format(self.test_area, self.test_name)

            # if we have a parameterized test, append index to the test name
            if len(self.results) > 1:
                t_name = "{}::{}".format(t_name, res.index)

            # append the description
            t_name = "{} - {}".format(t_name, self.desc)

            # if test was skipped, include skip comment
            if res.result == Result.SKIPPED:
                t_name = "{} # skip".format(t_name)

                # include additional comment for disabled tests
                if self.skip:
                    t_name += " Disabled flag set in criterion test "

            # append the tap result and test name to the tap report
            tap_report.append("{} {}".format(tap_result, t_name))

            # include all logged output during this test in the tap report
            for m in res.log:
                tap_report.append("# {}".format(m))


class TestResult:
    """
    Result and log for a particular test
    """
    def __init__(self, index=0):
        """

        Args:
            index: test index - used with parameterized tests
        """
        self.index = index
        self.result = Result.FAILED
        self.log = []


def get_result(the_line):
    """
    Determine the test result from the given line

    Args:
        the_line: the line to check

    Returns: the result

    """
    if "PASS" in the_line:
        return Result.PASSED
    elif "SKIP" in the_line:
        return Result.SKIPPED
    else:
        return Result.FAILED


def set_csr_value(csr, field, value):
    """
    Sets a CSR field to the given value

    Args:
        csr: the CSR
        field: the field
        value: the value

    """
    # use cxiutil to set the value
    send(node, "cd {}".format(PYCXI_DIR))
    cmd = 'cxiutil store csr {} {}={} && sleep 1 && echo "DONE"'.format(csr, field, value)
    send(node, cmd, resp_1="DONE")
    sys.stdout.flush()

    # verify the new value is set as expected
    new_val = get_csr_value(csr, field)
    if isinstance(new_val, dict):
        # we have a CSR array, so verify each member of the array
        for v in new_val.values():
            if int(v) != int(value):
                raise RuntimeError("Unable to set CSR with cmd: {}. "
                                   "Actual value of {} = {}".format(cmd, field, v))
    else:
        if int(new_val) != int(value):
            raise RuntimeError("Unable to set CSR with cmd: {}. "
                               "Actual value of {} = {}".format(cmd, field, new_val))


def get_csr_value(csr, field):
    """
    Returns the value of the CSR field. If the CSR is an array, returns a dict containing each CSR index and value

    Args:
        csr: the CSR
        field: the field

    Returns: the value, or a dict containing each CSR index and value

    """

    # use cxiutil to get the value
    send(node, "cd {}".format(PYCXI_DIR))
    sys.stdout.flush()
    send(node, 'cxiutil dump csr {} > tmp && sleep 1 && echo "DONE"'.format(csr, field), resp_1="DONE")

    with open("{}/tmp".format(PYCXI_DIR)) as file:
        all_lines = [line.rstrip() for line in file.readlines()]

    # parse the cxiutil output
    response = {}
    for line in all_lines:
        if "hex" in line:
            csr = line.split()[0]

        if field in line and "0x" in line:
            response[csr] = line.split()[2]

    # csr array, so return a dict containing each value in the csr array
    if len(response) > 1:
        return response
    # not a csr array, so just return the value
    elif len(response) == 1:
        return response[csr]
    else:
        raise RuntimeError("Unable to read CSR {} {}".format(csr, field))


def generate_tap_file():
    """
    generate the TAP results file
    """
    total_test_count = 0

    # capture the total number of tests
    for ts in test_set_list:
        for tst in ts.test_list:
            total_test_count += len(tst.results)

    # add TAP header line
    tap_header = "1..{}".format(total_test_count)
    tap_report.append(tap_header)

    # capture TAP results of each test
    for ts in test_set_list:
        for element in ts.test_list:
            element.create_tap_results()

    # create TAP file
    with open(RESULTS_FILE, 'w') as file_handler:
        for tap_line in tap_report:
            file_handler.write("{}\n".format(tap_line))
            print(tap_line)


def get_current_test_count_and_inc():
    """
    returns the current test count prior to incrementing it

    Returns: the current test count

    """
    global current_test_count
    tmp_count = current_test_count
    current_test_count += 1
    return tmp_count


def open_ssh(node_addr, prompt):
    """
    Create ssh connection to the given ip address

    Args:
        node_addr: the node name / ip address
        prompt: the prompt to expect

    Returns: SSH connection / process

    """
    s = pexpect.spawn("ssh {}".format(node_addr), encoding='utf-8')
    try:
        rc = s.expect([prompt, "Password:"], timeout=30)
        if rc == 1:
            s.sendline(PASSWORD)
            s.expect(prompt, timeout=10)
    except pexpect.TIMEOUT:
        print("Unable to ssh to {}".format(node_addr))
        raise pexpect.TIMEOUT
    return s


def send(the_node, cmd, resp_1=None, resp_2=None, expect_prompt=True, timeout=30):
    """
    send a command to the given node and verify expected response(s)

    Args:
        the_node: the node
        cmd: the command to send
        resp_1: the first expected response (if not None)
        resp_2: the second expected response (if not None)
        expect_prompt: flag to indicate if a prompt is expected
        timeout: the maximum time to wait for a response before throwing an exception
    """
    ssh_sesh = the_node.ssh
    ssh_sesh.sendline(cmd)

    if resp_1:
        ssh_sesh.expect(resp_1, timeout=timeout)

    if resp_2:
        ssh_sesh.expect(resp_2, timeout=timeout)

    if expect_prompt:
        ssh_sesh.expect(PROMPT, timeout=timeout)


if __name__ == "__main__":

    # used to filter ansi escape chars
    ansi_escape = re.compile(r'(?:\x1B[@-_]|[\x80-\x9F])[0-?]*[ -/]*[@-~]')

    p = ArgumentParser("run_criterion_tests")
    p.add_argument('-n',
                   dest="node",
                   nargs='?',
                   type=str,
                   required=True,
                   help="Name of node where test is to be run")

    p.add_argument('-y',
                   dest="yaml_file",
                   nargs='?',
                   type=str,
                   required=True,
                   help="Path to the test YAML file")

    args = p.parse_args()

    # parse the input yaml file
    try:
        with open(args.yaml_file, 'r') as stream:
            f = yaml.safe_load(stream)

    except FileNotFoundError:
        print("YAML file not found: {}".format(args.yaml_file))

    LIBFABRIC_DIR = f["env"]["libfabric_dir_on_node"]
    TEST_DIR = "{}/prov/cxi/test".format(LIBFABRIC_DIR)
    PYCXI_DIR = f["env"]["pycxi_dir_on_node"]
    PROMPT = f["env"]["node_prompt"]
    PASSWORD = f["env"]["node_password"]
    RESULTS_FILE = "{}/results.tap".format(TEST_DIR)

    # holds all TAP results
    tap_report = []

    # instantiate node object
    node = Node(args.node)

    # activate pycxi venv for cxiutil and remove old tap files
    send(node, "cd {}".format(PYCXI_DIR))
    send(node, ". .venv/bin/activate")
    send(node, "cd {}".format(TEST_DIR))
    send(node, "rm *.tap")

    # set global runtime parameters prior to running tests
    default_runtime_parameters = f["global_runtime_parameters"]
    for params in default_runtime_parameters:
        for param, val in params.items():
            send(node, "export {}={}".format(param, val))

    current_test_count = 1

    # create test sets
    test_set_list = []
    for test in f["tests"]:
        test_set_list.append(TestSet(
            desc=test["description"],
            test_filter=test["filter"],
            runtime_params=test["runtime_parameters"],
            csr_list=test["csrs"])
        )

    # execute the tests in each test set
    for test_set in test_set_list:
        test_set.execute_tests()

    # generate the tap file
    generate_tap_file()

