# High-Level Design: Non-Zero PCI Domains in the Hotplug Bus-ID Filter

## 1. Summary

Each DAOS engine's `bdev_busid_range` mechanism restricts SPDK NVMe hotplug add/remove event handling to the PCI bus-ID range local to that engine's NUMA node/socket, so that a hotplug event on a device belonging to a different engine's NUMA node is ignored rather than mis-claimed. Prior to this change, the mechanism (config parsing, auto-derivation from hardware topology, the wire format carried to the engine, and the SPDK-side `hotplug_filter_fn`) tracked only the PCI bus-ID component of a device's address and implicitly assumed every engine's devices live in PCI domain (segment) `0000`. On multi-root-complex platforms where each CPU socket/NUMA node is enumerated under its own, non-zero PCI domain, this assumption is false, and the filter can either reject every hotplug event for an engine pinned to a non-zero-domain NUMA node, or — more seriously — accept a hotplug event for a device that belongs to a different engine, if the two NUMA nodes happen to reuse overlapping bus-ID numbers in their respective domains.

This design makes PCI domain a first-class part of the hotplug bus-ID range at every layer (YAML config, auto-derivation, the control-to-engine wire format, and the SPDK-side filter), fixing both failure modes while remaining fully backward compatible with existing single-domain (`0000`) deployments. It is implemented in `PR #19259` (DAOS-19772) on branch `tanabarr/control-hotplug-pcidomain`. The same PR additionally lets an administrator opt a single engine into matching hotplug events on bus-ID alone, across every PCI domain, via `bdev_busid_range: 0x00-0xff` (no domain prefix) — a coarser-grained escape hatch for the case where a NUMA node's PCI buses genuinely span more than one domain (Section 4.1).

## 2. Background

### 2.1 The single-domain assumption

DAOS's existing hotplug bus-ID filtering design (`bdev_busid_range` server config parameter, auto-derivation from hardware topology, and the SPDK-side `hotplug_filter_fn`) was built around a topology model where a single PCI domain (`0000`) spans an entire server, and individual NUMA nodes/sockets are distinguished purely by disjoint, contiguous ranges of PCI bus-IDs within that one domain. This matches the common enumeration behavior of many single-root-complex platforms, where the chipset/root-complex exposes one logical PCI domain and buses are allocated contiguously as you walk the topology outward from the root. The only prior use of a non-zero or "don't care" domain in this code path was VMD (Volume Management Device): a VMD endpoint's backing NVMe devices are enumerated under their own domain, unrelated to the NUMA node's own domain, so the filter already carried an `ignore_domain`-equivalent "allow all buses" escape hatch for that specific case (`begin = 0x00`, `end = 0xFF`) — but this was a special case of allowing everything, not a mechanism for correctly scoping a *non-zero* domain to a specific NUMA node.

### 2.2 DAOS-19772: hot-plugged drive claimed by the wrong engine

A production dual-socket server was configured with two engines, each pinned to a different NUMA node, with `storage.bdev_list` entries like:

```yaml
engines:
  - pinned_numa_node: 0
    storage:
      - bdev_list:
          - 0000:84:00.0
          - 0000:85:00.0
  - pinned_numa_node: 1
    storage:
      - bdev_list:
          - "0001:34:00.0"
          - "0001:35:00.0"
```

NUMA node 1's NVMe devices are enumerated under PCI domain `0001`, not `0000` — expected behavior on this multi-root-complex platform (each CPU socket/root-complex gets its own PCI domain), unrelated to VMD, and corroborated by the engine's `fabric_iface: enP1s3np0` naming (`P1` = PCI domain 1). Because `bdev_busid_range` and its auto-derivation tracked only bus-ID, engine 1's hotplug filter was effectively programmed with a bare bus range (e.g. `0x34-0x35`) and no record of domain `0001`.

