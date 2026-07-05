# Add allow-numa-imbalance flag to config generate

## Summary

This PR adds a new `--allow-numa-imbalance` / `-n` flag to the `dmg config generate` and `daos_server config generate` commands. This flag enables generation of server configurations where NVMe devices are distributed equally across engines regardless of NUMA affinity.

## Problem Statement

Currently, the config generation commands enforce balanced NVMe device distribution that respects NUMA affinity. When generating a config:

1. The tool calculates the lowest common number of SSDs across all NUMA nodes
2. It restricts each engine to use only that minimum number of SSDs from its local NUMA node
3. Extra SSDs on NUMA nodes with more devices are excluded from the config

This prevents maximizing device utilization in heterogeneous hardware environments where:
- Different NUMA nodes have different numbers of NVMe devices
- Hardware constraints result in uneven device distribution
- Users want to utilize all available SSDs even if it requires cross-NUMA access

## Solution

Add a `--allow-numa-imbalance` flag that:

1. **Collects all SSDs**: Gathers all available NVMe devices from all NUMA nodes
2. **Distributes equally**: Allocates SSDs evenly across all engines, ignoring NUMA boundaries
3. **Handles remainders**: Uses maximum divisible number of SSDs; remainder SSDs are not used
4. **Warns about performance**: Flag description includes warning about suboptimal performance
5. **Sets server config parameter**: The generated config includes `allow_numa_imbalance: true` to allow the server to start with this configuration
6. **Maintains backward compatibility**: The default behavior (without the flag) remains unchanged

### Example Behavior

**Before (without flag) - NUMA-aware balancing:**
- NUMA-0: 4 SSDs available → 2 SSDs used (limited to minimum)
- NUMA-1: 2 SSDs available → 2 SSDs used
- Total: 4 of 6 SSDs used

**After (with flag) - Equal distribution:**
- NUMA-0: 4 SSDs available → 3 SSDs assigned (may include devices from NUMA-1)
- NUMA-1: 2 SSDs available → 3 SSDs assigned (may include devices from NUMA-0)
- Total: All 6 SSDs used, distributed equally

**With remainder (7 SSDs / 2 engines):**
- All 7 SSDs collected, but only 6 used (3 per engine)
- 1 SSD remains unused (not evenly divisible)
- Warning logged: "using 6 SSDs (3 per engine), 1 SSDs will not be used"

## Changes Made

### 1. Command-line Interface (`src/control/common/cmdutil/auto.go`)
- Added `AllowNumaImbalance bool` field to `ConfGenCmd` struct
- Added short flag `-n` and long flag `--allow-numa-imbalance`
- Flag description: "Redistribute NVMe devices equally across engines regardless of NUMA affinity"

### 2. Control Library (`src/control/lib/control/auto.go`)
- Added `AllowNumaImbalance bool` field to `ConfGenerateReq` struct
- Modified `correctSSDCounts()` function to accept `allowImbalance` parameter
  - When `false`, performs NUMA-aware balancing (existing behavior)
  - When `true`, calls new `distributeSSDs()` function
- Added new `distributeSSDs()` function:
  - Collects all SSDs from all NUMA nodes
  - Validates that VMD devices are not present (cannot distribute VMD)
  - Calculates SSDs per engine (total / nrEngines)
  - If remainder exists, uses only maximum divisible number
  - Logs notice when SSDs are unused due to remainder
  - Distributes SSDs equally across engines
  - Logs detailed information about distribution
- Updated `genEngineConfigs()` to pass the flag to `correctSSDCounts()`
- Modified `genServerConfig()` to call `WithAllowNumaImbalance()` on generated config

### 3. Tests (`src/control/lib/control/auto_test.go`)
- Updated `TestControl_AutoConfig_correctSSDCounts` to test the new parameter
- Added test case "allow imbalance distributes equally":
  - 4 SSDs on NUMA-0 + 2 on NUMA-1 = 6 total
  - Distributes to 3 SSDs per engine
  - Verifies equal distribution
