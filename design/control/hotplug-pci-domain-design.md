
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent

# High-Level Design: PCI Domain-Aware Hotplug Bus-ID Filtering

## 1. Summary

Each DAOS engine's hotplug bus-ID filter restricts SPDK NVMe hotplug add/remove event handling to the PCI addresses belonging to that engine, so that a hotplug event for a device belonging to a different engine is never mis-claimed. The filter was originally bus-ID-only, implicitly assuming a single PCI domain (`0000`) across the whole host — a false assumption on platforms with a socket-partitioned, multi-segment PCIe topology (e.g. Gen-12 HPE hardware, Section 2.1) where a single engine's NVMe SSDs can be enumerated across several distinct, non-zero PCI domains (segments), one per CPU socket.

This design makes the hotplug filter fully PCI-domain-aware and multi-domain-capable:

- A new config parameter, **`bdev_hotplug_range`**, holds a **list** of filter entries. Every entry mandatorily specifies both a PCI domain and a bus-ID, each independently a single value or an inclusive range, in hex (`0x`-prefixed) notation only.
- The filter is derived, automatically or explicitly, depending on whether it is specified in the server config file.
- If not specified then the filter is derived automatically to cover **exactly** the PCI addresses in the engine's `bdev_list`.
- If specified manually in the config file it is allowed to have a broader range filter than just the `bdev_list` addresses.
- The existing `bdev_busid_range` parameter is retained as a deprecated legacy alias and converted internally to a single `bdev_hotplug_range` entry on PCI domain `0x00` specifically.
- The prior `ignore_domain`/"any domain" special case is removed entirely: VMD now configures **zero** `bdev_hotplug_range` entries rather than a domain-agnostic wildcard entry, relying on the existing default-allow behavior when no filter entries are present.

All of the above — domain awareness, `bdev_hotplug_range`, the `bdev_list`-restricted derivation, and the legacy-alias conversion — is delivered as a single change in `PR #19259` (DAOS-19772).

## 2. Background

### 2.1 The single-domain assumption

DAOS's original hotplug bus-ID filtering (`bdev_busid_range`, its topology-based auto-derivation, and the SPDK-side `hotplug_filter_fn`) assumed a single PCI domain (`0000`) spans the entire host, with NUMA nodes/sockets distinguished only by disjoint bus-ID ranges within that one domain. This matches platforms with a single PCI segment, where all PCIe devices on the host — regardless of which CPU socket they are attached to — share domain `0000` and are differentiated only by bus-ID. HPE Gen-12 platforms (Intel Xeon 6-based) change this: PCI segments are enabled by default, with each CPU socket assigned its own segment (socket 1 → segment 0, socket 2 → segment 1, socket 3 → segment 2, socket 4 → segment 3). Since NVMe connectivity on these platforms is CPU-direct, this is exposed to Linux as a distinct PCI domain per socket (e.g. `0000:3b:00.0` and `0001:3b:00.0` are two different devices on different sockets, despite sharing an identical bus:device.function). This is a platform/firmware-level change in PCIe topology, not merely a difference in how Linux numbers buses, and it means software can no longer assume a PCI domain of zero — the full `domain:bus:device.function` address must be treated as the device identifier. The only prior use of a "don't care about domain" filter was VMD (Volume Management Device): VMD-backed NVMe devices are enumerated under their own domain, unrelated to the owning NUMA node, so the filter carried an `ignore_domain` flag to match any domain for the VMD case. This design removes that `ignore_domain` special case entirely (Section 4.4): with domain now mandatory everywhere else, VMD's distinct behavior is simplified to configuring no filter entries at all, rather than special-casing the domain match.

### 2.2 Field incident: DAOS-19772 — hot-plugged drive claimed by the wrong engine

