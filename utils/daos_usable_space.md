# daos_usable_space.py — usable capacity estimator (prototype)

`daos pool query` reports *raw* free space. To know how much data can still be written, a user
has to know the data-protection scheme DAOS will apply, which depends on the pool/container
redundancy factor, the number of fault domains and the object classes DAOS picks by default.
This prototype ([DAOS-19881](https://daosio.atlassian.net/browse/DAOS-19881)) does that
translation outside of DAOS so the model can be iterated quickly before being folded into
`daos pool query`.

It is a single Python 3 script with no dependencies beyond the standard library and a DAOS
client install (the `daos` command and, optionally, `libdaos.so`).

## How it works

1. `daos pool query --show-enabled --json` gives the raw free/total per storage tier and the
   enabled ranks. The reported `free` already excludes the VOS system reserve (GC/aggregation
   reserve, SCM fragmentation reserve), so that is not re-subtracted.
2. `daos system query --json` maps ranks to hosts; the pool's fault-domain count is the number
   of distinct hosts (redundancy level `node`) or ranks (`rank`).
3. The pool properties `rd_fac` and `space_rb` are read with the public C API
   (`daos_pool_query()` with a property list, via `ctypes` on `libdaos.so`), because the `daos`
   tool does not expose pool properties. The rebuild reserve (`space_rb` % of each target's
   total) is *not* deducted from the reported `free`; it is enforced at write time, independently
   of the system reserve, so the effective reserve is `max(sys, rb)`. The tool models the system
   reserve (40 MiB + 5 % of SCM clamped to 600 MB–6 GB per target) and withholds only
   `max(0, rb − sys)` per target.
4. For every container, `daos cont query --json` and `daos cont get-prop --json` provide the
   redundancy factor/level, the resolved default file object class, hints and chunk size.
5. The redundancy part of the object class (`S`, `RP_n`, `EC_kPp`) is turned into a raw-bytes
   per-user-byte overhead, and `usable = raw_free / overhead`.

## Assumptions

* **Only file data counts.** Directory entries, inodes, dkey/akey metadata and checksums are
  ignored. Measured metadata overhead for 256 MiB files was ~2–3 %.
* **Default object classes.** A user can create any file with an explicit object class
  (`daos fs set-attr`, `dfs_obj_set_oclass`, application hints); this is not detectable from
  outside and is not modelled. The container-level default is used, reproduced from
  `dc_set_oclass()` in `src/object/obj_class.c`:

  | RF | fault domains → default file class (array objects)            | directories |
  |----|-----------------------------------------------------------------|-------------|
  | 0  | `S` (1×)                                                        | `S`         |
  | 1  | ≥18 `EC_16P1`, ≥10 `EC_8P1`, ≥6 `EC_4P1`, ≥3 `EC_2P1`, else `RP_2` | `RP_2`   |
  | 2  | ≥20 `EC_16P2`, ≥12 `EC_8P2`, ≥8 `EC_4P2`, ≥4 `EC_2P2`, else `RP_3` | `RP_3`   |
  | 3  | ≥22 `EC_16P3`, ≥14 `EC_8P3`, ≥10 `EC_4P3`, else `RP_4`          | `RP_4`      |
  | 4  | `RP_5`                                                          | `RP_5`      |

  DFS hints override the choice: `file:single` forces replication, `file:max` forces EC. The
  `daos cont query` output does not reflect hints (it resolves the default without them), so the
  tool re-applies the hint itself; `daos fs get-attr` on a real file was used to confirm this.
* **Overhead.** `RP_n` → `n`×, `EC_kPp` → `(k+p)/k`×, `S` → 1×. EC writes smaller than a full
  stripe (and single values up to `((k/8)+1)·4 KiB`) are stored as `p+1` replicas instead of
  being encoded; `--small-file-pct P` blends `P %` of the data at `(p+1)`× to model small-file
  workloads. The default is 0 (large files).
* **Pool-level row.** Uses the pool's `rd_fac` property, redundancy level `node`, no hints. The
  pool default `rd_fac` (3, clamped to `domains-1`) is inherited by containers created without
  an explicit `rd_fac`, so on small clusters the pool-level default is often `RP_4` (4×).
* **Aggregate free space, tiers summed.** The estimate uses the sum of `free` over all tiers.
  It does not model the `data_thresh` split (small writes go to the SCM/meta tier) and it
  ignores per-target imbalance. Both make the estimate optimistic:
  * in a fill test on a 12-rank SCM-only pool, writes stopped with `DER_NOSPACE` after 69 % of
    the reported free space because one target filled first (per-target free was 25 MB min vs
    79 MB max at that point);
  * single-group classes (`…G1`, produced by `*:single` hints) place a whole file on one target
    group, so a single file can hit ENOSPC at roughly `free / targets`.
* **Non-POSIX containers** are reported assuming `RP_(rf+1)` since there is no file class.
* **Fallbacks.** If `libdaos.so` cannot be loaded the pool `rd_fac` is assumed to be the DAOS
  default and `space_rb` to be 0 (noted in the output); `--rd-fac`, `--space-rb` and
  `--domains` override the queried values.

## Usage

```
daos_usable_space.py [-h] [--cont CONT] [--rd-fac RD_FAC] [--space-rb SPACE_RB] [--sys SYS]
                     [--domains DOMAINS] [--small-file-pct SMALL_FILE_PCT]
                     [--daos-bin DAOS_BIN] [-v] [--self-test] [pool]

positional arguments:
  pool                  pool label or UUID

optional arguments:
  --cont CONT           only report these containers (repeatable); default: all
  --rd-fac RD_FAC       pool redundancy factor override
  --space-rb SPACE_RB   pool rebuild reserve percent override
  --sys SYS             DAOS system name (default: from agent)
  --domains DOMAINS     override number of fault domains (nodes)
  --small-file-pct SMALL_FILE_PCT
                        percent of data in writes below one EC stripe (replicated p+1x)
  --daos-bin DAOS_BIN   path to the daos binary
  -v, --verbose         print the commands run
  --self-test           run built-in checks and exit
```

Requirements: a running `daos_agent`, `daos` in `PATH` (or `--daos-bin`), and `libdaos.so`
resolvable through `LD_LIBRARY_PATH`/ldconfig for the pool property lookup. The user needs read
access to the pool and the containers being reported.

`--self-test` checks the object-class table, class-name parser and hint parser against the
values expected from `dc_set_oclass()` and needs no DAOS system.

## Examples

Pool on 6 nodes × 2 engines (12 ranks, 96 targets), SCM only, pool `rd_fac=3`, several POSIX
containers with different properties:

```
$ daos_usable_space.py usable_test
Pool usable_test: 12 engines on 6 nodes, 96/96 targets active
  SCM   total   22.4 GiB  free   18.5 GiB  (min/target 197.1 MiB, max/target 197.1 MiB)
  raw writable (all tiers): 18.5 GiB of 22.4 GiB

Container                    RF Lvl  Dom File class Overhead  Usable free  Note
-------------------------------------------------------------------------------
<pool default>                3 node   6 RP_4          4.00x      4.6 GiB  rd_fac from pool property; files only, no hints
c_rf1                         1 node   6 EC_4P1        1.25x     14.8 GiB
c_default                     3 node   6 RP_4          4.00x      4.6 GiB
c_rf1_single                  1 node   6 RP_2          2.00x      9.2 GiB  hints 'file:single'
c_rf0                         0 node   6 S             1.00x     18.5 GiB
c_kv                          1 node   6 RP_2          2.00x      9.2 GiB  non-POSIX (unknown); assuming replication
c_rf1_rp                      1 node   6 RP_3          3.00x      6.2 GiB  explicit file class
c_rf2_rank                    2 rank  12 EC_8P2        1.25x     14.8 GiB

Caveats: estimates assume large files written in full stripes; directory, inode and
per-write metadata are ignored (~3% observed); per-file object class overrides are not
detectable; space is an aggregate, so an uneven fill returns ENOSPC earlier, and a
single-group class (…G1, hint *:single) caps each file at one target's free space.
```

How the containers were created:

```
daos cont create usable_test c_default    --type POSIX
daos cont create usable_test c_rf0        --type POSIX --properties rd_fac:0
daos cont create usable_test c_rf1        --type POSIX --properties rd_fac:1
daos cont create usable_test c_rf1_single --type POSIX --properties rd_fac:1 --hints file:single
daos cont create usable_test c_rf1_rp     --type POSIX --properties rd_fac:1 --file-oclass RP_3GX
daos cont create usable_test c_rf2_rank   --type POSIX --properties rd_fac:2,rd_lvl:1
daos cont create usable_test c_kv                      --properties rd_fac:1
```

Modelling a workload where 30 % of the data is written in sub-stripe (small) writes:

```
$ daos_usable_space.py usable_test --cont c_rf1 --cont c_rf2_rank --small-file-pct 30
...
Container                    RF Lvl  Dom File class Overhead  Usable free  Note
-------------------------------------------------------------------------------
<pool default>                3 node   6 RP_4          4.00x      4.6 GiB  rd_fac from pool property; files only, no hints
c_rf1                         1 node   6 EC_4P1        1.48x     12.5 GiB
c_rf2_rank                    2 rank  12 EC_8P2        1.77x     10.4 GiB
```

Without `libdaos.so` reachable, the pool row falls back to the DAOS default:

```
$ LD_LIBRARY_PATH= daos_usable_space.py usable_test --cont c_default
...
<pool default>                3 node   6 RP_4          4.00x      4.6 GiB  rd_fac from DAOS default (libdaos unavailable); files only, no hints
```

Pool created with `--properties rd_fac:0,space_rb:60` (1 GiB SCM per rank, so 125 MB per
target; the 60 % rebuild reserve of 75 MB exceeds the 40 MiB system reserve by 33 MB per target):

```
$ daos_usable_space.py usable_rb60
Pool usable_rb60: 12 engines on 6 nodes, 96/96 targets active
  SCM   total   11.2 GiB  free    7.3 GiB  (min/target 78.0 MiB, max/target 78.0 MiB)
  rebuild reserve (space_rb=60%): 3.0 GiB beyond the system reserve already excluded from free
  raw writable (all tiers): 4.4 GiB of 11.2 GiB

Container                    RF Lvl  Dom File class Overhead  Usable free  Note
-------------------------------------------------------------------------------
<pool default>                0 node   6 S             1.00x      4.4 GiB  rd_fac from pool property; files only, no hints
c0                            0 node   6 S             1.00x      4.4 GiB
```

With `space_rb:30` on the same layout the 37.5 MB rebuild reserve is below the system reserve
and the tool reports `0 B beyond the system reserve`; a fill test confirmed targets went all the
way to 0 B free.

## Measured vs. predicted

Writing 256 MiB files through dfuse and comparing the drop in raw `free` with the bytes written:

| container      | predicted | measured | note                                           |
|----------------|-----------|----------|------------------------------------------------|
| `c_rf1` (EC_4P1)       | 1.25× | 1.27× | ~2 % metadata                                   |
| `c_rf1_single` (RP_2G1)| 2.00× | 2.07× | ENOSPC after 165 MB: G1 = one target pair       |
| `c_rf0` (S) fill test  | 18.5 GiB | 12.7 GiB written | stopped by per-target imbalance (69 %) |
| `usable_rb60` fill test | 4.4 GiB | 2.85 GiB written | first full target stopped at 33.1 MB free, matching `rb − sys` exactly; the rest is imbalance (65 %) |

## Possible next steps

* Report a conservative "guaranteed" figure from `min free per target × active targets`, which
  `tier_stats` already provides, next to the optimistic aggregate.
* Read `ec_cell_sz` and `data_thresh` through the same property lookup to model the SCM/NVMe
  split for MD-on-SSD / mixed-tier pools instead of summing the tiers.
* Once the model is agreed, port it into `daos pool query` (the Go `PoolQuery` already queries
  pool properties through `daos_pool_query()`).