- Added test case "allow imbalance with remainder discards extras":
  - 5 SSDs on NUMA-0 + 2 on NUMA-1 = 7 total
  - Uses 6 SSDs (3 per engine), 1 SSD unused
  - Verifies remainder handling
- Added test case "allow imbalance with 8 SSDs across 2 engines":
  - 5 SSDs on NUMA-0 + 3 on NUMA-1 = 8 total
  - Distributes to 4 SSDs per engine
  - Verifies equal distribution works
- Existing test cases verify default behavior still works correctly

### 4. Documentation (`docs/admin/deployment.md`)
- Added flag to both `daos_server config generate` and `dmg config generate` help output
- Added detailed description explaining:
  - Default NUMA-aware balancing behavior
  - Effect of the flag (equal distribution regardless of NUMA)
  - Handling of remainders (maximum divisible number used)
  - **Performance warning**: May result in suboptimal performance
  - That it sets `allow_numa_imbalance: true` in the generated config

## Use Cases

This feature is useful for:

1. **Development/testing environments** with heterogeneous hardware
2. **Hardware upgrades** where NVMe devices are added incrementally to specific nodes
3. **Hardware failures** where some devices are unavailable on certain NUMA nodes
4. **Specialized configurations** requiring maximum utilization despite imbalance
5. **Cost-optimized deployments** with mixed hardware specifications

## Example Usage

### Before (without flag)
```bash
$ daos_server config generate
# NUMA-0: 4 SSDs available, 2 used (limited to minimum)
# NUMA-1: 2 SSDs available, 2 used
# Total: 4 of 6 SSDs utilized
```

### After (with flag)
```bash
$ daos_server config generate --allow-numa-imbalance
# Collects all 6 SSDs and redistributes equally:
# - Engine 0: Gets 3 SSDs (regardless of which NUMA node they're on)
# - Engine 1: Gets 3 SSDs (regardless of which NUMA node they're on)
# - Config includes: allow_numa_imbalance: true
# Total: All 6 SSDs utilized
```

### DMG remote generation
```bash
$ dmg config generate -l host1,host2 --allow-numa-imbalance
# Generates config allowing imbalanced NVMe distribution across the hostset
```

## Testing

### Unit Tests
- Added test case for `correctSSDCounts()` with imbalance flag
- Verified existing tests still pass (default behavior unchanged)

### Manual Testing
Run the following to verify:
```bash
# Test without flag (default behavior)
daos_server config generate

# Test with flag (new behavior)
daos_server config generate --allow-numa-imbalance

# Verify generated config contains: allow_numa_imbalance: true
```

## Backward Compatibility

✅ **Fully backward compatible**
- Default behavior unchanged (flag defaults to `false`)
- Existing scripts and automation continue to work
- No breaking changes to API or config format

## Future Enhancements

Potential follow-up work:
1. Add validation to warn users about performance implications of imbalanced configs
2. Provide recommendations for device distribution optimization
3. Add metrics to monitor imbalance effects on production systems

## Checklist

- [x] Code changes implemented
- [x] Unit tests added/updated
- [x] Documentation updated
- [x] Commit message follows guidelines
- [x] Signed-off-by line included
- [ ] CI/CD tests pass
- [ ] Review by control plane experts

## Related Issues

- Addresses user requests for config generation in heterogeneous environments
- Related to DAOS-16979 (hugepage/NUMA imbalance handling)

---

**Testing Instructions for Reviewers:**

1. Verify flag appears in help output:
   ```bash
   daos_server config generate --help | grep numa
   dmg config generate --help | grep numa
   ```

2. Test with imbalanced hardware (if available) or mock setup

3. Verify generated config includes `allow_numa_imbalance: true` when flag is used

4. Confirm default behavior (without flag) still balances SSDs correctly
