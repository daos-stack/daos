#!/usr/bin/env python3
"""
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent

Estimate the usable (post data-protection) free capacity of a DAOS pool.

`daos pool query` reports raw free space. This prototype (DAOS-19881) converts it into the
amount of user data that could still be written, assuming the object classes DAOS would pick by
default for POSIX container files (mirroring dc_set_oclass() in src/object/obj_class.c).

Only user-level access is needed: the `daos` command for JSON output, and the public C API in
libdaos (via ctypes) for pool properties, which the `daos` tool does not expose. If libdaos
cannot be loaded, `--rd-fac` or DAOS' default (3, clamped to domains-1) is used instead.
"""

import argparse
import ctypes
import ctypes.util
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from typing import Optional

# Thresholds from dc_set_oclass(): rf -> [(min_domains, k, p), ...] in descending order.
EC_TABLE = {
    0: [(18, 16, 1), (10, 8, 1), (6, 4, 1), (3, 2, 1)],
    1: [(18, 16, 1), (10, 8, 1), (6, 4, 1), (3, 2, 1)],
    2: [(20, 16, 2), (12, 8, 2), (8, 4, 2), (4, 2, 2)],
    3: [(22, 16, 3), (14, 8, 3), (10, 4, 3)],
}
# Minimum domains before an array object defaults to EC (0/4: never by default).
EC_MIN_DOMAINS = {1: 3, 2: 4, 3: 10}

POOL_RD_FAC_DEFAULT = 3

# VOS per-target system reserve (vos_space_sys_init / frag_reserve_space in src/vos/vos_space.c).
VOS_AGG_RESERVE = 5 * (8 << 20)
VOS_FRAG_MIN = 600 << 20
VOS_FRAG_MAX = 6 << 30

# From enum daos_pool_props in daos_prop.h.
DAOS_PROP_PO_SPACE_RB = 3
DAOS_PROP_PO_EC_CELL_SZ = 9
DAOS_PROP_PO_DATA_THRESH = 10
DAOS_PROP_PO_REDUN_FAC = 11
DAOS_PC_RO = 1


class EstimateError(Exception):
    """Fatal error while estimating."""


@dataclass(frozen=True)
class OClass:
    """Redundancy part of an object class; the group count is irrelevant for overhead."""

    kind: str  # "S", "RP" or "EC"
    replicas: int = 1
    k: int = 0
    p: int = 0

    @classmethod
    def parse(cls, name: str) -> "OClass":
        """Parse a class name such as S1, SX, RP_3G1, RP_4GX, EC_2P1GX or EC_16P2G8."""
        m = re.fullmatch(r"(?:OC_)?S(?:\d+|X)?(?:G.*)?", name)
        if m:
            return cls("S")
        m = re.fullmatch(r"(?:OC_)?RP_(\d+)(?:G.*)?", name)
        if m:
            return cls("RP", replicas=int(m.group(1)))
        m = re.fullmatch(r"(?:OC_)?EC_(\d+)P(\d+)(?:G.*)?", name)
        if m:
            return cls("EC", k=int(m.group(1)), p=int(m.group(2)))
        raise EstimateError(f"unrecognized object class name '{name}'")

    @property
    def grp_size(self) -> int:
        return self.k + self.p if self.kind == "EC" else self.replicas

    @property
    def name(self) -> str:
        if self.kind == "S":
            return "S"
        if self.kind == "RP":
            return f"RP_{self.replicas}"
        return f"EC_{self.k}P{self.p}"

    def overhead(self, small_frac: float = 0.0) -> float:
        """Raw bytes consumed per byte of user data."""
        if self.kind == "EC":
            full = (self.k + self.p) / self.k
            # Writes below a stripe are replicated (p+1)x instead of encoded.
            return (1.0 - small_frac) * full + small_frac * (self.p + 1)
        return float(self.replicas)


