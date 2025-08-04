"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time

from .exception_utils import CommandFailure
from .run_utils import run_remote


def get_vos_file_path(log, server_manager, pool):
    """Get the VOS file path.

    If there are multiple VOS files, returns the first file obtained by "ls".

    Args:
        log (logger): logger for the messages produced by this method
        server_manager (DaosServerManager): the servers running the pool
        pool (TestPool): the pool in which to find the vos file

    Raises:
        CommandFailure: if there is an error obtaining the vos file from the pool

    Returns:
        str: VOS file path such as /mnt/daos0/<pool_uuid>/vos-0
    """
    hosts = server_manager.hosts[0:1]
    scm_mount = server_manager.get_config_value("scm_mount")
    vos_path = os.path.join(scm_mount, pool.uuid.lower())
    command = " ".join(["sudo", "ls", vos_path])
    result = run_remote(log, hosts, command)
    if not result.passed:
        raise CommandFailure(f"Command '{command}' failed on {hosts}")

    # return vos_file
    for file in result.output[0].stdout:
        # Assume the VOS file has "vos" in the file name.
        if "vos" in file:
            log.info("vos_file: %s", file)
            return file

    raise CommandFailure(f"Unable to find vos file in '{command}' output")


def get_check_query_response(dmg, *args, **kwargs):
    """Ccall dmg check query and return the response.

    Args:
        dmg (DmgCommand): the dmg command object used to run the check query

    Raises:
        CommandFailure: if there is an error running dmg check query

    Returns:
        dict: response from dmg check query.
    """
    check_query_out = dmg.check_query(*args, **kwargs)
    return check_query_out["response"]


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
        response = get_check_query_response(dmg)
        if status is None or response["status"] == status:
            return response
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
        bool: True if the match was found; False othewrwise
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
