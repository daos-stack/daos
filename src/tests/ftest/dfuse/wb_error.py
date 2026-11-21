"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import errno
import os
from contextlib import ExitStack

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from host_utils import get_local_host


class DfuseWritebackError(TestWithServers):
    """Verify delayed write-back errors through independent open handles.

    :avocado: recursive
    """

    def test_dfuse_wb_error(self):
        """Report delayed ENOSPC once per existing handle, not to new opens.

        Jira ID: DAOS-19735.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=daosio,dfuse
        :avocado: tags=DfuseWritebackError,test_dfuse_wb_error
        """
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        dfuse = get_dfuse(self, get_local_host())
        start_dfuse(self, dfuse, pool, container,
                    disable_caching=False, disable_wb_cache=False)

        target = os.path.join(dfuse.mount_dir.value, 'wb_error')
        block = bytes(128 * 1024)
        max_bytes = self.params.get('max_bytes', '/run/wb_error/*')
        self.assertGreater(max_bytes, 0)

        self.log_step('Fill the pool until an acknowledged write fails at fsync')
        with ExitStack() as stack:
            writer = stack.enter_context(open(target, 'w+b', buffering=0))
            observer = stack.enter_context(open(target, 'r+b', buffering=0))
            for _ in range((max_bytes + len(block) - 1) // len(block)):
                self.assertEqual(writer.write(block), len(block), 'Short buffered write')
                try:
                    os.fsync(writer.fileno())
                except OSError as error:
                    if error.errno != errno.ENOSPC:
                        raise
                    break
                os.fsync(observer.fileno())
            else:
                self.fail(f'No delayed ENOSPC after writing {max_bytes} bytes')

            # Do not submit more writes: each remaining sync observes the same error generation.
            self.log_step('The writer must not report the same error twice')
            os.fsync(writer.fileno())

            self.log_step('An independently opened handle must report the pending error once')
            with self.assertRaises(OSError) as raised:
                os.fsync(observer.fileno())
            self.assertEqual(raised.exception.errno, errno.ENOSPC)
            os.fsync(observer.fileno())

            self.log_step('A new open must not inherit historical write-back errors')
            fresh = stack.enter_context(open(target, 'r+b', buffering=0))
            os.fsync(fresh.fileno())
            os.fsync(fresh.fileno())
