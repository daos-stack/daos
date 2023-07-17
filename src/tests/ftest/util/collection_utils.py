"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from collections import OrderedDict
from logging import getLogger
import glob
import os
import re
import sys

from ClusterShell.NodeSet import NodeSet

from host_utils import get_local_host
from process_core_files import CoreFileProcessing, CoreFileException
from run_utils import RunException, run_local, run_remote, find_command, stop_processes
from test_env_utils import TestEnvironment
from user_utils import get_chown_command
from yaml_utils import get_test_category

CLEANUP_PROCESS_NAMES = [
    "daos_server", "daos_engine", "daos_agent", "cart_ctl", "orterun", "mpirun", "dfuse"]
CLEANUP_UNMOUNT_TYPES = ["fuse.daos"]
FAILURE_TRIGGER = "00_trigger-launch-failure_00"
TEST_EXPECT_CORE_FILES = ["./harness/core_files.py"]
TEST_RESULTS_DIRS = (
    "daos_configs", "daos_logs", "cart_logs", "daos_dumps", "valgrind_logs", "stacktraces")


def stop_daos_agent_services(test):
    """Stop any daos_agent.service running on the hosts running servers.

    Args:
        test (TestInfo): the test information

    Returns:
        bool: True if the daos_agent.service was successfully stopped; False otherwise

    """
    log = getLogger()
    service = "daos_agent.service"
    # pylint: disable=unsupported-binary-operation
    hosts = test.host_info.clients.hosts | get_local_host()
    log.debug("-" * 80)
    log.debug("Verifying %s after running '%s'", service, test)
    return stop_service(hosts, service)


def stop_daos_server_service(test):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        test (TestInfo): the test information

    Returns:
        bool: True if the daos_server.service  was successfully stopped; False otherwise

    """
    log = getLogger()
    service = "daos_server.service"
    hosts = test.host_info.servers.hosts
    log.debug("-" * 80)
    log.debug("Verifying %s after running '%s'", service, test)
    return stop_service(hosts, service)


def stop_service(hosts, service):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        hosts (NodeSet): list of hosts on which to stop the service.
        service (str): name of the service

    Returns:
        bool: True if the service was successfully stopped; False otherwise

    """
    log = getLogger()
    result = {"status": True}
    if hosts:
        status_keys = ["reset-failed", "stop", "disable"]
        mapping = {"stop": "active", "disable": "enabled", "reset-failed": "failed"}
        check_hosts = NodeSet(hosts)
        loop = 1
        # Reduce 'max_loops' to 2 once https://jira.hpdd.intel.com/browse/DAOS-7809
        # has been resolved
        max_loops = 3
        while check_hosts:
            # Check the status of the service on each host
            result = get_service_status(check_hosts, service)
            check_hosts = NodeSet()
            for key in status_keys:
                if result[key]:
                    if loop == max_loops:
                        # Exit the while loop if the service is still running
                        log.error(" - Error %s still %s on %s", service, mapping[key], result[key])
                        result["status"] = False
                    else:
                        # Issue the appropriate systemctl command to remedy the
                        # detected state, e.g. 'stop' for 'active'.
                        command = ["sudo", "-n", "systemctl", key, service]
                        run_remote(result[key], " ".join(command))

                        # Run the status check again on this group of hosts
                        check_hosts.add(result[key])
            loop += 1
    else:
        log.debug("  Skipping stopping %s service - no hosts", service)

    return result["status"]