In the field incident, a faulty drive belonging to engine 1 (bus-ID `0001:d6:00.0`) was unplugged, replaced, and rebound via `dmg storage nvme-rebind`. The new drive's `device_plugged` hotplug event was claimed by **engine 0** instead of engine 1, because engine 0's domain-`0000` NUMA node happened to reuse the same bus-ID number `d6` for one of its own devices, and the filter on both engines matched on bus-ID alone. `dmg storage replace nvme` subsequently failed (`unexpected number of results, want 1 got 2`), and engine 1 logged repeated `EAL: Cannot open /dev/vfio/83: Device or resource busy` / `PCI_BUS: Requested device 0001:d6:00.0 cannot be used` errors for roughly 20 minutes, since the device node was held open by engine 0's (incorrect) claim. Engine 1 only picked up the drive, 15 seconds later, once engine 0 was stopped — this is a race between the two engines' hotplug pollers, neither of which was checking whether a plugged device's domain actually belonged to its own NUMA node. The sequence was reproduced twice on two separate storage nodes.

A minimal reproduction procedure (stop one engine, simulate unplug/replug/rebind of a device whose bus-ID is shared across domains, then check `dmg storage query list-devices`) reliably showed the device attributed to the wrong engine prior to this fix.

## 3. Goals and Non-Goals

### Goals

1. Correctly scope each engine's hotplug bus-ID filter to its own PCI domain, not just its bus-ID range, so that a device belonging to one engine's NUMA node is never claimed by another engine purely because of a bus-ID collision across domains.
2. Support both automatic derivation of the domain (from hardware topology) and explicit, manual specification via `bdev_busid_range`.
3. Preserve existing VMD behavior exactly (VMD-backed addresses are domain-independent by construction, so domain must still be ignorable for that case).
4. Remain fully backward compatible with existing single-domain (`0000`) `bdev_busid_range` configs and the generated SPDK JSON config — no required config changes for unaffected deployments.
5. Allow a single engine to opt into matching hotplug events on bus-ID alone, across every PCI domain, for the case where a NUMA node's PCI buses genuinely span more than one domain (see Section 4.1).

### Non-Goals

- Supporting a single engine's hotplug filter that is scoped to a *specific list* of more than one, non-zero PCI domain (e.g. "match domain `0000` bus `0x80-0x8f` and domain `0001` bus `0x34-0x35`, but nothing else"). What *is* supported (Section 4.1) is the coarser "match this bus-ID range in any domain" case; a precise per-domain range list remains out of scope and is explored separately in `hotplug-multi-pci-domain-design.md` (Option 3).
- Any change to how PCI addresses are allocated, enumerated, or reported by hardware/firmware — this design only changes how DAOS *filters* hotplug events based on the addresses it is given.

## 4. Design

### 4.1 Config Parsing (`config.go`)

`parsePCIBusRange()` previously accepted only a `<Begin>-<End>` bus-ID range. It now accepts an optional `[<Domain>:]<Begin>-<End>` form: if a `:` separator is present, everything before it is parsed as a hexadecimal (no `0x` prefix, standard PCI BDF notation, e.g. `0001`) domain value; everything after it is parsed as the bus range exactly as before. The domain defaults to `0x0000` when the prefix is omitted, preserving every existing config's meaning unchanged. `parsePCIBusRange()` also now returns whether a domain prefix was actually present in the input, distinct from "omitted and defaulted to 0". `BdevBusRange.UnmarshalYAML()` stores the parsed domain into both `LowAddress.Domain` and `HighAddress.Domain` (a single range only ever spans one domain), and `BdevBusRange.String()` now renders the domain prefix (`%04x:0x%02x-0x%02x`) whenever either address has a non-zero domain, so round-tripping a manually-specified non-zero-domain range preserves it.

`BdevBusRange` additionally gains an `IgnoreDomain bool` field: when the domain prefix is **omitted** and the bus range is the **full** `0x00-0xFF` span, `UnmarshalYAML()` sets `IgnoreDomain = true` instead of defaulting to domain `0x0000`. This reuses the "ignore domain, match bus-ID only" mechanism already in place for VMD (Section 4.3/4.4) to let an administrator write `bdev_busid_range: 0x00-0xff` to mean "match this bus-ID range in any domain", for a NUMA node whose PCI buses span more than one domain — without inventing a new reserved sentinel value, since this specific combination (no domain, full range) was not otherwise a useful thing to configure explicitly. An explicit `0000:0x00-0xff` continues to mean exactly domain `0000` only, since the domain-prefix-presence distinction is preserved. This was evaluated as "Option 2b" in `hotplug-multi-pci-domain-design.md` and implemented directly in this PR rather than deferred, since it required no changes below the Go config-parsing/request layer.

