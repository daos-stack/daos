//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"fmt"
	"strconv"
	"strings"
)

func delta(a, b int) int {
	if a > b {
		return a - b
	}

	return b - a
}

// Version represents a semantic version.
type Version struct {
	Major int
	Minor int
	Patch int
}

// MustNewVersion creates a new version from a string,
// panics if the version cannot be created.
func MustNewVersion(in string) Version {
	v, err := NewVersion(in)
	if err != nil {
		panic(err)
	}

	return *v
}

// NewVersion creates a new version from a string.
func NewVersion(in string) (*Version, error) {
	var (
		major, minor, patch int
		err                 error
	)

	in = strings.TrimPrefix(in, "v")

	parts := strings.Split(in, ".")
	if len(parts) != 3 {
		return nil, fmt.Errorf("invalid version format %q", in)
	}

	major, err = strconv.Atoi(parts[0])
	if err != nil {
		return nil, fmt.Errorf("invalid major version %q: %v", parts[0], err)
	}

	minor, err = strconv.Atoi(parts[1])
	if err != nil {
		return nil, fmt.Errorf("invalid minor version %q: %v", parts[1], err)
	}

	patch, err = strconv.Atoi(parts[2])
	if err != nil {
		return nil, fmt.Errorf("invalid patch version %q: %v", parts[2], err)
	}

	return &Version{
		Major: major,
		Minor: minor,
		Patch: patch,
	}, nil
}

func (v Version) String() string {
	return fmt.Sprintf("%d.%d.%d", v.Major, v.Minor, v.Patch)
}

// IsZero returns true if the version is zero.
func (v Version) IsZero() bool {
	return v.Major == 0 && v.Minor == 0 && v.Patch == 0
}

// Equals tests if two versions are equal.
func (v Version) Equals(other Version) bool {
	return v.Major == other.Major && v.Minor == other.Minor && v.Patch == other.Patch
}

// GreaterThan tests if the version is greater than the other.
func (v Version) GreaterThan(other Version) bool {
	if v.Major > other.Major {
		return true
	}

	if v.Minor > other.Minor {
		return true
	}

	return v.Patch > other.Patch
}

// LessThan tests if the version is less than the other.
func (v Version) LessThan(other Version) bool {
	if v.Major < other.Major {
		return true
	}

	if v.Minor < other.Minor {
		return true
	}

	return v.Patch < other.Patch
}

// PatchCompatible tests if the major and minor versions
// are the same.
func (v Version) PatchCompatible(other Version) bool {
	return v.MajorDelta(other) == 0 && v.MinorDelta(other) == 0
}

// MajorDelta returns the difference between the major versions.
func (v Version) MajorDelta(other Version) int {
	return delta(v.Major, other.Major)
}

// MinorDelta returns the difference between the minor versions.
func (v Version) MinorDelta(other Version) int {
	return delta(v.Minor, other.Minor)
}

// PatchDelta returns the difference between the patch versions.
func (v Version) PatchDelta(other Version) int {
	return delta(v.Patch, other.Patch)
}