def get_service_status(hosts, service):
    """Get the status of the daos_server.service.

    Args:
        hosts (NodeSet): hosts on which to get the service state
        service (str): name of the service

    Returns:
        dict: a dictionary with the following keys:
            - "status":       boolean set to True if status was obtained; False otherwise
            - "stop":         NodeSet where to stop the daos_server.service
            - "disable":      NodeSet where to disable the daos_server.service
            - "reset-failed": NodeSet where to reset the daos_server.service

    """
    log = getLogger()
    status = {
        "status": True,
        "stop": NodeSet(),
        "disable": NodeSet(),
        "reset-failed": NodeSet()}
    status_states = {
        "stop": ["active", "activating", "deactivating"],
        "disable": ["active", "activating", "deactivating"],
        "reset-failed": ["failed"]}
    command = ["systemctl", "is-active", service]
    result = run_remote(hosts, " ".join(command))
    for data in result.output:
        if data.timeout:
            status["status"] = False
            status["stop"].add(data.hosts)
            status["disable"].add(data.hosts)
            status["reset-failed"].add(data.hosts)
            log.debug("  %s: TIMEOUT", data.hosts)
            break
        log.debug("  %s: %s", data.hosts, "\n".join(data.stdout))
        for key, state_list in status_states.items():
            for line in data.stdout:
                if line in state_list:
                    status[key].add(data.hosts)
                    break
    return status


def reset_server_storage(test):
    """Reset the server storage for the hosts that ran servers in the test.

    This is a workaround to enable binding devices back to nvme or vfio-pci after they are
    unbound from vfio-pci to nvme.  This should resolve the "NVMe not found" error seen when
    attempting to start daos engines in the test.

    Args:
        test (TestInfo): the test information

    Returns:
        bool: True if the service was successfully stopped; False otherwise

    """
    log = getLogger()
    log.debug("-" * 80)
    log.debug("Resetting server storage after running %s", test)
    hosts = test.host_info.servers.hosts
    if hosts:
        test_env = TestEnvironment()
        commands = [
            "if lspci | grep -i nvme",
            f"then export COVFILE={test_env.bullseye_file} && "
            "daos_server storage prepare -n --reset && "
            "sudo -n rmmod vfio_pci && sudo -n modprobe vfio_pci",
            "fi"]
        log.info("Resetting server storage on %s after running '%s'", hosts, test)
        result = run_remote(hosts, f"bash -c '{';'.join(commands)}'", timeout=600)
        if not result.passed:
            log.debug("Ignoring any errors from these workaround commands")
    else:
        log.debug("  Skipping resetting server storage - no server hosts")
    return True


def cleanup_processes(test, result):
    """Cleanup any processes left running on remote nodes.

    Args:
        test (TestInfo): the test information
        result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if nothing needed to be cleaned up; False otherwise
        int: status code: 0 = success; 4096 if processes were found

    """
    log = getLogger()
    any_found = False
    hosts = test.host_info.all_hosts
    log.debug("-" * 80)
    log.debug("Cleaning up running processes after running %s", test)

    proc_pattern = "|".join(CLEANUP_PROCESS_NAMES)
    log.debug("Looking for running processes: %s", proc_pattern)
    detected, running = stop_processes(hosts, f"'{proc_pattern}'", force=True)
    if running:
        message = f"Failed to kill processes on {running}"
        result.fail_test("Process", message)
    elif detected:
        message = f"Running processes found on {detected}"
        result.warn_test("Process", message)

    log.debug("Looking for mount types: %s", " ".join(CLEANUP_UNMOUNT_TYPES))
    # Use mount | grep instead of mount -t for better logging
    grep_pattern = "|".join(f'type {_type}' for _type in CLEANUP_UNMOUNT_TYPES)
    mount_grep_cmd = f"mount | grep -E '{grep_pattern}'"
    mount_grep_result = run_remote(hosts, mount_grep_cmd)
    if mount_grep_result.passed_hosts:
        any_found = True
        log.debug("Unmounting: %s", " ".join(CLEANUP_UNMOUNT_TYPES))
        type_list = ",".join(CLEANUP_UNMOUNT_TYPES)
        umount_cmd = f"sudo -n umount -v --all --force -t '{type_list}'"
        umount_result = run_remote(mount_grep_result.passed_hosts, umount_cmd)
        if umount_result.failed_hosts:
            message = f"Failed to unmount on {umount_result.failed_hosts}"
            result.fail_test("Process", message)
        else:
            message = f"Unexpected mounts on {mount_grep_result.passed_hosts}"
            result.warn_test("Process", message)

    return not any_found


