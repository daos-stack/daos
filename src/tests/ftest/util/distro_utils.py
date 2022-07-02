#!/usr/bin/python
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
# pylint: disable-next=wildcard-import, unused-wildcard-import
from avocado.utils.distro import *  # noqa: F403


class RockyProbe(Probe):                # noqa: F405
    """Probe with version checks for Rocky Linux systems."""

    CHECK_FILE = "/etc/rocky-release"
    CHECK_FILE_CONTAINS = "Rocky Linux"
    CHECK_FILE_DISTRO_NAME = "rocky"
    CHECK_VERSION_REGEX = re.compile(r"Rocky Linux release (\d{1,2})\.(\d{1,2}).*")


class AlmaProbe(Probe):                 # noqa: F405
    """Probe with version checks for AlmaLinux systems."""

    CHECK_FILE = "/etc/almalinux-release"
    CHECK_FILE_CONTAINS = "AlmaLinux"
    CHECK_FILE_DISTRO_NAME = "alma"
    CHECK_VERSION_REGEX = re.compile(r"AlmaLinux release (\d{1,2})\.(\d{1,2}).*")


register_probe(RockyProbe)              # noqa: F405
register_probe(AlmaProbe)               # noqa: F405
