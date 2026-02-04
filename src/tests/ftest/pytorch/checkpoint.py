"""
  (C) Copyright 2025 Google LLC
  (C) Copyright 2025-2026 Enakta Labs Ltd

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import errno
import os
import uuid

from apricot import TestWithServers
from pydaos.torch import Checkpoint


class PytorchCheckpointTest(TestWithServers):
    """Test Pytorch Checkpoint interface

    :avocado: recursive
    """

    def test_checkpoint_no_chunking(self):
        """Test Pytorch Checkpoint interface without chunking

        Test Description: Ensure that single or multiple writes are read back correctly

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pytorch
        :avocado: tags=PytorchCheckpointTest,test_checkpoint_no_chunking
        """
        pool = self.get_pool()
        container = self.get_container(pool)

        min_size = self.params.get("min_size", "/run/checkpoint_no_chunking/*", 1)
        max_size = self.params.get("max_size", "/run/checkpoint_no_chunking/*", 4 * 1024 * 1024)
        num_writes = self.params.get("writes", "/run/checkpoint_no_chunking/*", 7)

        writes = []
        for _ in range(num_writes):
            writes.append(os.urandom(self.random.randint(min_size, max_size)))
            self._test_checkpoint(pool.identifier, container.identifier, writes)

    def test_checkpoint_chunking(self):
        """Test Pytorch Checkpoint interface with chunking and parallel writes

        Test Description: Ensure that single or multiple writes are read back correctly

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pytorch
        :avocado: tags=PytorchCheckpointTest,test_checkpoint_chunking
        """

        pool = self.get_pool()
        container = self.get_container(pool)

        min_size = self.params.get("min_size", "/run/checkpoint_chunking/*", 1)
        max_size = self.params.get("max_size", "/run/checkpoint_chunking/*", 4 * 1024 * 1024)
        num_writes = self.params.get("writes", "/run/checkpoint_chunking/*", 8)
        chunk_sizes = self.params.get("chunk_sizes", "/run/checkpoint_chunking/*")
        chunks_limits = self.params.get("chunks_limits", "/run/checkpoint_chunking/*")
        workers = self.params.get("workers", "/run/checkpoint_chunking/*")

        if len(chunk_sizes) == 0 or len(workers) == 0 or len(chunks_limits) == 0:
            self.fail("chunk_sizes, chunks_limits and workers must be provided")

        writes = []
        for _ in range(num_writes):
            writes.append(os.urandom(self.random.randint(min_size, max_size)))
            for chunk_size in chunk_sizes:
                for chunks_limit in chunks_limits:
                    for worker in workers:
                        self._test_checkpoint(pool.identifier, container.identifier, writes,
                                              chunk_size=chunk_size, chunks_limit=chunks_limit,
                                              workers=worker)

    def test_checkpoint_nested_directories(self):
        """ Test Pytorch Checkpoint interface with nested directories
        Test Description: Ensure that parent directories are created for the checkpoint path

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pytorch
        :avocado: tags=PytorchCheckpointTest,test_checkpoint_nested_directories
        """

        pool = self.get_pool().identifier
        container = self.get_container(pool).identifier

        d1, d2 = str(uuid.uuid4()), str(uuid.uuid4())
        files = ["/file.pt", f"/{d1}/file.pt", f"/{d1}/{d2}/file.pt"]

        # by default parent directories should be created
        with Checkpoint(pool, container) as pt:
            for name in files:
                with pt.writer(name) as w:
                    w.write(os.urandom(4096))

        # ensure that it fails with expected exception
        try:
            with Checkpoint(pool, container) as pt:
                fname = f"/{str(uuid.uuid4())}/file.pt"
                with pt.writer(fname, ensure_path=False) as w:
                    w.write(os.urandom(4096))
                    raise RuntimeError("expected OSError with errno.ENOENT")
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise RuntimeError(f"expected errno.ENOENT, got {os.strerror(e.errno)}") from e
        except Exception as e:
            raise RuntimeError(f"unexpected error: {e}") from e

    def _test_checkpoint(self, pool, cont, writes, chunk_size=0, chunks_limit=0, workers=0):
        """Creates a checkpoint with the given parameters, writes the given data to it,
        then reads written data back from it and compares it with the expected writes.
        """

        self.log.info("Checkpoint test: writes=%s, chunk_size=%s, chunks_limit=%s, workers=%s",
                      len(writes), chunk_size, chunks_limit, workers)
        with Checkpoint(pool, cont, transfer_chunk_size=chunk_size, chunks_limit=chunks_limit,
                        workers=workers) as chkp:
            expected = bytearray()
            fname = str(uuid.uuid4())
            with chkp.writer(fname) as w:
                for chunk in writes:
                    w.write(chunk)
                    expected.extend(chunk)

            actual = chkp.reader(fname)
            if expected != actual.getvalue():
                self.fail(
                    f"checkpoint did not read back the expected content for {len(writes)} writes,"
                    f"chunk_size={chunk_size}, chunks_limit={chunks_limit}, workers={workers}")