def archive_files(summary, hosts, source, pattern, destination, depth, threshold, timeout,
                  test_result, test=None):
    """Archive the files from the source to the destination.

    Args:
        summary (str): description of the files being processed
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        destination (str): where the files should be moved to on this host
        depth (int): max depth for find command
        threshold (str): optional upper size limit for test log files
        timeout (int): number of seconds to wait for the command to complete.
        test_result (TestResult): the test result used to update the status of the test
        test (TestInfo, optional): the test information. Defaults to None.

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    log = getLogger()
    log.debug("=" * 80)
    log.info(
        "Archiving %s from %s:%s to %s%s,",
        summary, hosts, os.path.join(source, pattern), destination,
        f" after running '{str(test)}'" if test else "")
    log.debug("  Remote hosts: %s", hosts.difference(get_local_host()))
    log.debug("  Local host:   %s", hosts.intersection(get_local_host()))

    # List any remote files and their sizes and determine which hosts contain these files
    return_code, file_hosts = list_files(hosts, source, pattern, depth, test_result)
    if not file_hosts:
        # If no files are found then there is nothing else to do
        log.debug("No %s files found on %s", os.path.join(source, pattern), hosts)
        return return_code

    if "log" in pattern:
        # Remove any empty files
        return_code |= remove_empty_files(file_hosts, source, pattern, depth, test_result)

        # Report an error if any files sizes exceed the threshold
        if threshold is not None:
            return_code |= check_log_size(
                file_hosts, source, pattern, depth, threshold, test_result)

        # Run cart_logtest on log files
        return_code |= cart_log_test(file_hosts, source, pattern, depth, test_result)

    # Remove any empty files
    return_code |= remove_empty_files(file_hosts, source, pattern, depth, test_result)

    # Compress any files larger than 1 MB
    return_code |= compress_files(file_hosts, source, pattern, depth, test_result)

    # Move the test files to the test-results directory on this host
    return_code |= move_files(file_hosts, source, pattern, destination, depth, timeout, test_result)

    if test and "core files" in summary:
        # Process the core files
        return_code |= process_core_files(os.path.split(destination)[0], test, test_result)

    return return_code


def list_files(hosts, source, pattern, depth, test_result):
    """List the files in source with that match the pattern.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        tuple: a tuple containing:
            int: 0 = success, 16 = failure
            NodeSet: hosts with at least one file matching the pattern in the source directory

    """
    log = getLogger()
    status = 0
    hosts_with_files = NodeSet()
    source_files = os.path.join(source, pattern)
    log.debug("-" * 80)
    log.debug("Listing any %s files on %s", source_files, hosts)
    other = ["-printf", "'%M %n %-12u %-12g %12k %t %p\n'"]
    result = run_remote(hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = f"Error determining if {source_files} files exist on {hosts}"
        test_result.fail_test("Process", message)
        status = 16
    else:
        for data in result.output:
            for line in data.stdout:
                if source in line:
                    log.debug("Found at least one file match on %s: %s", data.hosts, line)
                    hosts_with_files.add(data.hosts)
                    break
            if re.findall(fr"{FAILURE_TRIGGER}", "\n".join(data.stdout)):
                # If a file is found containing the FAILURE_TRIGGER then report a failure.
                # This feature is used by avocado tests to verify that launch.py reports
                # errors correctly in CI. See test_launch_failures in harness/basic.py for
                # more details.
                log.debug(
                    "Found a file matching the '%s' failure trigger on %s",
                    FAILURE_TRIGGER, data.hosts)
                message = f"Error trigger failure file found in {source} (error handling test)"
                test_result.fail_test("Process", message)
                hosts_with_files.add(data.hosts)
                status = 16

    log.debug("List files results: status=%s, hosts_with_files=%s", status, hosts_with_files)
    return status, hosts_with_files


def check_log_size(hosts, source, pattern, depth, threshold, test_result):
    """Check if any file sizes exceed the threshold.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        threshold (str): optional upper size limit for test log files
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 32 = failure

    """
    log = getLogger()
    source_files = os.path.join(source, pattern)
    log.debug("-" * 80)
    log.debug(
        "Checking for any %s files exceeding %s on %s", source_files, threshold, hosts)
    other = ["-size", f"+{threshold}", "-printf", "'%p %k KB'"]
    result = run_remote(hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = f"Error checking for {source_files} files exceeding the {threshold} threshold"
        test_result.fail_test("Process", message)
        return 32

    # The command output will include the source path if the threshold has been exceeded
    for data in result.output:
        if source in "\n".join(data.stdout):
            message = f"One or more {source_files} files exceeded the {threshold} threshold"
            test_result.fail_test("Process", message)
            return 32

    log.debug("No %s file sizes found exceeding the %s threshold", source_files, threshold)
    return 0


def cart_log_test(hosts, source, pattern, depth, test_result):
    """Run cart_logtest on the log files.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    log = getLogger()
    source_files = os.path.join(source, pattern)
    cart_logtest = os.path.abspath(os.path.join("cart", "cart_logtest.py"))
    log.debug("-" * 80)
    log.debug("Running %s on %s files on %s", cart_logtest, source_files, hosts)
    other = ["-print0", "|", "xargs", "-0", "-r0", "-n1", "-I", "%", "sh", "-c",
             f"'{cart_logtest} % > %.cart_logtest 2>&1'"]
    result = run_remote(hosts, find_command(source, pattern, depth, other), timeout=4800)
    if not result.passed:
        message = f"Error running {cart_logtest} on the {source_files} files"
        test_result.fail_test("Process", message)
        return 16
    return 0


