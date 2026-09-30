#!/bin/bash
#
#  Copyright 2021-2024 Intel Corporation.
#  Copyright 2025-2026 Hewlett Packard Enterprise Development LP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent

bootstrap_dnf() {
    # This must be the first DNF operation after image restore to avoid
    # repeated noisy DNF logging in subsequent package operations.
    dnf -y reinstall sssd-common
}

group_repo_post() {
    # Nothing to do for EL
    :
}

distro_custom() {
    # TODO: This code is not exiting on failure.

    # Use a more recent python version for unit testing, this allows us to also test installing
    # pydaos into virtual environments.
    : "${PYTHON_VERSION:=3.11}"
    dnf -y install "python${PYTHON_VERSION}" "python${PYTHON_VERSION}-devel"
}
