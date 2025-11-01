"""
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from exception_utils import CommandFailure


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


def check_policies(dmg_command, interact_count):
    """Check the first interact_count policies are INTERACT and the remainder are default.

    Args:
        dmg_command (DmgCommand): DmgCommand object to call check get-policy.
        interact_count (int): Number of policies to check whether it's INTERACT.

    Raises:
        CommandFailure: if there is error running the dmg command or the expected policies are not
            INTERACT or DEFAULT.

    Returns:
            list: List of class name and its policy. e.g., "POOL_NONEXIST_ON_MS:DEFAULT"
    """
    policy_out = dmg_command.check_get_policy()
    policies = policy_out["response"]["policies"]
    # Check the first interact_count policies are INTERACT.
    for i in range(interact_count):
        policy = policies[i].split(":")[1]
        if policy != "INTERACT":
            class_name = policies[i].split(":")[0]
            msg = f"Unexpected policy for {class_name}! Expected = INTERACT, Actual = {policy}"
            raise CommandFailure(msg)
    # Check the rest of the policies are DEFAULT.
    for i in range(interact_count, len(policies)):
        policy = policies[i].split(":")[1]
        if policy != "DEFAULT":
            class_name = policies[i].split(":")[0]
            msg = f"Unexpected policy for {class_name}! Expected = DEFAULT, Actual = {policy}"
            raise CommandFailure(msg)
    return policies


def check_ram_used(server_manager, log):
    """Check whether 'ram' field is used in the storage section of the server config.

    Args:
        server_manager (ServerManager):
        log (logging.Logger): Used to print helpful logs.

    Returns:
        bool: If 'ram' field is found in 'storage', return True. Otherwise return False.
    """
    server_config_file = server_manager.manager.job.yaml_data
    log.info("server_config_file = %s", server_config_file)
    engine = server_config_file["engines"][0]
    storage_list = engine["storage"]
    ram_used = False
    for storage in storage_list:
        log.info("storage = %s", storage)
        if storage["class"] == "ram":
            log.info("ram found in %s", storage)
            ram_used = True
            break
    return ram_used