def remove_empty_files(hosts, source, pattern, depth, test_result):
    """Remove any files with zero size.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bint: status code: 0 = success, 16 = failure

    """
    log = getLogger()
    log.debug("-" * 80)
    log.debug("Removing any zero-length %s files in %s on %s", pattern, source, hosts)
    other = ["-empty", "-print", "-delete"]
    if not run_remote(hosts, find_command(source, pattern, depth, other)).passed:
        message = f"Error removing any zero-length {os.path.join(source, pattern)} files"
        test_result.fail_test("Process", message)
        return 16
    return 0


def compress_files(hosts, source, pattern, depth, test_result):
    """Compress any files larger than 1M.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    log = getLogger()
    log.debug("-" * 80)
    log.debug("Compressing any %s files in %s on %s larger than 1M", pattern, source, hosts)
    other = ["-size", "+1M", "-print0", "|", "sudo", "-n", "xargs", "-0", "-r0", "lbzip2", "-v"]
    result = run_remote(hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = f"Error compressing {os.path.join(source, pattern)} files larger than 1M"
        test_result.fail_test("Process", message)
        return 16
    return 0


def move_files(hosts, source, pattern, destination, depth, timeout, test_result):
    """Move files from the source to the destination.

    Args:
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        destination (str): where the files should be moved to on this host
        depth (int): max depth for find command
        timeout (int): number of seconds to wait for the command to complete.
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    log = getLogger()
    log.debug("-" * 80)
    log.debug("Moving files from %s to %s on %s", source, destination, hosts)

    # Core and dump files require a file ownership change before they can be copied
    if "stacktrace" in destination or "daos_dumps" in destination:
        # pylint: disable=import-outside-toplevel
        other = ["-print0", "|", "xargs", "-0", "-r0", "sudo", "-n", get_chown_command()]
        if not run_remote(hosts, find_command(source, pattern, depth, other)).passed:
            message = f"Error changing {os.path.join(source, pattern)} file permissions"
            test_result.fail_test("Process", message)
            return 16

    # Use the last directory in the destination path to create a temporary sub-directory on the
    # remote hosts in which all the source files matching the pattern will be copied. The entire
    # temporary sub-directory will then be copied back to this host and renamed as the original
    # destination directory plus the name of the host from which the files originated. Finally
    # delete this temporary sub-directory to remove the files from the remote hosts.
    rcopy_dest, tmp_copy_dir = os.path.split(destination)
    if source == os.path.join(os.sep, "etc", "daos"):
        # Use a temporary sub-directory in a directory where the user has permissions
        tmp_copy_dir = os.path.join(TestEnvironment().log_dir, tmp_copy_dir)
        sudo_command = "sudo -n "
    else:
        tmp_copy_dir = os.path.join(source, tmp_copy_dir)
        sudo_command = ""

    # Create a temporary remote directory - should already exist, see _setup_test_directory()
    command = f"mkdir -p '{tmp_copy_dir}'"
    if not run_remote(hosts, command).passed:
        message = f"Error creating temporary remote copy directory '{tmp_copy_dir}'"
        test_result.fail_test("Process", message)
        return 16

    # Move all the source files matching the pattern into the temporary remote directory
    other = f"-print0 | xargs -0 -r0 -I '{{}}' {sudo_command}mv '{{}}' '{tmp_copy_dir}'/"
    if not run_remote(hosts, find_command(source, pattern, depth, other)).passed:
        message = f"Error moving files to temporary remote copy directory '{tmp_copy_dir}'"
        test_result.fail_test("Process", message)
        return 16

    # Clush -rcopy the temporary remote directory to this host
    command = ["clush", "-w", str(hosts), "-pv", "--rcopy", f"'{tmp_copy_dir}'", "--dest",
               f"'{rcopy_dest}'"]
    return_code = 0
    try:
        run_local(" ".join(command), check=True, timeout=timeout)

    except RunException:
        message = f"Error copying remote files to {destination}"
        test_result.fail_test("Process", message, sys.exc_info())
        return_code = 16

    finally:
        # Remove the temporary remote directory on each host
        command = f"{sudo_command}rm -fr '{tmp_copy_dir}'"
        if not run_remote(hosts, command).passed:
            message = f"Error removing temporary remote copy directory '{tmp_copy_dir}'"
            test_result.fail_test("Process", message)
            return_code = 16

    return return_code