def default_class(rf: int, domain_nr: int, is_array: bool, rdd: Optional[str]) -> OClass:
    """Port of the first stage of dc_set_oclass(): class picked for OC_UNKNOWN.

    rdd is the redundancy hint: None, "RP" or "EC".
    """
    if rf > 4:
        raise EstimateError(f"invalid redundancy factor {rf}")

    want_ec = rdd == "EC" or (rdd is None and is_array)
    if rf == 0:
        if rdd == "RP" and domain_nr >= 2:
            oc = OClass("RP", replicas=2)
        elif rdd == "EC":
            oc = _ec_from_table(0, domain_nr)
        else:
            oc = OClass("S")
    elif rf == 4:
        oc = OClass("RP", replicas=5)
    elif want_ec and domain_nr >= EC_MIN_DOMAINS[rf]:
        oc = _ec_from_table(rf, domain_nr)
    else:
        oc = OClass("RP", replicas=rf + 1)

    if oc.grp_size > domain_nr:
        raise EstimateError(f"rf={rf} needs {oc.grp_size} fault domains, pool has {domain_nr}")
    return oc


def _ec_from_table(rf: int, domain_nr: int) -> OClass:
    for min_dom, k, p in EC_TABLE[rf]:
        if domain_nr >= min_dom:
            return OClass("EC", k=k, p=p)
    # dc_set_oclass() falls through to the smallest EC class for rf0 even below the threshold.
    _, k, p = EC_TABLE[rf][-1]
    return OClass("EC", k=k, p=p)


def parse_hints(hints: str, rf: int) -> dict:
    """Return {"file": rdd, "dir": rdd} following decode_one_hint() in src/client/dfs/common.c."""
    out = {"file": None, "dir": None}
    if not hints:
        return out
    for token in hints.split(","):
        if ":" not in token:
            raise EstimateError(f"invalid hint '{token}'")
        name, val = (s.strip().lower() for s in token.split(":", 1))
        if name in ("dir", "directory"):
            key = "dir"
        elif name == "file":
            key = "file"
        else:
            raise EstimateError(f"invalid hint type '{name}'")
        if val not in ("single", "max"):
            raise EstimateError(f"invalid hint value '{val}'")
        if rf == 0:
            out[key] = None
        elif key == "file" and val == "max":
            out[key] = "EC"
        else:
            out[key] = "RP"
    return out


class DaosHandle(ctypes.Structure):
    _fields_ = [("cookie", ctypes.c_uint64)]


class DaosPropEntry(ctypes.Structure):
    _fields_ = [("dpe_type", ctypes.c_uint32), ("dpe_flags", ctypes.c_uint16),
                ("dpe_reserv", ctypes.c_uint16), ("dpe_val", ctypes.c_uint64)]


class DaosProp(ctypes.Structure):
    _fields_ = [("dpp_nr", ctypes.c_uint32), ("dpp_reserv", ctypes.c_uint32),
                ("dpp_entries", ctypes.POINTER(DaosPropEntry))]


def query_pool_props(pool: str, sys_name: Optional[str], types, verbose: bool) -> dict:
    """Read integer pool properties through the public libdaos API; {} if unavailable."""
    libname = ctypes.util.find_library("daos") or "libdaos.so"
    try:
        lib = ctypes.CDLL(libname)
    except OSError as exc:
        if verbose:
            print(f"libdaos not loadable ({exc}); pool properties not queried", file=sys.stderr)
        return {}

    lib.daos_prop_alloc.restype = ctypes.POINTER(DaosProp)
    lib.daos_prop_alloc.argtypes = [ctypes.c_uint32]
    lib.daos_prop_free.argtypes = [ctypes.POINTER(DaosProp)]
    lib.daos_pool_connect.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint,
                                      ctypes.POINTER(DaosHandle), ctypes.c_void_p, ctypes.c_void_p]
    lib.daos_pool_query.argtypes = [DaosHandle, ctypes.c_void_p, ctypes.c_void_p,
                                    ctypes.POINTER(DaosProp), ctypes.c_void_p]
    lib.daos_pool_disconnect.argtypes = [DaosHandle, ctypes.c_void_p]

    rc = lib.daos_init()
    if rc != 0:
        raise EstimateError(f"daos_init() failed: rc={rc}")
    try:
        poh = DaosHandle()
        csys = sys_name.encode() if sys_name else None
        rc = lib.daos_pool_connect(pool.encode(), csys, DAOS_PC_RO, ctypes.byref(poh), None, None)
        if rc != 0:
            raise EstimateError(f"daos_pool_connect({pool}) failed: rc={rc}")
        try:
            prop = lib.daos_prop_alloc(len(types))
            if not prop:
                raise EstimateError("daos_prop_alloc() failed")
            try:
                for i, ptype in enumerate(types):
                    prop.contents.dpp_entries[i].dpe_type = ptype
                rc = lib.daos_pool_query(poh, None, None, prop, None)
                if rc != 0:
                    raise EstimateError(f"daos_pool_query({pool}) failed: rc={rc}")
                return {prop.contents.dpp_entries[i].dpe_type: prop.contents.dpp_entries[i].dpe_val
                        for i in range(len(types))}
            finally:
                lib.daos_prop_free(prop)
        finally:
            lib.daos_pool_disconnect(poh, None)
    finally:
        lib.daos_fini()


