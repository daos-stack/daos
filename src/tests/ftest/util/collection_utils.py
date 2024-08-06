"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
import glob
import os
import re
import sys
from collections import OrderedDict
from difflib import unified_diff

from ClusterShell.NodeSet import NodeSet
from process_core_files import CoreFileException, CoreFileProcessing
# pylint: disable=import-error,no-name-in-module
from util.environment_utils import TestEnvironment
from util.host_utils import get_local_host
from util.run_utils import find_command, run_local, run_remote, stop_processes
from util.user_utils import get_chown_command
from util.yaml_utils import get_test_category

CLEANUP_PROCESS_NAMES = [
    "daos_server", "daos_engine", "daos_agent", "cart_ctl", "orterun", "mpirun", "dfuse"]
CLEANUP_UNMOUNT_TYPES = ["fuse.daos"]
FAILURE_TRIGGER = "00_trigger-launch-failure_00"
TEST_EXPECT_CORE_FILES = ["./harness/core_files.py"]
TEST_RESULTS_DIRS = (
    "daos_configs", "daos_logs", "cart_logs", "daos_dumps", "valgrind_logs", "stacktraces")


def stop_daos_agent_services(logger, test):
    """Stop any daos_agent.service running on the client hosts.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        bool: True if the daos_agent.service was successfully stopped; False otherwise

    """
    service = "daos_agent.service"
    # pylint: disable=unsupported-binary-operation
    hosts = test.host_info.clients.hosts | get_local_host()
    logger.debug("-" * 80)
    logger.debug("Verifying %s after running '%s'", service, test)
    return stop_service(logger, hosts, service)