def process_core_files(test_job_results, test, test_result):
    """Generate a stacktrace for each core file detected.

    Args:
        test_job_results (str): the location of the core files
        test (TestInfo): the test information
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 2048 = Core file exist; 256 = failure; 0 = success

    """
    log = getLogger()
    core_file_processing = CoreFileProcessing()
    try:
        core_files_processed = core_file_processing.process_core_files(
            test_job_results, True, test=str(test))

    except CoreFileException:
        message = "Errors detected processing test core files"
        test_result.fail_test("Process", message, sys.exc_info())
        return 256

    except Exception:       # pylint: disable=broad-except
        message = "Unhandled error processing test core files"
        test_result.fail_test("Process", message, sys.exc_info())
        return 256

    if core_file_processing.is_el7() and str(test) in TEST_EXPECT_CORE_FILES:
        log.debug(
            "Skipping checking core file detection for %s as it is not supported on this OS",
            str(test))
        return 0

    if core_files_processed > 0 and str(test) not in TEST_EXPECT_CORE_FILES:
        message = "One or more core files detected after test execution"
        test_result.fail_test("Process", message)
        return 2048

    if core_files_processed == 0 and str(test) in TEST_EXPECT_CORE_FILES:
        message = "No core files detected when expected"
        test_result.fail_test("Process", message)
        return 256

    return 0


