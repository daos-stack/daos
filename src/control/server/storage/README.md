# storage package

This package implements the control plane's representation of, and configuration for, SCM
and NVMe storage tiers, including the config-parsing and SPDK JSON-config-generation logic
shared by `server/storage/scm` and `server/storage/bdev`.

## Hotplug PCI-domain/bus-ID filtering

See [`design/control/hotplug-pci-domain-design.md`](/design/control/hotplug-pci-domain-design.md)
for the full design background. This section documents only the configuration syntax, which is
intentionally **not** described in user-facing admin documentation; the filter is derived
automatically from each engine's `bdev_list` and manual configuration is an experimental,
unsupported edge case.

`bdev_hotplug_range` (field `BdevConfig.HotplugRanges`, type `BdevHotplugRanges`) is a list of
one or more `"<domain>:<bus-id>"` entries restricting which NVMe hotplug events an engine reacts
to. Both the domain and the bus-ID are mandatory in every entry. Domain is always a single
identifier (domain ranges are not supported — one entry covers exactly one PCI domain); bus-ID
may be a single identifier or an inclusive `<lo>-<hi>` range of identifiers. All identifiers are
plain hex (no `0x` prefix), matching ordinary PCI address formatting (e.g. `0000:3b:00.0`);
decimal notation is not accepted (see `parseHotplugIdent()` in `config.go`). A hotplug event is
accepted if it matches any configured entry, for example:

```yaml
bdev_hotplug_range: ["00:80", "01:00-ff"]
```

The filter is engine-wide, not per-tier, so an engine with multiple bdev tiers (e.g. separate
`wal` and `meta`/`data` tiers) must set `bdev_hotplug_range`/`bdev_busid_range` on **at most one**
of its bdev tiers; setting either parameter on more than one bdev tier of the same engine is
rejected at config validation time (`TierConfigs.Validate()`).

If this parameter is unset (the normal case), the filter is derived automatically from
`bdev_list` by resolving each listed PCI address's domain and grouping addresses into
per-domain bus-ID ranges. If VMD is enabled, no filter entries are generated or accepted at all,
since VMD backing device addresses are unrelated to the owning NUMA node's PCI domain.

The deprecated legacy `bdev_busid_range` parameter (a bare `<begin>-<end>` bus-ID range, with no
domain concept) is still accepted for backward compatibility, and is always interpreted as
applying to PCI domain `0x00` specifically; unlike `bdev_hotplug_range`, it accepts both hex and
decimal notation (see `parseBusRange()` in `config.go`). `bdev_busid_range` and
`bdev_hotplug_range` are mutually exclusive on the same storage tier.
