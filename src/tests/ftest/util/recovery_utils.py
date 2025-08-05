"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from exception_utils import CommandFailure
from run_utils import run_remote


def get_vos_file_path(log, server_manager, pool):
    """Get the VOS file path.

    If there are multiple VOS files, returns the first file obtained by "ls".

    Args:
        log (logger): logger for the messages produced by this method
        server_manager (DaosServerManager): the servers running the pool
        pool (TestPool): the pool in which to find the vos file

    Returns:
        str: VOS file path such as /mnt/daos0/<pool_uuid>/vos-0 if found, else "".
    """
    hosts = server_manager.hosts[0:1]
    vos_path = server_manager.get_vos_path(pool)
    command = f"sudo ls {vos_path}"
    result = run_remote(log, hosts, command)
    if not result.passed:
        raise CommandFailure(f"Command '{command}' failed on {hosts}")

    for file in result.output[0].stdout:
        # Assume the VOS file has "vos" in the file name.
        if "vos" in file:
            log.info("vos_file: %s", file)
            return os.path.join(vos_path, file)

    # No VOS file found
    return ""


def wait_for_check_query(dmg, status=None):
    """Repeatedly call dmg check query until status matches the response status.

    Args:
        dmg (DmgCommand): the dmg command object used to run the check query
        status (str, optional): expected status to wait for or None. Defaults to None.

    Raises:
        CommandFailure: if there is an error running dmg check query or the status was not found in
            the response.

    Returns:
        dict: response from dmg check query.
    """
    for _ in range(8):
        check_query_out = dmg.check_query()
        if status is None or check_query_out["response"]["status"] == status:
            return check_query_out["response"]
        time.sleep(5)

    if status == "COMPLETED":
        raise CommandFailure("Checker didn't detect or repair any inconsistency!")
    if status == "RUNNING":
        raise CommandFailure("Checker didn't detect any inconsistency!")
    raise CommandFailure(f"Checker status is not '{status}'!")


def wait_for_check_complete(dmg):
    """Repeatedly call dmg check query until status becomes COMPLETED.

    If the status doesn't become COMPLETED, fail the test.

    Args:
        dmg (DmgCommand): the dmg command object used to run the check query

    Raises:
        CommandFailure: if there is an error running dmg check query or the status was not found in
            the response.

    Returns:
        list: List of repair reports.
    """
    response = wait_for_check_query(dmg, "COMPLETED")
    return response["reports"]


def find_report_message(reports, match):
    """Determine if the match exists in any of the dmg check query response messages.

    Args:
        reports (list): reports from the dmg check query response
        match (str): item to search for in each report message

    Returns:
        bool: True if the match was found; False otherwise
    """
    for report in reports:
        if match in report["msg"]:
            return True
    return False


def query_detect(dmg, fault):
    """Query until status becomes RUNNING and check if given fault is found.

    Args:
        dmg (DmgCommand): the dmg command object used to run the check query
        fault (str): Fault string to search in the query report.

    Raises:
        CommandFailure: if there is an error running dmg check query or the status was not found in
            the response.

    Returns:
        list: List of query reports.
    """
    response = wait_for_check_query(dmg, "RUNNING")
    if not find_report_message(response["reports"], fault):
        raise CommandFailure(f"Checker didn't detect {fault}!")
    return response["reports"]
