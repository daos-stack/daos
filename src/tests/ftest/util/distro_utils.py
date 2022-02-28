#!/usr/bin/python
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.utils import distro
import re


class RockyProbe(distro.Probe):
    """Probe with version checks for Rocky systems."""

    CHECK_FILE = '/etc/rocky-release'
    CHECK_FILE_CONTAINS = 'Rocky Linux'
    CHECK_FILE_DISTRO_NAME = 'rocky'
    CHECK_VERSION_REGEX = re.compile(r'Rocky Linux release (\d{1,2})\.(\d{1,2}).*')


distro.register_probe(RockyProbe)


def detect():
    """Detect the current distribution."""
    return distro.detect()