def stop_daos_server_service(logger, test):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        bool: True if the daos_server.service  was successfully stopped; False otherwise

    """
    service = "daos_server.service"
    hosts = test.host_info.servers.hosts
    logger.debug("-" * 80)
    logger.debug("Verifying %s after running '%s'", service, test)
    return stop_service(logger, hosts, service)


def stop_service(logger, hosts, service):
    """Stop any daos_server.service running on the hosts running servers.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): list of hosts on which to stop the service.
        service (str): name of the service

    Returns:
        bool: True if the service was successfully stopped; False otherwise

    """
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
            result = get_service_status(logger, check_hosts, service)
            check_hosts = NodeSet()
            for key in status_keys:
                if result[key]:
                    if loop == max_loops:
                        # Exit the while loop if the service is still running
                        logger.error(
                            " - Error %s still %s on %s", service, mapping[key], result[key])
                        result["status"] = False
                    else:
                        # Issue the appropriate systemctl command to remedy the
                        # detected state, e.g. 'stop' for 'active'.
                        command = ["sudo", "-n", "systemctl", key, service]
                        run_remote(logger, result[key], " ".join(command))

                        # Run the status check again on this group of hosts
                        check_hosts.add(result[key])
            loop += 1
    else:
        logger.debug("  Skipping stopping %s service - no hosts", service)

    return result["status"]


def get_service_status(logger, hosts, service):
    """Get the status of the daos_server.service.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodeSet): hosts on which to get the service state
        service (str): name of the service

    Returns:
        dict: a dictionary with the following keys:
            - "status":       boolean set to True if status was obtained; False otherwise
            - "stop":         NodeSet where to stop the daos_server.service
            - "disable":      NodeSet where to disable the daos_server.service
            - "reset-failed": NodeSet where to reset the daos_server.service

    """
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
    result = run_remote(logger, hosts, " ".join(command))
    for data in result.output:
        if data.timeout:
            status["status"] = False
            status["stop"].add(data.hosts)
            status["disable"].add(data.hosts)
            status["reset-failed"].add(data.hosts)
            logger.debug("  %s: TIMEOUT", data.hosts)
            break
        logger.debug("  %s: %s", data.hosts, "\n".join(data.stdout))
        for key, state_list in status_states.items():
            for line in data.stdout:
                if line in state_list:
                    status[key].add(data.hosts)
                    break
    return status


def reset_server_storage(logger, test):
    """Reset the server storage for the hosts that ran servers in the test.

    This is a workaround to enable binding devices back to nvme or vfio-pci after they are
    unbound from vfio-pci to nvme.  This should resolve the "NVMe not found" error seen when
    attempting to start daos engines in the test.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information

    Returns:
        bool: True if the server storage was successfully reset; False otherwise

    """
    logger.debug("-" * 80)
    logger.debug("Resetting server storage after running %s", test)
    hosts = test.host_info.servers.hosts
    if hosts:
        test_env = TestEnvironment()
        commands = [
            "if lspci | grep -i nvme",
            f"then export COVFILE={test_env.bullseye_file} && "
            "daos_server nvme reset && "
            "sudo -n rmmod vfio_pci && sudo -n modprobe vfio_pci",
            "fi"]
        logger.info("Resetting server storage on %s after running '%s'", hosts, test)
        result = run_remote(logger, hosts, f"bash -c '{';'.join(commands)}'", timeout=600)
        if not result.passed:
            logger.debug("Ignoring any errors from these workaround commands")
            # return False
    else:
        logger.debug("  Skipping resetting server storage - no server hosts")
    return True


def cleanup_processes(logger, test, result):
    """Cleanup any processes left running on remote nodes.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information
        result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if nothing needed to be cleaned up; False otherwise

    """
    any_found = False
    hosts = test.host_info.all_hosts
    if not hosts:
        return True

    logger.debug("-" * 80)
    logger.debug("Cleaning up running processes on %s after running %s", hosts, test)

    proc_pattern = "|".join(CLEANUP_PROCESS_NAMES)
    logger.debug("Looking for running processes on %s: %s", hosts, proc_pattern)
    detected, running = stop_processes(logger, hosts, f"'{proc_pattern}'", force=True)
    if running:
        message = f"Failed to kill processes on {running}"
        result.fail_test(logger, "Process", message)
    elif detected:
        message = f"Running processes found on {detected}"
        result.warn_test(logger, "Process", message)

    logger.debug("Looking for mount types on %s: %s", hosts, " ".join(CLEANUP_UNMOUNT_TYPES))
    # Use mount | grep instead of mount -t for better logging
    grep_pattern = "|".join(f'type {_type}' for _type in CLEANUP_UNMOUNT_TYPES)
    mount_grep_cmd = f"mount | grep -E '{grep_pattern}'"
    mount_grep_result = run_remote(logger, hosts, mount_grep_cmd)
    if mount_grep_result.passed_hosts:
        any_found = True
        logger.debug("Unmounting: %s", " ".join(CLEANUP_UNMOUNT_TYPES))
        type_list = ",".join(CLEANUP_UNMOUNT_TYPES)
        umount_cmd = f"sudo -n umount -v --all --force -t '{type_list}'"
        umount_result = run_remote(logger, mount_grep_result.passed_hosts, umount_cmd)
        if umount_result.failed_hosts:
            message = f"Failed to unmount on {umount_result.failed_hosts}"
            result.fail_test(logger, "Process", message)
        else:
            message = f"Unexpected mounts on {mount_grep_result.passed_hosts}"
            result.warn_test(logger, "Process", message)

    return not any_found


def archive_files(logger, summary, hosts, source, pattern, destination, depth, threshold, timeout,
                  test_result, test=None):
    # pylint: disable=too-many-arguments
    """Archive the files from the source to the destination.

    Args:
        logger (Logger): logger for the messages produced by this method
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
    logger.debug("=" * 80)
    logger.info(
        "Archiving %s from %s:%s to %s%s,",
        summary, hosts, os.path.join(source, pattern), destination,
        f" after running '{str(test)}'" if test else "")
    logger.debug("  Remote hosts: %s", hosts.difference(get_local_host()))
    logger.debug("  Local host:   %s", hosts.intersection(get_local_host()))

    # List any remote files and their sizes and determine which hosts contain these files
    return_code, file_hosts = list_files(logger, hosts, source, pattern, depth, test_result)
    if not file_hosts:
        # If no files are found then there is nothing else to do
        logger.debug("No %s files found on %s", os.path.join(source, pattern), hosts)
        return return_code

    if "log" in pattern:
        # Remove any empty files
        return_code |= remove_empty_files(logger, file_hosts, source, pattern, depth, test_result)

        # Report an error if any files sizes exceed the threshold
        if threshold is not None:
            return_code |= check_log_size(
                logger, file_hosts, source, pattern, depth, threshold, test_result)

        # Run cart_logtest on log files
        return_code |= cart_log_test(logger, file_hosts, source, pattern, depth, test_result)

    # Remove any empty files
    return_code |= remove_empty_files(logger, file_hosts, source, pattern, depth, test_result)

    # Compress any files larger than 1 MB
    return_code |= compress_files(logger, file_hosts, source, pattern, depth, test_result)

    # Move the test files to the test-results directory on this host
    return_code |= move_files(
        logger, file_hosts, source, pattern, destination, depth, timeout, test_result)

    if test and "core files" in summary:
        # Process the core files
        return_code |= process_core_files(logger, os.path.split(destination)[0], test, test_result)

    return return_code


