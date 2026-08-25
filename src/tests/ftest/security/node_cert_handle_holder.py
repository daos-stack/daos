"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Open a pool handle and hold it until SIGTERM, polling pool_query so the
  test can tell whether the handle was evicted.

  Usage:
      python3 node_cert_handle_holder.py LIB64_DIR POOL_UUID PIDFILE READYFILE [GROUP]

  LIB64_DIR is the DAOS install lib64 directory.
"""
import os
import signal
import sys
import time

from pydaos.raw import DaosApiError, DaosContext, DaosPool

_HEARTBEAT_INTERVAL_SEC = 2


def _terminate(_signum, _frame):
    # Exit without disconnecting so the handle stays live for revoke.
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

    # pidfile first (cleanup), handlers before connect, readyfile last.
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

    # Exit non-zero once the handle is evicted.
    while True:
        try:
            pool.pool_query()
        except DaosApiError as exc:
            print(f"pool_query failed: {exc}", file=sys.stderr)
            sys.exit(3)
        time.sleep(_HEARTBEAT_INTERVAL_SEC)


if __name__ == "__main__":
    main()