A production dual-socket server had two engines, each pinned to a different NUMA node/CPU socket, enumerated under *different* non-zero PCI domains (`0000` and `0001` respectively) — expected on this multi-segment-PCIe platform, where each CPU socket is assigned its own PCI segment/domain. Because the filter tracked only bus-ID, a replacement drive at `0001:d6:00.0` (engine 1) was instead claimed by **engine 0**, because engine 0's domain-`0000` NUMA node happened to reuse bus-ID `d6` for one of its own devices. `dmg storage replace nvme` failed (`unexpected number of results, want 1 got 2`), and engine 1 logged repeated `PCI_BUS: Requested device 0001:d6:00.0 cannot be used` errors for ~20 minutes until engine 0 was stopped. Reproduced twice on two separate storage nodes; root cause confirmed via a minimal unplug/replug/rebind procedure. This design, to be delivered in `PR #19259` (DAOS-19772), makes the filter domain-aware and natively supports multiple non-zero domains per engine, as required by Gen-12 hardware.

## 3. Goals and Non-Goals

### Goals

1. Scope each engine's hotplug filter to the PCI domain(s) it has been assigned with PCI domain/segment and bus-ID, so a device is never claimed by the wrong engine due to a bus-ID collision across domains.
2. In every `bdev_hotplug_range` entry, **both domain and bus-ID are mandatory** — each independently expressible as a single value or an inclusive range.
3. Accept **hex (`0x`-prefixed) notation only** for each identifier (domain or bus-ID value) in config file entries — decimal notation is not supported.
4. Support **multiple entries** per engine, so one engine can filter on several domain+bus-ID combinations at once.
5. When no `bdev_hotplug_range`/`bdev_busid_range` is manually configured, **auto-derive the filter to match exactly the addresses in `bdev_list`** for the engine (generated under the hood from PCI topology), rather than the full NUMA/host-bridge topology (see Section 5 for the resulting limitation). An administrator may manually configure `bdev_hotplug_range` to cover a broader range than `bdev_list` (e.g. to admit devices not yet present at format time); this is permitted but not advertised and as such is at the administrator's own risk (Section 5).
6. Preserve existing VMD behavior while eliminating the `ignore_domain`/"any domain" special case entirely: a VMD-enabled engine configures **zero** `bdev_hotplug_range` entries (the SPDK `hotplug_busid_range` method is not invoked at all), which is equivalent to applying no PCI-address restriction — the same default-allow behavior already used whenever no filter is configured.
7. Remain backward compatible with the existing, deprecated `bdev_busid_range` parameter: it continues to be accepted and is converted internally to a single `bdev_hotplug_range` entry **on PCI domain `0x00` specifically** (Section 4.2) — there is no "any domain" fallback for the legacy parameter.

### Non-Goals

- Any change to how PCI addresses are allocated, enumerated, or reported by hardware/firmware — only how DAOS *filters* hotplug events based on addresses it is given.
- Supporting post-format updates to `bdev_list` — see Section 5.

## 4. Design

### 4.1 Filter resolution order

Three sources are evaluated in priority order by `setHotplugRange()`:

1. If `VMDEnabled`, **no `bdev_hotplug_range` entries are configured at all** — the SPDK `hotplug_busid_range` method is simply not invoked, so the filter defaults to allow-all for this engine (VMD backing devices may use a synthetic domain unrelated to their NUMA node, so no PCI-domain/bus-ID restriction is meaningful here).
2. Otherwise, an explicit `bdev_hotplug_range` (or legacy `bdev_busid_range`, converted per Section 4.2) is used if present.
3. If neither applies, the filter is derived automatically from the engine's `bdev_list` (Section 4.3).

### 4.2 Config syntax

`bdev_hotplug_range` holds a list of entries, grammar:

```
entry       := domain-part ":" bus-part
domain-part := identifier | identifier "-" identifier
bus-part    := identifier | identifier "-" identifier
identifier  := "0x" HEXDIGIT+
```

Both `domain-part` and `bus-part` are **mandatory** in every entry — there is no bare `bus-part`-only form and no way to omit the domain. Ranges are inclusive at both ends. Decimal notation is not accepted; every identifier must use the `0x`-prefixed hex form.

