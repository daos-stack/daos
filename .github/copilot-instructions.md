# DAOS: guidance for Copilot

DAOS is a distributed object store: Go control plane (`src/control`), C data plane
(`daos_engine`, `src/engine` + server modules) and C client library (`libdaos`, `src/client` +
`cli*.c` in pool/container/object).

## Use the knowledge base first

Before searching `src/` broadly, read the relevant files in [docs/kb](../docs/kb/README.md):

- [docs/kb/architecture.md](../docs/kb/architecture.md): processes, channels (gRPC / dRPC / CaRT),
  engine xstreams, module index.
- [docs/kb/concepts.md](../docs/kb/concepts.md): pool, container, object, epoch, DTX, target, ... mapped to code.
- Module cards in [docs/kb/modules](../docs/kb/modules): vos, dtx, object, pool, rebuild, engine,
  control (Go control plane).
- Flow traces in [docs/kb/flows](../docs/kb/flows): write path, pool create, rebuild.
- Generated lookups:
  - "who handles RPC X?": [docs/kb/generated/rpcs.md](../docs/kb/generated/rpcs.md)
  - "who handles dRPC method X / who calls it from Go?": [docs/kb/generated/drpc.md](../docs/kb/generated/drpc.md)
  - "what does module X depend on / who uses it?": [docs/kb/generated/modules.md](../docs/kb/generated/modules.md)

KB references use `path::symbol`; jump to the file and search for the symbol (no line numbers).
If a KB statement contradicts the code, trust the code and fix the KB.

## Code conventions

- C style: `.clang-format`; function return type on its own line, name at column 0 (so
  `^name(` finds definitions).
- Errors are negative `-DER_*` codes (`src/include/daos_errno.h`); log with `D_ERROR`/`DL_ERROR`
  etc. from `src/include/gurt/debug.h`; goto-based cleanup (`D_GOTO(out, rc = ...)`).
- Memory: `D_ALLOC*`/`D_FREE` (never raw malloc/free in DAOS code).
- Engine code runs in Argobots ULTs: never block an xstream (no sleeping syscalls, no pthread
  waits); yield or use ABT primitives. Never yield inside a `umem`/VOS transaction.
- RPCs are declared with X-macro lists (`*_PROTO_*_RPC_LIST`) and `CRT_RPC_DECLARE`; changing a
  wire format requires a protocol version bump.
- Go code is under `src/control`; ignore `src/control/vendor`.

## Build and test

- Build: `scons --jobs=$(nproc) install` (see [docs/dev/development.md](../docs/dev/development.md)).
- C unit tests: `utils/run_utest.py` (config `utils/utest.yaml`); Go: `src/control/run_go_tests.sh`.
- Functional tests: `src/tests/ftest` (Avocado, `launch.py`).

## Keeping the KB correct

After renaming/moving code referenced by the KB, run `utils/kb/kb.py check` (regenerates nothing,
fails on stale generated files or broken references); fix with `utils/kb/kb.py extract` and by
editing the cards.