def rename_avocado_test_dir(test, job_results_dir, test_result, jenkins_xml, total_repeats):
    """Append the test name to its avocado job-results directory name.

    Args:
        test (TestInfo): the test information
        job_results_dir (str): path to the avocado job results
        test_result (TestResult): the test result used to update the status of the test
        jenkins_xml (bool): whether to update the results.xml with the Jenkins test names
        total_repeats (int): total number of times the test will be repeated

    Returns:
        int: status code: 0 = success, 1024 = failure

    """
    log = getLogger()
    log.debug("=" * 80)
    log.info("Renaming the avocado job-results directory")

    test_logs_lnk = os.path.join(job_results_dir, "latest")
    test_logs_dir = os.path.realpath(test_logs_lnk)

    # Create the new avocado job-results test directory name
    new_test_logs_dir = "-".join([test_logs_dir, get_test_category(test.test_file)])
    if jenkins_xml:
        new_test_logs_dir = os.path.join(job_results_dir, test.directory, test.python_file)
        if total_repeats > 1:
            # When repeating tests ensure Jenkins-style avocado log directories
            # are unique by including the repeat count in the path
            new_test_logs_dir = os.path.join(
                job_results_dir, test.directory, test.python_file, test.name.repeat_str)
        try:
            os.makedirs(new_test_logs_dir)
        except OSError:
            message = f"Error creating {new_test_logs_dir}"
            test_result.fail_test("Process", message, sys.exc_info())
            return 1024

    # Rename the avocado job-results test directory and update the 'latest' symlink
    log.info("Renaming test results from %s to %s", test_logs_dir, new_test_logs_dir)
    try:
        os.rename(test_logs_dir, new_test_logs_dir)
        os.remove(test_logs_lnk)
        os.symlink(new_test_logs_dir, test_logs_lnk)
        log.debug("Renamed %s to %s", test_logs_dir, new_test_logs_dir)
    except OSError:
        message = f"Error renaming {test_logs_dir} to {new_test_logs_dir}"
        test_result.fail_test("Process", message, sys.exc_info())
        return 1024

    # Update the results.xml file with the new functional test class name
    if jenkins_xml and not update_jenkins_xml(test, new_test_logs_dir, test_result):
        return 1024

    # Remove latest symlink directory to avoid inclusion in the Jenkins build artifacts
    try:
        run_local(f"rm -fr '{test_logs_lnk}'")
    except RunException:
        message = f"Error removing {test_logs_lnk}"
        test_result.fail_test("Process", message, sys.exc_info())
        return 1024

    return 0


