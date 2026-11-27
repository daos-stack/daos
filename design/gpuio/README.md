# GPU Direct I/O Design Documents

GPU direct I/O lets an application pass GPU buffers to the DAOS object, array and DFS APIs,
so that the NIC moves data between GPU memory and the server without a host copy.

Start with `gpuio.md`. The other two build on it and are independent of each other.

| Document | What it covers | Who should read it |
|----------|----------------|--------------------|
| [gpuio.md](gpuio.md) | The base design: the `_ma` APIs, memory registration, client safety checks, errors, phases | everyone, first |
| [integrations.md](integrations.md) | How frameworks reach these APIs: a NIXL backend (Dynamo), a GDS backend in LMCache, a cuFile library | framework and integration developers |
| [server_ec.md](server_ec.md) | GPU I/O on erasure-coded objects, with parity computed on the server | DAOS object and EC developers |

All three are drafts. Nothing in them has run on GPU hardware yet.
