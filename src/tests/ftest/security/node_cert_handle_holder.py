"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Open a pool handle and keep it live until SIGTERM. The PoolNodeAuthRevoke
  eviction tests use this to plant a real, machine-bound handle on a
  remote client host so that `dmg pool revoke-client` has something to
  evict and the assertions on handles_evicted/evict_scope are meaningful.

  After opening the handle, the holder loops calling pool_query against
  that same handle every few seconds. If the handle has been evicted
  server-side, pool_query fails and the holder exits non-zero — which the
  test interprets as "the existing handle was torn down". The
  --no-evict test reads "process still alive after revoke" as "the existing
  handle survived".

  Usage:
      python3 node_cert_handle_holder.py LIB64_DIR POOL_UUID PIDFILE READYFILE [GROUP]

  LIB64_DIR is the install lib64 directory (e.g. /opt/daos/install/lib64
  for source builds, /usr/lib64 for RPMs) — the caller passes the install
  prefix because the holder script can't infer it.
"""
import os
import signal
import sys
import time

from pydaos.raw import DaosApiError, DaosContext, DaosPool

_HEARTBEAT_INTERVAL_SEC = 2


def _terminate(_signum, _frame):
    # Exit without disconnecting: the whole point is that the handle is
    # still live when revoke runs, so the server has something to evict.
    sys.exit(0)


def main():
    """Open a pool handle on the calling host and hold it open until signaled."""
    if len(sys.argv) < 5:
        print("usage: node_cert_handle_holder.py LIB64_DIR POOL_UUID "
              "PIDFILE READYFILE [GROUP]", file=sys.stderr)
        sys.exit(2)

    lib64_dir = sys.argv[1]
    pool_uuid = sys.argv[2]
    pidfile = sys.argv[3]
    readyfile = sys.argv[4]
    group = sys.argv[5] if len(sys.argv) > 5 else "daos_server"

    # Order matters:
    #   1. pidfile FIRST so the test can always find this process for
    #      cleanup, even if any subsequent step fails.
    #   2. Signal handlers BEFORE pool.connect() so SIGTERM during the
    #      connect window can't leave a half-opened handle behind.
    #   3. pool.connect() — the work that needs cleanup.
    #   4. readyfile LAST — the canary the test polls for; signals that
    #      the handle is open and the holder is in its heartbeat loop.
    with open(pidfile, "w", encoding="utf-8") as fh:
        fh.write(f"{os.getpid()}\n")

    signal.signal(signal.SIGTERM, _terminate)
    signal.signal(signal.SIGINT, _terminate)

    context = DaosContext(lib64_dir)
    pool = DaosPool(context)
    pool.set_uuid_str(pool_uuid)
    pool.set_group(group.encode())
    # DAOS_PC_RO = 1; RO is enough for revoke to see a live handle.
    pool.connect(1)

    with open(readyfile, "w", encoding="utf-8") as fh:
        fh.write("ready\n")

    # Heartbeat loop: an op on the existing handle. If the server has
    # evicted the handle (revoke without --no-evict), pool_query will
    # fail and we exit non-zero so the test can see the canary stop.
    while True:
        try:
            pool.pool_query()
        except DaosApiError as exc:
            print(f"pool_query failed: {exc}", file=sys.stderr)
            sys.exit(3)
        time.sleep(_HEARTBEAT_INTERVAL_SEC)


if __name__ == "__main__":
    main()