### 4.2 Auto-Derivation (`bdev.go`)

`getNumaNodeBusidRange()` previously derived a bus-ID range by reading only the `Bus` component of the first and last `PCIBuses` entry for a NUMA node, under the assumption that host bridges are enumerated in sorted order. It now returns a domain value alongside begin/end, and — since that sort-order assumption cannot be relied on for domain detection — iterates every bus range's `LowAddress`/`HighAddress`, confirming every one shares the same `Domain` while tracking the overall min/max bus-ID with the `min`/`max` builtins. If any range's domain disagrees with the first, the function fails with an explicit error naming the two conflicting domains and directing the administrator to set `bdev_busid_range` explicitly, rather than silently picking one domain or merging incompatible ranges. Automatic derivation itself does not attempt the "any domain" case (Section 4.1) on the administrator's behalf; that still requires an explicit `bdev_busid_range: 0x00-0xff` override.

### 4.3 Request and Wire Format (`provider.go`, `bdev/backend_json.go`)

`BdevWriteConfigRequest` gains `HotplugBusidDomain uint16` and `HotplugIgnoreDomain bool` fields alongside the existing `HotplugBusidBegin`/`HotplugBusidEnd uint8` fields. `setHotplugRange()` populates these from whichever source applies (VMD: domain `0x0000` with the existing "allow all buses" `0x00`-`0xFF` range; explicit `tier.Bdev.BusidRange`, propagating its `IgnoreDomain` flag; or auto-derivation), and logs either `NUMA %d: hotplug bus-ids %X-%X (any PCI domain)` or `NUMA %d: hotplug bus-ids %04x:%X-%X` for diagnostics, depending on which applies. `HotplugBusidRangeParams` (the JSON struct written into the SPDK config consumed by the engine) gains matching `Domain uint16` and `IgnoreDomain bool` fields; `IgnoreDomain` is set from `req.VMDEnabled || req.HotplugIgnoreDomain`, so both VMD and an explicit "any domain" `bdev_busid_range` configuration drive the same underlying flag, keeping the "match bus-ID only" behavior as a single, explicit, named mechanism rather than an implicit side effect of a wide bus range.

### 4.4 SPDK/C Filter (`bio_config.c`)

`struct busid_range_info` gains `domain` (`uint32_t`) and `ignore_domain` (`bool`) fields, decoded from the JSON config via two new optional `spdk_json_decode_*` entries (defaulting to zero/false when absent, so an older-format JSON config — e.g. generated before this change — still decodes correctly). `hotplug_filter_fn()` now additionally requires `addr->domain == hotplug_busid_range.domain` to match, unless `ignore_domain` is set, in which case the domain check is skipped exactly as before — this one code path serves both the VMD case and the new "any domain" `bdev_busid_range` case from Section 4.1/4.3, with no C-side changes needed to support the latter. The `D_INFO`/`D_DEBUG` log lines in `bio_set_hotplug_filter()` were updated to print the effective domain (or `domain=any` when `ignore_domain` is set) alongside the bus-ID range, so the filter actually in effect is unambiguous in engine logs — this was itself the subject of a PR review comment, since the earlier revision printed a literal domain value even when it was being ignored.

### 4.5 Documentation (`utils/config/daos_server.yml`)

Both `bdev_busid_range` example blocks were updated to document the new `[domain:]` prefix syntax, a new domain-prefixed example line (`bdev_busid_range: 0001:0x80-0x8f`), a new "any domain" example line (`bdev_busid_range: 0x00-0xff`), and the fact that a single domain-prefixed range still covers only one domain — if auto-derivation fails because a NUMA node's PCI buses span more than one domain, the administrator must either restrict `bdev_busid_range` to one domain (accepting that hotplug events in the other domain are then ignored), set it to the full `0x00-0xff` range with no domain prefix to match any domain, or set `disable_hotplug: true` for that engine.

## 5. Compatibility and Interoperability