def update_jenkins_xml(test, logs_dir, test_result):
    """Update the xml files for use in Jenkins.

    Args:
        test (TestInfo): the test information
        logs_dir (str): location of the test results
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if all the xml updates were successful; False if there was error
    """
    log = getLogger()
    log.info("Updating xml files for use in Jenkins")
    xml_file = os.path.join(logs_dir, 'results.xml')
    launchable_xml = os.path.join(logs_dir, 'xunit1_results.xml')

    # Read the test xml file
    xml_data = get_xml_data(xml_file, test_result)
    if not xml_data:
        return False

    # Extract the test class name from the xml data
    try:
        test_class = re.findall(r'<testcase classname="([A-Za-z0-9_]+)"', xml_data)[0]
        log.debug("Test class from xml: %s", test_class)
    except IndexError:
        message = f"Error obtaining class name from {xml_file}"
        test_result.fail_test("Process", message, sys.exc_info())
        return False

    # Update the class name to include the functional test directory
    log.debug("Updating the xml data in the test %s file", launchable_xml)
    pattern = 'classname="'
    replacement = f'classname="FTEST_{test.directory}.'
    if not update_xml(xml_file, pattern, replacement, xml_data, test_result):
        return False

    # Create an copy of the test xml for Launchable processing
    log.debug("Updating the xml data for the Launchable %s file", launchable_xml)
    pattern = r'(name=")\d+-\.\/.+\.(test_[^;]+);[^"]+(")'
    replacement = rf'\1\2\3 file="{test.test_file}"'
    if not update_xml(launchable_xml, pattern, replacement, xml_data, test_result):
        return False

    # Update the class name to include the functional test directory and test class in any
    # cmocka xml files generated by this test
    status = True
    cmocka_files = glob.glob(f"{logs_dir}/test-results/*-*/data/*.xml")
    log.debug("Updating %s cmocka xml files for use in Jenkins", len(cmocka_files))
    for cmocka_xml in cmocka_files:
        cmocka_data = get_xml_data(cmocka_xml, test_result)
        if not cmocka_data:
            return False

        if 'classname' not in cmocka_data:
            # Update cmocka results that are missing a 'classname' entry for their testsuite entries
            log.debug("Updating the xml data in the test %s file", cmocka_xml)
            pattern = '<testsuite name="(.*)"'
            replacement = f'<testsuite classname="FTEST_{test.directory}-{test_class}.'
            if not update_xml(cmocka_xml, pattern, replacement, cmocka_data, test_result):
                return False
        else:
            # Update the class name to include the functional test directory
            log.debug("Updating the xml data in the test %s file", cmocka_xml)
            name = re.findall(r'name="([A-Za-z0-9_-]*)"', cmocka_data)[0]
            pattern = '<testcase name='
            replacement = f'<testcase classname="FTEST_{test.directory}-{test_class}.{name} name='
            if not update_xml(cmocka_xml, pattern, replacement, cmocka_data, test_result):
                return False

        # if '<testcase classname' in cmocka_data:
        #     pattern = 'case classname="'
        #     replacement = f'case classname="FTEST_{test.directory}.{test_class}-'
        #     if not update_xml(cmocka_xml, pattern, replacement, cmocka_data, test_result):
        #         status = False
        # else:
        #     for suite in re.findall(r'<testsuite name="(.*)"\s', cmocka_data):
        #         pattern = 'case name'
        #         replacement = (
        #             f'case classname=\"FTEST_{test.directory}.{test_class}-{suite}\" name')
        #         if not update_xml(cmocka_xml, pattern, replacement, cmocka_data, test_result):
        #             status = False
    return status


def get_xml_data(xml_file, test_result):
    """Get data from the xml file.

    Args:
        xml_file (str): the xml file to read
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        str: data from the xml file or None if there was an error
    """
    log = getLogger()
    log.debug("Collecting data from the %s", xml_file)
    try:
        with open(xml_file, encoding="utf-8") as xml_buffer:
            return xml_buffer.read()
    except OSError:
        message = f"Error reading {xml_file}"
        test_result.fail_test("Process", message, sys.exc_info())
        return None


def update_xml(xml_file, pattern, replacement, xml_data, test_result):
    """Update the class name information in the test result xml.

    Args:
        xml_file (str): the xml file to create with the modified xml data
        pattern (str): the value to be replaced in the xml data
        replacement (str): the value to use as the replacement in the xml data
        xml_data (str): the data to modify and write to the xml file
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if successful; False if an error was detected
    """
    log = getLogger()
    log.debug("Replacing '%s' with '%s' in %s", pattern, replacement, xml_file)

    log.debug("  Contents of %s before replacement", xml_file)
    for line in xml_data.splitlines():
        log.debug("    %s", line)
    log.debug("")

    try:
        with open(xml_file, "w", encoding="utf-8") as xml_buffer:
            xml_buffer.write(re.sub(pattern, replacement, xml_data))
    except OSError:
        message = f"Error writing {xml_file}"
        test_result.fail_test("Process", message, sys.exc_info())
        return False

    log.debug("  Contents of %s after replacement", xml_file)
    for line in get_xml_data(xml_file, test_result).splitlines():
        log.debug("    %s", line)
    log.debug("")

    return True


