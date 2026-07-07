//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

// PMemRegionType represents PMem region type.
type PMemRegionType uint32

// PMemRegionType values represent the ipmctl region_type enum. Type of region.
const (
	RegionTypeUnknown        PMemRegionType = iota
	RegionTypeAppDirect                     // App Direct mode.
	RegionTypeNotInterleaved                // Non-interleaved App Direct mode.
	RegionTypeVolatile                      // Volatile.
)

func (pmrt PMemRegionType) String() string {
	if val, exists := map[PMemRegionType]string{
		RegionTypeUnknown:        "Unknown",
		RegionTypeAppDirect:      "AppDirect",
		RegionTypeNotInterleaved: "AppDirectNotInterleaved",
		RegionTypeVolatile:       "Volatile",
	}[pmrt]; exists {
		return val
	}
	return "Unknown"
}

func PMemRegionTypeFromString(in string) PMemRegionType {
	if val, exists := map[string]PMemRegionType{
		"Unknown":                 RegionTypeUnknown,
		"AppDirect":               RegionTypeAppDirect,
		"AppDirectNotInterleaved": RegionTypeNotInterleaved,
		"Volatile":                RegionTypeVolatile,
	}[in]; exists {
		return val
	}
	return RegionTypeUnknown
}

// PMemRegionHealth represents PMem region health.
type PMemRegionHealth uint32

// PMemRegionHealth values represent the ipmctl region_health enum. Rolled-up health of the underlying
// PMem modules from which the REGION is created. Constant values start at 1.
const (
	_                   PMemRegionHealth = iota
	RegionHealthNormal                   // All underlying PMem module capacity is available.
	RegionHealthError                    // Issue with some or all of the underlying PMem module capacity.
	RegionHealthUnknown                  // The region health cannot be determined.
	RegionHealthPending                  // A new memory allocation goal has been created but not applied.
	RegionHealthLocked                   // One or more of the underlying PMem modules are locked.
)

func (pmrh PMemRegionHealth) String() string {
	if val, exists := map[PMemRegionHealth]string{
		RegionHealthNormal:  "Healthy",
		RegionHealthError:   "Error",
		RegionHealthUnknown: "Unknown",
		RegionHealthPending: "Pending",
		RegionHealthLocked:  "Locked",
	}[pmrh]; exists {
		return val
	}
	return "Unknown"
}

func PMemRegionHealthFromString(in string) PMemRegionHealth {
	if val, exists := map[string]PMemRegionHealth{
		"Healthy": RegionHealthNormal,
		"Error":   RegionHealthError,
		"Unknown": RegionHealthUnknown,
		"Pending": RegionHealthPending,
		"Locked":  RegionHealthLocked,
	}[in]; exists {
		return val
	}
	return RegionHealthUnknown
}
