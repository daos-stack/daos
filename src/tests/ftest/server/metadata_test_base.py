#!/usr/bin/python3
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from apricot import TestWithServers


class MetadataTestBase(TestWithServers):
    """Base test class for metadata testing.

    Test Class Description:
        Test the general Metadata operations and boundary conditions.

    :avocado: recursive
    """

    # Minimum number of containers that should be able to be created
    CREATED_CONTAINERS_MIN = 3000

    # Number of created containers that should not be possible
    CREATED_CONTAINERS_LIMIT = 3500

    def create_pool(self):
        """Create a pool and display the svc ranks."""
        self.add_pool()
        self.log.info("Created pool %s: svc ranks:", self.pool.uuid)
        for index, rank in enumerate(self.pool.svc_ranks):
            self.log.info("[%d]: %d", index, rank)

    def create_all_containers(self, expected=None):
        """Create the maximum number of supported containers.

        Args:
            expected (int, optional): number of containers expected to be
                created. Defaults to None.

        Returns:
            bool: True if all of the expected number of containers were created
                successfully; False otherwise

        """
        status = False
        self.container = []
        self.log.info(
            "Attempting to create up to %d containers",
            self.CREATED_CONTAINERS_LIMIT)
        for index in range(self.CREATED_CONTAINERS_LIMIT):
            # Continue to create containers until there is not enough space
            if not self._create_single_container(index):
                status = True
                break

        self.log.info(
            "Created %s containers before running out of space",
            len(self.container))

        # Safety check to avoid test timeout - should hit an exception first
        if len(self.container) >= self.CREATED_CONTAINERS_LIMIT:
            self.log.error(
                "Created too many containers: %d", len(self.container))

        # Verify that at least MIN_CREATED_CONTAINERS have been created
        if status and len(self.container) < self.CREATED_CONTAINERS_MIN:
            self.log.error(
                "Only %d containers created; expected %d",
                len(self.container), self.CREATED_CONTAINERS_MIN)
            status = False

        # Verify that the expected number of containers were created
        if status and expected and len(self.container) != expected:
            self.log.error(
                "Unexpected created container quantity: %d/%d",
                len(self.container), expected)
            status = False

        return status

    def _create_single_container(self, index):
        """Create a single container.

        Args:
            index (int): container count

        Returns:
            bool: was a container created successfully

        """
        status = False
        # self.log.info("Creating container %d", index + 1)
        self.container.append(self.get_container(self.pool, create=False))
        if self.container[-1].daos:
            self.container[-1].daos.verbose = False
        try:
            self.container[-1].create()
            status = True
        except TestFail as error:
            self.log.info(
                "  Failed to create container %s: %s",
                index + 1, str(error))
            del self.container[-1]
            if "RC: -1007" not in str(error):
                self.fail(
                    "Unexpected error detected creating container %d",
                    index + 1)
        return status

    def destroy_all_containers(self):
        """Destroy all of the created containers.

        Returns:
            bool: True if all of the containers were destroyed successfully;
                False otherwise

        """
        self.log.info("Destroying %d containers", len(self.container))
        errors = self.destroy_containers(self.container)
        if errors:
            self.log.error(
                "Errors detected destroying %d containers: %d",
                len(self.container), len(errors))
            for error in errors:
                self.log.error("  %s", error)
        self.container = []
        return len(errors) == 0