class DaosCli:
    """Thin wrapper around `daos ... --json`."""

    def __init__(self, daos_bin: str, verbose: bool):
        self.daos_bin = daos_bin
        self.verbose = verbose

    def _run(self, argv) -> dict:
        if self.verbose:
            print("+ " + " ".join(argv), file=sys.stderr)
        try:
            proc = subprocess.run(argv, capture_output=True, text=True, check=False)
        except FileNotFoundError as exc:
            raise EstimateError(f"cannot execute {argv[0]}: {exc}") from exc
        # The daos tool may log to stdout before the JSON document; find the document start.
        out = proc.stdout
        start = out.find("{")
        if start < 0:
            raise EstimateError(f"{' '.join(argv)} produced no JSON (rc={proc.returncode}):\n"
                                f"{proc.stderr.strip()}")
        try:
            doc = json.loads(out[start:])
        except json.JSONDecodeError as exc:
            raise EstimateError(f"{' '.join(argv)}: bad JSON: {exc}") from exc
        if doc.get("status", 0) != 0 or doc.get("error"):
            raise EstimateError(f"{' '.join(argv)} failed: {doc.get('error')}")
        return doc["response"]

    def daos(self, *args) -> dict:
        return self._run([self.daos_bin, *args, "--json"])


@dataclass
class Topology:
    rank_host: dict  # rank -> host string

    def domains(self, ranks, rd_lvl: str) -> int:
        if rd_lvl == "rank":
            return len(ranks)
        hosts = {self.rank_host.get(r) for r in ranks}
        hosts.discard(None)
        return len(hosts)


def host_of_uri(uri: str) -> str:
    """ofi+tcp://10.0.0.1:31417 -> 10.0.0.1"""
    rest = uri.split("://", 1)[-1]
    if rest.startswith("["):
        return rest[1:rest.find("]")]
    return rest.rsplit(":", 1)[0]


def get_topology(cli: DaosCli) -> Topology:
    sysinfo = cli.daos("system", "query")
    return Topology({r["rank"]: host_of_uri(r["uri"]) for r in sysinfo.get("rank_uris", [])})


def prop_value(props, name: str):
    for prop in props:
        if prop.get("name") == name:
            return prop.get("value")
    return None


@dataclass
class Row:
    label: str
    rf: int
    rd_lvl: str
    domains: int
    oclass: OClass
    note: str = ""


def fmt_bytes(nbytes: float) -> str:
    units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
    val = float(nbytes)
    for unit in units:
        if val < 1024 or unit == units[-1]:
            return f"{val:.1f} {unit}" if unit != "B" else f"{int(val)} B"
        val /= 1024
    return f"{val:.1f} PiB"


def vos_sys_reserve(tgt_total: int, is_scm: bool) -> int:
    """Approximate the per-target space VOS withholds from `free` (GC bags ignored)."""
    rsrvd = VOS_AGG_RESERVE
    if not is_scm:
        return rsrvd
    if tgt_total <= 2 * rsrvd:
        return 0
    ovhd = tgt_total * 5 // 100
    if tgt_total < 2 * (ovhd + rsrvd):
        return rsrvd
    ovhd = min(ovhd, VOS_FRAG_MAX)
    if ovhd < VOS_FRAG_MIN and tgt_total >= 2 * (VOS_FRAG_MIN + rsrvd):
        ovhd = VOS_FRAG_MIN
    return max(rsrvd, ovhd)