def list_files(logger, hosts, source, pattern, depth, test_result):
    """List the files in source with that match the pattern.

    Args:
        logger (Logger): logger for the messages produced by this method
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
    status = 0
    hosts_with_files = NodeSet()
    source_files = os.path.join(source, pattern)
    logger.debug("-" * 80)
    logger.debug("Listing any %s files on %s", source_files, hosts)
    other = ["-printf", "'%M %n %-12u %-12g %12k %t %p\n'"]
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = f"Error determining if {source_files} files exist on {result.failed_hosts}"
        test_result.fail_test(logger, "Process", message)
        status = 16
    else:
        for data in result.output:
            for line in data.stdout:
                if source in line:
                    logger.debug("Found at least one file match on %s: %s", data.hosts, line)
                    hosts_with_files.add(data.hosts)
                    break
            if re.findall(fr"{FAILURE_TRIGGER}", "\n".join(data.stdout)):
                # If a file is found containing the FAILURE_TRIGGER then report a failure.
                # This feature is used by avocado tests to verify that launch.py reports
                # errors correctly in CI. See test_launch_failures in harness/basic.py for
                # more details.
                logger.debug(
                    "Found a file matching the '%s' failure trigger on %s",
                    FAILURE_TRIGGER, data.hosts)
                message = f"Error trigger failure file found in {source} (error handling test)"
                test_result.fail_test(logger, "Process", message)
                hosts_with_files.add(data.hosts)
                status = 16

    logger.debug("List files results: status=%s, hosts_with_files=%s", status, hosts_with_files)
    return status, hosts_with_files


def check_log_size(logger, hosts, source, pattern, depth, threshold, test_result):
    """Check if any file sizes exceed the threshold.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        threshold (str): optional upper size limit for test log files
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 32 = failure

    """
    source_files = os.path.join(source, pattern)
    logger.debug("-" * 80)
    logger.debug(
        "Checking for any %s files exceeding %s on %s", source_files, threshold, hosts)
    other = ["-size", f"+{threshold}", "-printf", "'%p %k KB'"]
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = (f"Error checking for {source_files} files exceeding the {threshold} threshold "
                   f"on {result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return 32

    # The command output will include the source path if the threshold has been exceeded
    for data in result.output:
        if source in "\n".join(data.stdout):
            message = f"One or more {source_files} files exceeded the {threshold} threshold"
            test_result.fail_test(logger, "Process", message)
            return 32

    logger.debug("No %s file sizes found exceeding the %s threshold", source_files, threshold)
    return 0


def cart_log_test(logger, hosts, source, pattern, depth, test_result):
    """Run cart_logtest on the log files.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    source_files = os.path.join(source, pattern)
    cart_logtest = os.path.abspath(os.path.join("cart", "cart_logtest.py"))
    logger.debug("-" * 80)
    logger.debug("Running %s on %s files on %s", cart_logtest, source_files, hosts)
    other = ["-print0", "|", "xargs", "-0", "-r0", "-n1", "-I", "%", "sh", "-c",
             f"'{cart_logtest} --ftest-mode %'"]
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other), timeout=4800)
    if not result.passed:
        message = (f"Error running {cart_logtest} on the {source_files} files on "
                   f"{result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return 16
    return 0


def remove_empty_files(logger, hosts, source, pattern, depth, test_result):
    """Remove any files with zero size.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        tuple: status code int (0 = success, 16 = failure), NoseSet of hosts on which the command
            was successful

    """
    logger.debug("-" * 80)
    logger.debug("Removing any zero-length %s files in %s on %s", pattern, source, hosts)
    other = ["-empty", "-print", "-delete"]
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = (f"Error removing any zero-length {os.path.join(source, pattern)} files on "
                   f"{result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return 16
    return 0


def compress_files(logger, hosts, source, pattern, depth, test_result):
    """Compress any files larger than 1M.

    Args:
        logger (Logger): logger for the messages produced by this method
        hosts (NodSet): hosts on which the files are located
        source (str): where the files are currently located
        pattern (str): pattern used to limit which files are processed
        depth (int): max depth for find command
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 0 = success, 16 = failure

    """
    logger.debug("-" * 80)
    logger.debug("Compressing any %s files in %s on %s larger than 1M", pattern, source, hosts)
    other = ["-size", "+1M", "-print0", "|", "sudo", "-n", "xargs", "-0", "-r0", "lbzip2", "-v"]
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = (f"Error compressing {os.path.join(source, pattern)} files larger than 1M on "
                   f"{result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return 16
    return 0


def move_files(logger, hosts, source, pattern, destination, depth, timeout, test_result):
    """Move files from the source to the destination.

    Args:
        logger (Logger): logger for the messages produced by this method
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
    logger.debug("-" * 80)
    logger.debug("Moving files from %s to %s on %s", source, destination, hosts)
    return_code = 0

    # Core and dump files require a file ownership change before they can be copied
    if "stacktrace" in destination or "daos_dumps" in destination:
        # pylint: disable=import-outside-toplevel
        other = ["-print0", "|", "xargs", "-0", "-r0", "sudo", "-n", get_chown_command()]
        result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
        if not result.passed:
            message = (f"Error changing {os.path.join(source, pattern)} file permissions on "
                       f"{result.failed_hosts}")
            test_result.fail_test(logger, "Process", message)
            return_code = 16
            hosts = result.passed_hosts.copy()
    if not hosts:
        return return_code

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
    result = run_remote(logger, hosts, command)
    if not result.passed:
        message = (f"Error creating temporary remote copy directory '{tmp_copy_dir}' on "
                   f"{result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return_code = 16
        hosts = result.passed_hosts.copy()
    if not hosts:
        return return_code

    # Move all the source files matching the pattern into the temporary remote directory
    other = f"-print0 | xargs -0 -r0 -I '{{}}' {sudo_command}mv '{{}}' '{tmp_copy_dir}'/"
    result = run_remote(logger, hosts, find_command(source, pattern, depth, other))
    if not result.passed:
        message = (f"Error moving files to temporary remote copy directory '{tmp_copy_dir}' on "
                   f"{result.failed_hosts}")
        test_result.fail_test(logger, "Process", message)
        return_code = 16
        hosts = result.passed_hosts.copy()
    if not hosts:
        return return_code

    # Clush -rcopy the temporary remote directory to this host
    command = ["clush", "-w", str(hosts), "-pv", "--rcopy", f"'{tmp_copy_dir}'", "--dest",
               f"'{rcopy_dest}'"]
    if not run_local(logger, " ".join(command), timeout=timeout).passed:
        message = f"Error copying remote files to {destination}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return_code = 16

    # Remove the temporary remote directory on each host
    command = f"{sudo_command}rm -fr '{tmp_copy_dir}'"
    if not run_remote(logger, hosts, command).passed:
        message = f"Error removing temporary remote copy directory '{tmp_copy_dir}'"
        test_result.fail_test(logger, "Process", message)
        return_code = 16

    return return_code


def process_core_files(logger, test_job_results, test, test_result):
    """Generate a stacktrace for each core file detected.

    Args:
        logger (Logger): logger for the messages produced by this method
        test_job_results (str): the location of the core files
        test (TestInfo): the test information
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 2048 = Core file exist; 256 = failure; 0 = success

    """
    core_file_processing = CoreFileProcessing(logger)
    try:
        core_files_processed = core_file_processing.process_core_files(
            test_job_results, True, test=str(test))

    except CoreFileException:
        message = "Errors detected processing test core files"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return 256

    except Exception:       # pylint: disable=broad-except
        message = "Unhandled error processing test core files"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return 256

    if core_file_processing.is_el7() and str(test) in TEST_EXPECT_CORE_FILES:
        logger.debug(
            "Skipping checking core file detection for %s as it is not supported on this OS",
            str(test))
        return 0

    if core_files_processed > 0 and str(test) not in TEST_EXPECT_CORE_FILES:
        message = "One or more core files detected after test execution"
        test_result.fail_test(logger, "Process", message)
        return 2048

    if core_files_processed == 0 and str(test) in TEST_EXPECT_CORE_FILES:
        message = "No core files detected when expected"
        test_result.fail_test(logger, "Process", message)
        return 256

    return 0


def create_steps_log(logger, job_results_dir, test_result):
    """Create a steps.log file from the job.log file.

    The steps.log file contains high level test steps.

    Args:
        logger (Logger): logger for the messages produced by this method
        job_results_dir (str): path to the avocado job results
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        int: status code: 8192 = problem creating steps.log; 0 = success
    """
    logger.debug("=" * 80)
    logger.info("Creating steps.log file")

    test_logs_lnk = os.path.join(job_results_dir, "latest")
    test_logs_dir = os.path.realpath(test_logs_lnk)
    job_log = os.path.join(test_logs_dir, 'job.log')
    step_log = os.path.join(test_logs_dir, 'steps.log')
    command = rf"grep -E '(INFO |ERROR)\| (==> Step|START|PASS|FAIL|ERROR)' {job_log}"
    result = run_local(logger, command)
    if not result.passed:
        message = f"Error creating {step_log}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return 8192
    with open(step_log, 'w', encoding="utf-8") as file:
        file.write(result.joined_stdout)
    return 0


def rename_avocado_test_dir(logger, test, job_results_dir, test_result, jenkins_xml, total_repeats):
    """Append the test name to its avocado job-results directory name.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information
        job_results_dir (str): path to the avocado job results
        test_result (TestResult): the test result used to update the status of the test
        jenkins_xml (bool): whether to update the results.xml with the Jenkins test names
        total_repeats (int): total number of times the test will be repeated

    Returns:
        int: status code: 0 = success, 1024 = failure

    """
    logger.debug("=" * 80)
    logger.info("Renaming the avocado job-results directory")

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
            test_result.fail_test(logger, "Process", message, sys.exc_info())
            return 1024

    # Rename the avocado job-results test directory and update the 'latest' symlink
    logger.info("Renaming test results from %s to %s", test_logs_dir, new_test_logs_dir)
    try:
        os.rename(test_logs_dir, new_test_logs_dir)
        os.remove(test_logs_lnk)
        os.symlink(new_test_logs_dir, test_logs_lnk)
        logger.debug("Renamed %s to %s", test_logs_dir, new_test_logs_dir)
    except OSError:
        message = f"Error renaming {test_logs_dir} to {new_test_logs_dir}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return 1024

    # Update the results.xml file with the new functional test class name
    if jenkins_xml and not update_jenkins_xml(logger, test, new_test_logs_dir, test_result):
        return 1024

    # Remove latest symlink directory to avoid inclusion in the Jenkins build artifacts
    if not run_local(logger, f"rm -fr '{test_logs_lnk}'").passed:
        message = f"Error removing {test_logs_lnk}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return 1024

    return 0


def update_jenkins_xml(logger, test, logs_dir, test_result):
    """Update the xml files for use in Jenkins.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information
        logs_dir (str): location of the test results
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if all the xml updates were successful; False if there was error
    """
    logger.info("Updating the xml test result files for use in Jenkins")

    # Read the test xml file
    xml_file = os.path.join(logs_dir, 'results.xml')
    xml_data = get_xml_data(logger, xml_file, test_result)
    if not xml_data:
        return False

    # Include the functional test directory in the class name of the avocado test xml file
    launchable_xml = os.path.join(logs_dir, 'xunit1_results.xml')
    if not update_test_xml(logger, test, xml_file, xml_data, launchable_xml, test_result):
        return False

    # Determine if this test produced any cmocka xml files
    cmocka_files = glob.glob(f"{logs_dir}/test-results/*-*/data/*.xml")
    logger.debug("Updating %s cmocka xml files for use in Jenkins", len(cmocka_files))
    if cmocka_files:
        # Extract the test class name from the xml data
        try:
            test_class = re.findall(r'<testcase classname="([A-Za-z0-9_]+)"', xml_data)[0]
            logger.debug("Test class from xml: %s", test_class)
        except IndexError:
            message = f"Error obtaining class name from {xml_file}"
            test_result.fail_test(logger, "Process", message, sys.exc_info())
            return False
        for cmocka_xml in cmocka_files:
            # Read the cmocka xml file
            cmocka_data = get_xml_data(logger, cmocka_xml, test_result)
            if not cmocka_data:
                return False

            # Include the functional test directory in the class name of the cmocka xml file
            if not update_cmocka_xml(
                    logger, test, cmocka_xml, cmocka_data, test_class, test_result):
                return False
    return True


def update_test_xml(logger, test, xml_file, xml_data, launchable_xml, test_result):
    """Update the class name the avocado test results xml file.

    Also create a launchable xml file from the original avocado test results.xml file data where
    the 'name' entry is replaced by the functional test method name and a 'file' entry is added for
    the functional test file name.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information
        xml_file (str): the functional test results xml file
        xml_data (str): the data to modify and write to the xml file
        launchable_xml (str) the launchable test results xml file
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: False if there problems updating the test results xml file; True otherwise
    """
    logger.debug("Updating the xml data in the test %s file", xml_file)

    # Update the class name to include the functional test directory
    pattern = 'classname="'
    replacement = f'classname="FTEST_{test.directory}.'
    if not update_xml(logger, xml_file, pattern, replacement, xml_data, test_result):
        return False

    # Create an copy of the test xml for Launchable processing
    logger.debug("Updating the xml data for the Launchable %s file", launchable_xml)
    pattern = r'(name=")\d+-\.\/.+\.(test_[^;]+);[^"]+(")'
    replacement = rf'\1\2\3 file="{test.test_file}"'
    return update_xml(logger, launchable_xml, pattern, replacement, xml_data, test_result)


def update_cmocka_xml(logger, test, cmocka_xml, cmocka_data, test_class, test_result):
    """Update the class name in the cmocka test result xml file.

    Args:
        logger (Logger): logger for the messages produced by this method
        test (TestInfo): the test information
        cmocka_xml (str): the cmocka xml file
        cmocka_data (str): the data to modify and write to the cmocka xml file
        test_class (str): avocado test class name
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: False if there problems updating the cmocka results xml file; True otherwise
    """
    logger.debug("Updating the xml data in the test %s file", cmocka_xml)

    if len(re.findall('<testsuites>', cmocka_data)) > 1:
        # Remove all but the first <testsuites> entry and all but the last </testsuites> entry from
        # the cmocka xml
        pattern = '</testsuites>\n<testsuites>\n'
        cmocka_data = replace_xml(logger, cmocka_xml, pattern, '', cmocka_data, test_result)
        if not cmocka_data:
            return False

    if 'classname' not in cmocka_data:
        # Update cmocka results that are missing a class name entry for their test suite entries
        try:
            pattern = r'<testsuite name="([A-Za-z0-9_-]*)"'
            name = re.findall(pattern, cmocka_data)[0]
            logger.debug("Cmocka test name from xml: %s", name)
        except IndexError:
            message = f"Error obtaining cmocka test name from {cmocka_xml} - no match for {pattern}"
            test_result.fail_test(logger, "Process", message, sys.exc_info())
            return False
        pattern = '<testcase name='
        replacement = f'<testcase classname="FTEST_{test.directory}.{test_class}-{name}" name='
        return update_xml(logger, cmocka_xml, pattern, replacement, cmocka_data, test_result)

    # Update the class name to include the functional test directory
    pattern = 'classname="'
    replacement = f'classname="FTEST_{test.directory}.{test_class}.'
    return update_xml(logger, cmocka_xml, pattern, replacement, cmocka_data, test_result)


def get_xml_data(logger, xml_file, test_result):
    """Get data from the xml file.

    Args:
        logger (Logger): logger for the messages produced by this method
        xml_file (str): the xml file to read
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        str: data from the xml file or None if there was an error
    """
    logger.debug("Collecting data from the %s", xml_file)
    try:
        with open(xml_file, encoding="utf-8") as xml_buffer:
            return xml_buffer.read()
    except OSError:
        message = f"Error reading {xml_file}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return None


def update_xml(logger, xml_file, pattern, replacement, xml_data, test_result):
    """Update the result xml.

    Args:
        logger (Logger): logger for the messages produced by this method
        xml_file (str): the xml file to create with the modified xml data
        pattern (str): the value to be replaced in the xml data
        replacement (str): the value to use as the replacement in the xml data
        xml_data (str): the data to modify and write to the xml file
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        bool: True if successful; False if an error was detected
    """
    return replace_xml(logger, xml_file, pattern, replacement, xml_data, test_result) is not None


def replace_xml(logger, xml_file, pattern, replacement, xml_data, test_result):
    """Replace the patterns in the xml data with specified replacements.

    Args:
        logger (Logger): logger for the messages produced by this method
        xml_file (str): the xml file to create with the modified xml data
        pattern (str): the value to be replaced in the xml data
        replacement (str): the value to use as the replacement in the xml data
        xml_data (str): the data to modify and write to the xml file
        test_result (TestResult): the test result used to update the status of the test

    Returns:
        str: the updated xml_data; None if an error was detected
    """
    logger.debug("Replacing '%s' with '%s' in %s", pattern, replacement, xml_file)
    try:
        with open(xml_file, "w", encoding="utf-8") as xml_buffer:
            xml_buffer.write(re.sub(pattern, replacement, xml_data))
    except OSError:
        message = f"Error writing {xml_file}"
        test_result.fail_test(logger, "Process", message, sys.exc_info())
        return None

    new_xml_data = get_xml_data(logger, xml_file, test_result)
    if new_xml_data is not None:
        logger.debug("  Diff of %s after replacement", xml_file)
        for line in unified_diff(xml_data.splitlines(), new_xml_data.splitlines(),
                                 fromfile=xml_file, tofile=xml_file, n=0, lineterm=""):
            logger.debug("    %s", line)
        logger.debug("")

    return new_xml_data


def collect_test_result(logger, test, test_result, job_results_dir, stop_daos, archive, rename,
                        jenkins_xml, core_files, threshold, total_repeats):
    # pylint: disable=too-many-arguments
    """Process the test results.

    This may include (depending upon argument values):
        - Stopping any running servers or agents
        - Resetting the server storage
        - Archiving any files generated by the test and including them with the test results
        - Renaming the test results directory and results.xml entries
        - Processing any core files generated by the test

    Args:
        logger (Logger): logger for the messages produced by this method
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
        if not stop_daos_agent_services(logger, test):
            return_code |= 512
        if not stop_daos_server_service(logger, test):
            return_code |= 512
        if not reset_server_storage(logger, test):
            return_code |= 512
        if not cleanup_processes(logger, test, test_result):
            return_code |= 4096

    # Mark the test execution as failed if a results.xml file is not found
    test_logs_dir = os.path.realpath(os.path.join(job_results_dir, "latest"))
    results_xml = os.path.join(test_logs_dir, "results.xml")
    if not os.path.exists(results_xml):
        message = f"Missing a '{results_xml}' file for {str(test)}"
        test_result.fail_test(logger, "Process", message)
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
                logger, summary, data["hosts"].copy(), data["source"], data["pattern"],
                data["destination"], data["depth"], threshold, data["timeout"],
                test_result, test)

    # Generate a steps.log file
    return_code |= create_steps_log(logger, job_results_dir, test_result)

    # Optionally rename the test results directory for this test
    if rename:
        return_code |= rename_avocado_test_dir(
            logger, test, job_results_dir, test_result, jenkins_xml, total_repeats)

    return return_code