| Entry                 | Domain                  | Bus-ID              | Meaning                                  |
|-----------------------|--------------------------|----------------------|-------------------------------------------|
| `0x00:0x80`           | `0x00`                   | `0x80`               | Single domain, single bus-ID              |
| `0x01:0x00-0xFF`      | `0x01`                   | `0x00`-`0xFF`        | Single domain, full bus-ID range           |
| `0x00-0x02:0x00-0x80` | `0x00`-`0x02`            | `0x00`-`0x80`        | Domain range, bus-ID range                 |

**Legacy parameter and migration:** `bdev_busid_range` (the original `<begin>-<end>` bus-ID-only syntax; no domain concept) is retained in the config schema as a deprecated, legacy alias. It is always interpreted as applying to PCI domain `0x00` specifically — there is no "any domain" fallback, and any domain prefix previously accepted by this parameter is no longer recognized; multi-domain configuration is now exclusively the responsibility of `bdev_hotplug_range`. Config load behavior:

- If only `bdev_busid_range` is set, it is converted to a single-entry `bdev_hotplug_range` with the parsed `begin`-`end` bus-ID range on domain `0x00`.
- If only `bdev_hotplug_range` is set, it is used as-is.
- If **both** are set for the same storage tier, config validation fails with an explicit error naming the tier — administrators must migrate fully to `bdev_hotplug_range` rather than mixing the two parameters.

### 4.3 Auto-derivation

When neither `VMDEnabled` nor an explicit `bdev_hotplug_range`/`bdev_busid_range` is configured, the filter is auto-derived directly from the engine's configured `bdev_list`, using PCI topology information to resolve each `bdev_list` address's domain: emit multiple `bdev_hotplug_range` entries per domain covering contiguous and non-contiguous ranges of `bdev_list` addresses. This replaces NUMA-node/host-bridge-topology-based derivation (which could be broader than what the engine owns, or fail outright on a NUMA node whose host bridges span multiple domains) with a derivation that is always precise and always succeeds for any valid `bdev_list`, including one spanning several non-zero domains. An administrator may instead manually configure `bdev_hotplug_range` (or legacy `bdev_busid_range`) to cover a range broader than `bdev_list`; manual configuration always takes priority over auto-derivation and is not validated against `bdev_list` (Section 5).

### 4.4 Wire format and SPDK/C filter

`BdevWriteConfigRequest`/`HotplugFilterParams` (SPDK JSON config) carry a slice of entries, each with just a domain range and a bus-ID range (`Domain`/`Begin`/`End` — no `IgnoreDomain` field, which is removed entirely), rather than a single object with a flag; a single `hotplug_busid_range` SPDK config method call takes an array parameter (zero or more entries), keeping one point of truth for the filter config. On the engine, `struct hotplug_filter_info` (`bio_config.c`) becomes an array + count of `{domain, begin, end}` entries (`ignore_domain` removed), decoded via `spdk_json_decode_array` (an existing pattern elsewhere in this file). `hotplug_filter_fn()` loops over all entries, accepting a device if **any** entry matches:

```c
domain_lo <= addr->domain <= domain_hi && bus_lo <= addr->bus <= bus_hi
```

(a single-value domain or bus-ID is the degenerate case `lo == hi`). If **zero** entries are configured, the filter defaults to allow-all — this is now how VMD is handled (no entries written to the SPDK config at all) rather than a dedicated `ignore_domain` wildcard case, since VMD backing addresses are unrelated to the owning NUMA node's domain(s) by construction and don't need PCI-based filtering.

## 5. Known Limitation: `bdev_list` is static after format

Auto-derivation restricts the filter to `bdev_list` addresses (Goal 6), which ties filter correctness to the configured device list — currently static after an engine is formatted:

- A replacement device at the **same** PCI address as an existing `bdev_list` entry works exactly as today.
- Under auto-derivation, a device plugged at an address **not** in the original `bdev_list` (e.g. a spare slot populated after format) will **not** match the filter and will not be processed by the control plane, even though it might fall within a topology-derived range under the prior, topology-based auto-derivation.