def rb_extra_reserve(tier: dict, space_rb: int, ntargets: int) -> int:
    """Part of the rebuild reserve not already hidden by the system reserve.

    vos_space_hold() checks `free >= sys` and `free >= total*space_rb/100` independently, so
    the effective reserve is max(sys, rb) while the reported free is already `free - sys`.
    """
    if not space_rb or ntargets <= 0:
        return 0
    tgt_total = tier["total"] // ntargets
    rb = tgt_total * space_rb // 100
    sys_rsv = vos_sys_reserve(tgt_total, tier.get("media_type") == "scm")
    return max(rb - sys_rsv, 0) * ntargets


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Estimate the usable (post data-protection) free capacity of a DAOS pool.")
    parser.add_argument("pool", nargs="?", help="pool label or UUID")
    parser.add_argument("--cont", action="append", default=[],
                        help="only report these containers (repeatable); default: all")
    parser.add_argument("--rd-fac", type=int, help="pool redundancy factor override")
    parser.add_argument("--space-rb", type=int, help="pool rebuild reserve percent override")
    parser.add_argument("--sys", help="DAOS system name (default: from agent)")
    parser.add_argument("--domains", type=int, help="override number of fault domains (nodes)")
    parser.add_argument("--small-file-pct", type=float, default=0.0,
                        help="percent of data in writes below one EC stripe (replicated p+1x)")
    parser.add_argument("--daos-bin", default="daos", help="path to the daos binary")
    parser.add_argument("-v", "--verbose", action="store_true", help="print the commands run")
    parser.add_argument("--self-test", action="store_true", help="run built-in checks and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.pool:
        parser.error("pool is required")
    if not 0 <= args.small_file_pct <= 100:
        parser.error("--small-file-pct must be within 0..100")
    if args.space_rb is not None and not 0 <= args.space_rb < 100:
        parser.error("--space-rb must be within 0..99")

    try:
        report(args)
    except EstimateError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def report(args) -> None:
    cli = DaosCli(args.daos_bin, args.verbose)
    small_frac = args.small_file_pct / 100.0

    pinfo = cli.daos("pool", "query", args.pool, "--show-enabled")
    topo = get_topology(cli)
    ranks = pinfo.get("enabled_ranks") or sorted(topo.rank_host)
    pool_nodes = args.domains or topo.domains(ranks, "node")
    pool_ranks = len(ranks)

    tiers = [t for t in pinfo.get("tier_stats", []) if t.get("total", 0) > 0]
    raw_total = sum(t["total"] for t in tiers)

    rd_fac, space_rb, prop_src = pool_props(args, pool_nodes)

    for t in tiers:
        t["rb_reserve"] = rb_extra_reserve(t, space_rb, pinfo["active_targets"])
        t["writable"] = max(t["free"] - t["rb_reserve"], 0)
    raw_free = sum(t["writable"] for t in tiers)

    rows = []
    rows.append(Row("<pool default>", rd_fac, "node", pool_nodes,
                    default_class(rd_fac, pool_nodes, True, None),
                    f"rd_fac from {prop_src}; files only, no hints"))

    conts = cli.daos("pool", "list-containers", args.pool) or []
    for cont in conts:
        cid = cont.get("label") or cont["uuid"]
        if args.cont and cid not in args.cont and cont["uuid"] not in args.cont:
            continue
        rows.append(container_row(cli, args.pool, cid, topo, ranks, args.domains))

    print(f"Pool {pinfo.get('label') or pinfo['uuid']}: {pool_ranks} engines on {pool_nodes} "
          f"nodes, {pinfo['active_targets']}/{pinfo['total_targets']} targets active")
    for t in tiers:
        print(f"  {t['media_type'].upper():5s} total {fmt_bytes(t['total']):>10s}  "
              f"free {fmt_bytes(t['free']):>10s}  "
              f"(min/target {fmt_bytes(t['min'])}, max/target {fmt_bytes(t['max'])})")
    if space_rb:
        rb_total = sum(t["rb_reserve"] for t in tiers)
        print(f"  rebuild reserve (space_rb={space_rb}%): {fmt_bytes(rb_total)} beyond the "
              f"system reserve already excluded from free")
    print(f"  raw writable (all tiers): {fmt_bytes(raw_free)} of {fmt_bytes(raw_total)}")
    if small_frac:
        print(f"  assuming {args.small_file_pct:g}% of data written as sub-stripe (small) writes")
    print()

    hdr = f"{'Container':<28s} {'RF':>2s} {'Lvl':<4s} {'Dom':>3s} {'File class':<10s} " \
          f"{'Overhead':>8s} {'Usable free':>12s}  Note"
    print(hdr)
    print("-" * len(hdr))
    for row in rows:
        ovh = row.oclass.overhead(small_frac)
        print(f"{row.label:<28s} {row.rf:>2d} {row.rd_lvl:<4s} {row.domains:>3d} "
              f"{row.oclass.name:<10s} {ovh:>7.2f}x {fmt_bytes(raw_free / ovh):>12s}  {row.note}")

    print()
    print("Caveats: estimates assume large files written in full stripes; directory, inode and")
    print("per-write metadata are ignored (~3% observed); per-file object class overrides are not")
    print("detectable; space is an aggregate, so an uneven fill returns ENOSPC earlier, and a")
    print("single-group class (…G1, hint *:single) caps each file at one target's free space.")


def pool_props(args, nodes: int):
    """Return (rd_fac, space_rb, source) honouring overrides and the libdaos fallback."""
    wanted = []
    if args.rd_fac is None:
        wanted.append(DAOS_PROP_PO_REDUN_FAC)
    if args.space_rb is None:
        wanted.append(DAOS_PROP_PO_SPACE_RB)
    props = query_pool_props(args.pool, args.sys, wanted, args.verbose) if wanted else {}

    if args.rd_fac is not None:
        rd_fac, src = args.rd_fac, "--rd-fac"
    elif DAOS_PROP_PO_REDUN_FAC in props:
        rd_fac, src = int(props[DAOS_PROP_PO_REDUN_FAC]), "pool property"
    else:
        # The pool default is clamped at creation so that rd_fac + 1 <= domains.
        rd_fac, src = min(POOL_RD_FAC_DEFAULT, max(nodes - 1, 0)), \
            "DAOS default (libdaos unavailable)"

    if args.space_rb is not None:
        space_rb = args.space_rb
    else:
        space_rb = int(props.get(DAOS_PROP_PO_SPACE_RB, 0))
    return rd_fac, space_rb, src


def container_row(cli: DaosCli, pool: str, cid: str, topo: Topology, ranks, dom_override) -> Row:
    cinfo = cli.daos("cont", "query", pool, cid)
    rf = int(cinfo.get("redundancy_factor", 0))
    if cinfo.get("container_type") != "POSIX":
        props = cli.daos("cont", "get-prop", pool, cid)
        rd_lvl = str(prop_value(props, "rd_lvl") or "node")
        dom = dom_override or topo.domains(ranks, rd_lvl)
        return Row(cid, rf, rd_lvl, dom, default_class(rf, dom, False, None),
                   f"non-POSIX ({cinfo.get('container_type')}); assuming replication")

    props = cli.daos("cont", "get-prop", pool, cid)
    rd_lvl = str(prop_value(props, "rd_lvl") or "node")
    dom = dom_override or topo.domains(ranks, rd_lvl)

    reported = OClass.parse(cinfo["file_object_class"])
    hints = cinfo.get("hints", "")
    note = ""
    # dfs_query() resolves the default without hints, so apply them here unless the class was
    # explicitly set (in which case it differs from the hint-less default).
    file_rdd = parse_hints(hints, rf)["file"]
    try:
        table_default = default_class(rf, dom, True, None)
    except EstimateError:
        table_default = None
    if hints and table_default == reported:
        oclass = default_class(rf, dom, True, file_rdd)
        note = f"hints '{hints}'"
    else:
        oclass = reported
        if hints:
            note = f"explicit file class; hints '{hints}' ignored"
        elif table_default != reported:
            note = "explicit file class"
    return Row(cid, rf, rd_lvl, dom, oclass, note)


def self_test() -> int:
    checks = [
        # (rf, domains, is_array, rdd, expected)
        (0, 2, True, None, "S"),
        (0, 4, True, "RP", "RP_2"),
        (0, 6, True, "EC", "EC_4P1"),
        (1, 2, True, None, "RP_2"),
        (1, 3, True, None, "EC_2P1"),
        (1, 6, True, None, "EC_4P1"),
        (1, 10, True, None, "EC_8P1"),
        (1, 18, True, None, "EC_16P1"),
        (1, 18, False, None, "RP_2"),
        (1, 18, True, "RP", "RP_2"),
        (2, 3, True, None, "RP_3"),
        (2, 4, True, None, "EC_2P2"),
        (2, 8, True, None, "EC_4P2"),
        (2, 12, True, None, "EC_8P2"),
        (2, 20, True, None, "EC_16P2"),
        (3, 6, True, None, "RP_4"),
        (3, 10, True, None, "EC_4P3"),
        (3, 14, True, None, "EC_8P3"),
        (3, 22, True, None, "EC_16P3"),
        (4, 22, True, None, "RP_5"),
    ]
    failed = 0
    for rf, dom, is_array, rdd, expected in checks:
        got = default_class(rf, dom, is_array, rdd).name
        if got != expected:
            print(f"FAIL default_class({rf}, {dom}, {is_array}, {rdd}) = {got}, want {expected}")
            failed += 1
    for name, expected in [("S1", "S"), ("SX", "S"), ("RP_3G1", "RP_3"), ("RP_4G12", "RP_4"),
                           ("EC_2P1GX", "EC_2P1"), ("EC_16P2G8", "EC_16P2"),
                           ("OC_EC_4P2G1", "EC_4P2")]:
        got = OClass.parse(name).name
        if got != expected:
            print(f"FAIL parse({name}) = {got}, want {expected}")
            failed += 1
    for hints, rf, expected in [("file:single", 2, "RP"), ("file:max", 2, "EC"),
                                ("file:max", 0, None), ("dir:max,file:single", 1, "RP")]:
        got = parse_hints(hints, rf)["file"]
        if got != expected:
            print(f"FAIL parse_hints({hints}, {rf}) = {got}, want {expected}")
            failed += 1
    ec = OClass("EC", k=4, p=2)
    if abs(ec.overhead() - 1.5) > 1e-9 or abs(ec.overhead(1.0) - 3.0) > 1e-9:
        print("FAIL EC overhead")
        failed += 1
    if host_of_uri("ofi+tcp://10.214.208.197:31417") != "10.214.208.197":
        print("FAIL host_of_uri")
        failed += 1
    # 125 MB SCM targets (observed sys 42.6 MB): tiny pool => agg reserve only, frag disabled.
    if vos_sys_reserve(125001728, True) != VOS_AGG_RESERVE:
        print("FAIL vos_sys_reserve tiny")
        failed += 1
    # 100 GiB SCM target => 5% = 5 GiB frag reserve dominates.
    if vos_sys_reserve(100 << 30, True) != 5 << 30:
        print("FAIL vos_sys_reserve frag")
        failed += 1
    # 1 TiB NVMe target: 30% rb minus 40 MiB agg reserve.
    nvme = {"total": 1 << 40, "media_type": "nvme"}
    if rb_extra_reserve(nvme, 30, 1) != (1 << 40) * 30 // 100 - VOS_AGG_RESERVE:
        print("FAIL rb_extra_reserve nvme")
        failed += 1
    # rb (30% of 125 MB = 37.5 MB) below sys (40 MiB) => nothing extra withheld.
    if rb_extra_reserve({"total": 125001728 * 96, "media_type": "scm"}, 30, 96) != 0:
        print("FAIL rb_extra_reserve hidden")
        failed += 1
    print("self-test " + ("FAILED" if failed else "passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
