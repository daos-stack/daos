"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re


def extract_redundancy_factor(oclass):
    """Extract the redundancy factor from an object class.

    Args:
        oclass (str): the object class.

    Returns:
        int: the redundancy factor.

    """
    match = re.search("EC_[0-9]+P([0-9])+", oclass)
    if match:
        return int(match.group(1))
    match = re.search("RP_([0-9]+)", oclass)
    if match:
        return int(match.group(1)) - 1
    return 0


def calculate_min_engines(oclass):
    """Calculate the minimum number of required engines for an object class.

    Args:
        oclass (str): the object class.

    Returns:
        int: the minimum number of required engines.

    """
    patterns = [
        "EC_([0-9]+)P([0-9])+",
        "RP_([0-9]+)"
    ]
    for pattern in patterns:
        # returns a list where each element is a tuple of groups ()
        match = re.findall(pattern, oclass)
        if match:
            # Sum all groups (). Only index 0 should exist.
            return sum(int(n) for n in match[0])
    return 1


def get_ec_data_parity_group(oclass):
    """Get the data shards, parity shards, and group number for an EC object class.

    Args:
        oclass (str): the object class

    Raises:
        ValueError: if oclass is not a valid EC object class

    Returns:
        (int, int, str): data shards, parity shards, group number
    """
    try:
        _match = re.findall('EC_([0-9])+P([0-9]+)G(.*)', oclass, flags=re.IGNORECASE)[0]
        return int(_match[0]), int(_match[1]), str(_match[2])
    except IndexError as error:
        raise ValueError(f'Invalid oclass: {oclass}') from error


def calculate_ec_targets_used(oclass, total_targets):
    """Calculate the number of targets used by an EC object class.

    Args:
        oclass (str): the object class
        total_targets (int): total number of system targets

    Raises:
        ValueError: if oclass is not a valid EC object class

    Returns:
        int: number of targets used by the object class
    """
    data_shards, parity_shards, group_number = get_ec_data_parity_group(oclass)
    group_size = data_shards + parity_shards
    if group_number in ('x', 'X'):
        group_number = max(1, total_targets // group_size)
    return group_size * int(group_number)
