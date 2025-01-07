"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP
  (C) Copyright 2025 Google LLC
  (C) Copyright 2025 Enakta Labs Ltd

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import random

from apricot import TestWithServers
from pydaos.torch import Checkpoint


class PytorchCheckpointTest(TestWithServers):
    """Test Pytorch Checkpoint interface

    :avocado: recursive
    """

    def test_checkpoint(self):
        """Test Pytorch Checkpoint interface

        Test Description: Ensure that writing and reading a checkpoint works as expected.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pytorch
        :avocado: tags=PytorchCheckpointTest,test_checkpoint
        """
        pool = self.get_pool()
        container = self.get_container(pool)

        writes = self.params.get("writes", "/run/checkpoint/*")
        min_size = self.params.get("min_size", "/run/checkpoint/*", 1)
        max_size = self.params.get("max_size", "/run/checkpoint/*", 1024 * 1024)

        expected = bytearray()
        chkp = Checkpoint(pool.identifier, container.identifier)
        with chkp.writer('blob') as w:
            for _ in range(writes):
                content = os.urandom(random.randint(min_size, max_size))

                w.write(content)
                expected.extend(content)

        actual = chkp.reader('blob')
        if expected != actual.getvalue():
            self.fail("checkpoint did not read back the expected content")
