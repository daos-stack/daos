#!/usr/bin/env python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

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

def calculate_min_servers(oclass):
    """Calculate the minimum number of required servers for an object class.

    Args:
        oclass (str): the object class.

    Returns:
        int: the minimum number of required servers.

    """
    patterns = [
        "EC_([0-9]+)P([0-9])+",
        "RP_([0-9]+)"
    ]
    for pattern in patterns:
        # Findall returns a list where each element is a tuple of groups ()
        match = re.findall(pattern, oclass)
        if match:
            # Sum all groups (). Only index 0 should exist.
            return sum(int(n) for n in match[0])
    return 1