No existing config, request, or JSON schema field is removed or repurposed; all new fields (`HotplugBusidDomain`, `HotplugIgnoreDomain`, `Domain`, `ignore_domain`) are strictly additive and default to zero/false, which reproduces the exact pre-existing behavior (domain `0000`, bus-ID-only filtering unaffected). An existing `bdev_busid_range: 0x80-0x8f` config (no domain prefix, partial range) continues to parse identically to before — only the specific combination of "no domain prefix" **and** "full `0x00-0xFF` range" changes meaning, and that combination was not previously a useful thing to configure explicitly (it was silently equivalent to "domain `0000` only, every bus", a narrower and less useful case than either "every domain" or "domain `0000` explicitly"). VMD mode's wire/filter behavior (`ignore_domain = true`, full bus range) is unchanged from an external standpoint; only its internal representation changed from an implicit wide-range side effect to an explicit named flag, now shared with the new "any domain" `bdev_busid_range` case.

## 6. External Interfaces

- `bdev_busid_range` in `daos_server.yml` gains the optional `[domain:]` prefix, and a new meaning for the no-prefix, full-range (`0x00-0xff`) combination specifically ("match any domain"); existing values without a domain prefix and with a non-full range are unaffected.
- No `dmg`/API surface changes — this is purely an engine-config and SPDK-filter-internal change; the only observable effect to an administrator is that hotplug events are now correctly scoped per engine on multi-domain platforms, where previously they could be mis-attributed or spuriously rejected, and that an engine whose NUMA node spans multiple domains can now opt into "any domain" matching instead of only a hard failure.

## 7. Testing and Validation

### 7.1 Unit Tests

New/expanded coverage across `bdev_test.go` (`Test_getNumaNodeBusidRange`, including a multi-domain-mid-list case matching the field incident), `config_test.go` (`TestStorage_parsePCIBusRange` for domain-prefixed range parsing and the domain-prefix-presence flag; `TestStorage_BdevBusRange_IgnoreDomain` for the no-prefix-plus-full-range "any domain" case and its string round-tripping), `provider_test.go` (`Test_BdevWriteRequestFromConfig`, including a "range specified; all domains" case), and `bdev/backend_json_test.go` (request-to-JSON-params population, including both the VMD and the explicit "all domains" `IgnoreDomain` cases).

### 7.2 Manual Validation

Validated on an AMD Gen12 ("trout") system using the minimal reproduction procedure from DAOS-19772 (stop one engine, unplug/replug/rebind a device whose bus-ID is shared across domains, confirm `dmg storage query list-devices` attributes it to the correct engine): the domain-aware filter, derived automatically from topology, correctly recognized the device only on its own engine. A regression pass against the existing VMD hotplug test suite was separately requested (no new VMD-specific tests were deemed necessary, since VMD's `ignore_domain` path is unchanged in effect, only in internal representation), to confirm no change in behavior for VMD-enabled configurations where PCI domain for backing devices is unrelated to the owning NUMA node's domain.

## 8. Risks, Mitigations and Future Work

### 8.1 Risks and Mitigations

- The fix still requires a NUMA node's PCI buses to belong to a single domain for automatic derivation to succeed; a NUMA node genuinely spanning multiple non-zero domains will fail engine startup with an explicit error rather than silently misbehaving (Section 4.2), unless the administrator explicitly opts into "any domain" matching via `bdev_busid_range: 0x00-0xff` (Section 4.1).
- `ignore_domain` (used for VMD, and now also for the explicit "any domain" `bdev_busid_range` case) is coarser-grained than true per-domain filtering: an administrator who opts into it to work around a multi-domain NUMA node reintroduces the same bus-ID-collision-across-domains risk this design otherwise fixes for the single-domain case, if the engine's devices do not actually have unique bus-IDs across every domain in play. This is called out in the `daos_server.yml` documentation (Section 4.5) but is not otherwise guarded against in code, since there is no reliable way to detect the collision risk from software alone.

### 8.2 Future Work

Supporting a single engine whose devices span more than one non-zero PCI domain with **precise, per-domain** filtering (rather than the coarser "any domain" bus-ID-only matching added in Section 4.1) remains out of scope here and is explored as a follow-on in `hotplug-multi-pci-domain-design.md` (Option 3): a full per-domain range-list rewrite, to be pursued only if the "any domain" option's bus-ID-collision risk is determined to be a real concern on target hardware.