def collect_test_result(test, test_result, job_results_dir, stop_daos, archive, rename, jenkins_xml,
                        core_files, threshold, total_repeats):
    """Process the test results.

    This may include (depending upon argument values):
        - Stopping any running servers or agents
        - Resetting the server storage
        - Archiving any files generated by the test and including them with the test results
        - Renaming the test results directory and results.xml entries
        - Processing any core files generated by the test

    Args:
        test (TestInfo): the test information
        test_result (TestResult): the test result used to update the status of the test
        job_results_dir (str): path to the avocado job results
        repeat (int): the test repetition number
        stop_daos (bool): whether or not to stop daos servers/clients after the test
        archive (bool): whether or not to collect remote files generated by the test
        rename (bool): whether or not to rename the default avocado job-results directory names
        jenkins_xml (bool): whether or not to update the results.xml to use Jenkins-style names
        core_files (dict): location and pattern defining where core files may be written
        threshold (str): optional upper size limit for test log files
        total_repeats (int): total number of times the test will be repeated

    Returns:
        int: status code: 0 = success, >0 = failure

    """
    return_code = 0

    # Stop any agents or servers running via systemd
    if stop_daos:
        if not stop_daos_agent_services(test):
            return_code |= 512
        if not stop_daos_server_service(test):
            return_code |= 512
        if not reset_server_storage(test):
            return_code |= 512
        if not cleanup_processes(test, test_result):
            return_code |= 4096

    # Mark the test execution as failed if a results.xml file is not found
    test_logs_dir = os.path.realpath(os.path.join(job_results_dir, "latest"))
    results_xml = os.path.join(test_logs_dir, "results.xml")
    if not os.path.exists(results_xml):
        message = f"Missing a '{results_xml}' file for {str(test)}"
        test_result.fail_test("Process", message)
        return_code = 16

    # Optionally store all of the server and client config files and remote logs along with
    # this test's results. Also report an error if the test generated any log files with a
    # size exceeding the threshold.
    test_env = TestEnvironment()
    if archive:
        remote_files = OrderedDict()
        remote_files["local configuration files"] = {
            "source": test_env.log_dir,
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[0]),
            "pattern": "*_*_*.yaml",
            "hosts": get_local_host(),
            "depth": 1,
            "timeout": 300,
        }
        remote_files["remote configuration files"] = {
            "source": os.path.join(os.sep, "etc", "daos"),
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[0]),
            "pattern": "daos_*.yml",
            "hosts": test.host_info.all_hosts,
            "depth": 1,
            "timeout": 300,
        }
        remote_files["daos log files"] = {
            "source": test_env.log_dir,
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[1]),
            "pattern": "*log*",
            "hosts": test.host_info.all_hosts,
            "depth": 1,
            "timeout": 900,
        }
        remote_files["cart log files"] = {
            "source": test_env.log_dir,
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[2]),
            "pattern": "*log*",
            "hosts": test.host_info.all_hosts,
            "depth": 2,
            "timeout": 900,
        }
        remote_files["ULTs stacks dump files"] = {
            "source": os.path.join(os.sep, "tmp"),
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[3]),
            "pattern": "daos_dump*.txt*",
            "hosts": test.host_info.servers.hosts,
            "depth": 1,
            "timeout": 900,
        }
        remote_files["valgrind log files"] = {
            "source": test_env.shared_dir,
            "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[4]),
            "pattern": "valgrind*",
            "hosts": test.host_info.servers.hosts,
            "depth": 1,
            "timeout": 900,
        }
        for index, hosts in enumerate(core_files):
            remote_files[f"core files {index + 1}/{len(core_files)}"] = {
                "source": core_files[hosts]["path"],
                "destination": os.path.join(job_results_dir, "latest", TEST_RESULTS_DIRS[5]),
                "pattern": core_files[hosts]["pattern"],
                "hosts": NodeSet(hosts),
                "depth": 1,
                "timeout": 1800,
            }
        for summary, data in remote_files.items():
            if not data["hosts"]:
                continue
            return_code |= archive_files(
                summary, data["hosts"].copy(), data["source"], data["pattern"],
                data["destination"], data["depth"], threshold, data["timeout"],
                test_result, test)

    # Optionally rename the test results directory for this test
    if rename:
        return_code |= rename_avocado_test_dir(
            test, job_results_dir, test_result, jenkins_xml, total_repeats)

    return return_code