This is an accepted consequence of Goal 6 for the auto-derived case, not a regression: it deliberately narrows the default filter from "anything on this NUMA node's PCI topology" to "only what was configured." An administrator who needs hotplug to admit addresses beyond the current `bdev_list` (e.g. anticipating spare slots to be populated later) may manually configure `bdev_hotplug_range` to cover a broader range than `bdev_list` — this is explicitly supported (Section 4.3) but is **at the administrator's own risk**: the control plane does not validate a manually-configured range against `bdev_list`, so a wider manual range can admit hotplug events for devices the engine was never formatted with, which the control plane may not handle correctly. This trade-off must be called out prominently in `daos_server.yml`/`docs/admin/administration.md`. A future, separate enhancement to allow post-format `bdev_list` updates would also need to refresh auto-derived filter ranges.

## 6. Compatibility and Interoperability

No existing field is removed; `bdev_busid_range` remains valid, always interpreted as domain `0x00` (Section 4.2), and is converted internally to a single `bdev_hotplug_range` entry, so existing single-domain configs require no changes. Specifying both `bdev_busid_range` and `bdev_hotplug_range` on the same tier is rejected at config validation time, rather than silently preferring one. VMD's external behavior is unchanged (no hotplug events are missed), even though it is now implemented as zero configured entries rather than an `ignore_domain` wildcard.

On the `daos_nvme.conf` (engine JSON config) side, **no backward-compatible matching of older on-disk shapes is implemented**. The prior design's `ignore_domain`/absent-key-defaults-to-a-guessed-domain behavior is exactly the class of lenient, ambiguous matching that caused DAOS-19772 (Section 2.2) — an engine silently accepting a hotplug event it should not have claimed. Reintroducing that kind of guesswork purely to tolerate an old-shape config file would reintroduce the same risk. Instead:

- The control plane regenerates `daos_nvme.conf` with the current `hotplug_busid_range` array shape on every engine start via `WriteNvmeConfig`/`UpgradeBdevConfig`, so in normal operation the engine never encounters an older shape.
- `ControlMetadata.AllowSpdkConfOverride` is not supported in combination with a `daos_nvme.conf` predating this design — administrators using the override must regenerate the file (disable the override once, let the control plane rewrite it, then re-enable if still required) before upgrading to an engine binary that expects the array shape. This is a deliberate restriction, not an oversight.

VMD's external behavior is unchanged throughout.

## 7. External Interfaces

- `daos_server.yml` gains a new `bdev_hotplug_range` parameter (mandatory-domain, multi-entry list syntax); the existing `bdev_busid_range` parameter is kept as a deprecated legacy alias (Section 4.2). No `dmg`/API surface changes — this is purely an engine-config and SPDK-filter-internal change.

## 8. Testing and Validation

Unit test coverage, across `bdev_test.go`, `config_test.go`, `provider_test.go`, and `bdev/backend_json_test.go` (plus C-side hotplug filter tests), needs to include: `bdev_list`-based multi-domain/multi-entry derivation; legacy `bdev_busid_range` conversion to a single `bdev_hotplug_range` entry on domain `0x00`; the both-parameters-set validation error; overlapping-range validation; hex-only notation parsing (including rejection of decimal input); single-value vs. range parsing for both domain and bus-ID; multi-entry JSON config round-tripping; and VMD configuring zero `bdev_hotplug_range` entries (not an `ignore_domain` wildcard entry). Manual validation should be performed on Gen-12 HPE hardware with a single engine's `bdev_list` spanning 2+ non-zero PCI domains, confirming per-domain precision and that an address absent from `bdev_list` is correctly ignored (Section 5), plus a regression pass against the existing VMD hotplug test suite.

## 9. Risks, Mitigations and Future Work

- The `bdev_list`-static-after-format limitation (Section 5) is accepted but should be revisited if/when post-format `bdev_list` updates become possible — at which point the derived filter ranges would also need to be refreshed.
- Deliberately not supporting `AllowSpdkConfOverride` with a `daos_nvme.conf` predating this design (Section 6) means an administrator who upgrades without regenerating the file first will see `bio_set_hotplug_filter()` fail to decode `hotplug_busid_range`, rather than a silent mismatch — a fail-closed/visible failure was chosen over any lenient legacy-shape matching, since lenient matching (e.g. `ignore_domain`, or guessing a domain for an absent key) is the exact mechanism that caused DAOS-19772.
