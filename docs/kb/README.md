# DAOS Knowledge Base

A compact, link-checked map of the DAOS code base, written for LLM agents (and humans) who need to
find their way around `src/` quickly. Prefer these files over broad searches; follow the paths and
`file::symbol` references they contain to jump straight to code.

## How to use

1. Start with [architecture.md](architecture.md) (processes, layers, channels, module index) and
   [concepts.md](concepts.md) (vocabulary mapped to code).
2. For a module, open its card in [modules/](modules/). Cards list purpose, file map, entry points,
   key structs, threading, invariants, and where to look for common tasks.
3. For cross-module behaviour, open a flow trace in [flows/](flows/). Each is an ordered call chain
   with `file::function` at every hop.
4. For exhaustive facts, use the generated tables (regenerated from source, never hand-edited):
   - [generated/rpcs.md](generated/rpcs.md): every CaRT RPC opcode -> handler -> file.
   - [generated/drpc.md](generated/drpc.md): every dRPC method -> Go caller -> engine handler.
   - [generated/modules.md](generated/modules.md): engine modules and include-derived dependencies.
   - [generated/graph.json](generated/graph.json): the same facts plus card metadata as a node/edge
     graph.

## Contents

| Kind | File | Covers |
|---|---|---|
| Overview | [architecture.md](architecture.md) | Processes, layering, communication, module index |
| Overview | [concepts.md](concepts.md) | Pool, container, object, epoch, DTX, target, xstream, ... |
| Module | [modules/vos.md](modules/vos.md) | Versioning Object Store (per-target persistent store) |
| Module | [modules/dtx.md](modules/dtx.md) | Distributed transactions |
| Module | [modules/object.md](modules/object.md) | Object client/server I/O, EC, migration |
| Module | [modules/pool.md](modules/pool.md) | Pool client, pool service, pool map |
| Module | [modules/rebuild.md](modules/rebuild.md) | Rebuild, reclaim and object migration |
| Module | [modules/engine.md](modules/engine.md) | `daos_engine` runtime: startup, xstreams, ULTs, scheduler, dRPC server, IV |
| Module | [modules/control.md](modules/control.md) | Go control plane: `daos_server`, MS, `dmg`, `daos_agent` |
| Flow | [flows/write-path.md](flows/write-path.md) | `dfs_write()` down to SCM/NVMe |
| Flow | [flows/pool-create.md](flows/pool-create.md) | `dmg pool create` through control plane, engine, rsvc, rdb |
| Flow | [flows/rebuild.md](flows/rebuild.md) | Rank failure / `dmg pool exclude` -> pool map update -> rebuild -> reclaim |
| Flow | [flows/drive-replace.md](flows/drive-replace.md) | NVMe replacement and engine recovery -> device and pool verification |

## Maintenance

- `utils/kb/kb.py extract` regenerates `generated/`.
- `utils/kb/kb.py check` fails if `generated/` is stale or if any path, link, or symbol referenced
  by the KB or by `.github/` instructions no longer exists. Run it after renaming code.
- `utils/kb/benchmark/questions.md` holds benchmark questions with verified answers for
  measuring the KB (baseline vs. KB runs); its anchors are linted too.
- Conventions for hand-written files:
  - Reference code as `<path>::<symbol>` (checked: the file must contain the symbol) or
    `<symbol>()` (checked to exist somewhere under `src/`). Never cite line numbers.
  - Keep each card under ~200 lines; link instead of copying.
  - Cards start with frontmatter (`id`, `kind`, `depends_on`, `used_by`, `related`) which
    `kb.py extract` merges into `generated/graph.json`.
