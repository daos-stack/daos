---
applyTo: "src/vos/**,src/include/daos_srv/vos.h,src/include/daos_srv/vos_types.h"
description: "VOS (Versioning Object Store) context and rules"
---
# Working in VOS

Read [docs/kb/modules/vos.md](../../docs/kb/modules/vos.md) first; for how VOS is reached from the
network see [docs/kb/flows/write-path.md](../../docs/kb/flows/write-path.md).

- VOS is per target and single-threaded per target xstream; it has no locking across xstreams and
  no network code. Never call it from the system xstream for a target's pool.
- Never yield (ABT yield, blocking I/O, RPC wait) inside a `umem` transaction
  (`vos_tx_begin()` ... `vos_tx_end()`).
- Everything is epoch-versioned (MVCC); punch is a tombstone recorded in the ilog. Do not update
  in place.
- `*_begin()` reserves resources; always pair with `*_end()` passing the error so reservations are
  cancelled.
- Any change to `src/vos/vos_layout.h` is an on-media format change: bump the layout version and
  handle upgrade of existing pools.
- Unit tests: `src/vos/tests` (`vos_tests`); add a test with each behaviour change.
