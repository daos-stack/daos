//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package system

import (
	"errors"
	"strings"
)

const (
	// FaultDomainSeparator is the dividing character for different levels of a
	// fault domain.
	FaultDomainSeparator = "/"

	// FaultDomainNilStr is the string value of a nil FaultDomain.
	FaultDomainNilStr = "(nil)"
)

// FaultDomain represents a multi-layer fault domain.
type FaultDomain struct {
	Domains []string // Hierarchical sequence of fault domain levels
}

func (f *FaultDomain) String() string {
	if f == nil {
		return FaultDomainNilStr
	}
	if f.Empty() {
		return ""
	}
	return FaultDomainSeparator + strings.Join(f.Domains, FaultDomainSeparator)
}

// Equals checks if the fault domains are equal.
func (f *FaultDomain) Equals(other *FaultDomain) bool {
	return f.String() == other.String()
}

// NumLevels gets the number of levels in the domain.
func (f *FaultDomain) NumLevels() int {
	if f == nil {
		return 0
	}
	return len(f.Domains)
}

// Empty checks whether the fault domain is empty or not.
func (f *FaultDomain) Empty() bool {
	return f.NumLevels() == 0
}

// Level returns the descriptor for the requested level of the fault domain.
// Level 0 is the bottom level. Level (n - 1) is the top level.
func (f *FaultDomain) Level(level int) (string, error) {
	if level < 0 || level >= f.NumLevels() {
		return "", errors.New("out of range")
	}
	return f.Domains[f.topLevelIdx()-level], nil
}

func (f *FaultDomain) topLevelIdx() int {
	return f.NumLevels() - 1
}

// BottomLevel returns the descriptor for the lowest level of this fault domain.
func (f *FaultDomain) BottomLevel() string {
	bottom, _ := f.Level(0)
	return bottom
}

// TopLevel returns the descriptor for the highest level of this fault domain
// below the root.
func (f *FaultDomain) TopLevel() string {
	top, _ := f.Level(f.topLevelIdx())
	return top
}

// Overlaps determines if the two fault domains share any common parents at
// higher levels.
func (f *FaultDomain) Overlaps(d *FaultDomain) bool {
	if f.Empty() {
		return false
	}
	if f.TopLevel() == d.TopLevel() {
		return true
	}
	return false
}

// NewChild creates a FaultDomain with a level below this one.
func (f *FaultDomain) NewChild(childLevel string) (*FaultDomain, error) {
	if f == nil {
		return NewFaultDomain(childLevel)
	}
	childDomains := append(f.Domains, childLevel)
	return NewFaultDomain(childDomains...)
}

// MustCreateChild creates a child fault domain. If that is not possible, it
// panics.
func (f *FaultDomain) MustCreateChild(childLevel string) *FaultDomain {
	child, err := f.NewChild(childLevel)
	if err != nil {
		panic(err)
	}
	return child
}

// NewFaultDomain creates a FaultDomain from a sequence of strings representing
// individual levels of the domain.
// For each level of the domain, we assume case insensitivity and trim
// leading/trailing whitespace.
func NewFaultDomain(domains ...string) (*FaultDomain, error) {
	for i := range domains {
		domains[i] = strings.TrimSpace(domains[i])
		if domains[i] == "" {
			return nil, errors.New("invalid fault domain")
		}
		domains[i] = strings.ToLower(domains[i])
	}

	return &FaultDomain{
		Domains: domains,
	}, nil
}

// MustCreateFaultDomain creates a FaultDomain from a sequence of strings
// representing individual levels of the domain. If it is not possible to
// create a valid FaultDomain, it panics.
func MustCreateFaultDomain(domains ...string) *FaultDomain {
	fd, err := NewFaultDomain(domains...)
	if err != nil {
		panic(err)
	}
	return fd
}

// NewFaultDomainFromString creates a FaultDomain from a string in the fault
// domain path format (example: /rack0/pdu1/hostname).
// If the string isn't formatted correctly, returns an error.
func NewFaultDomainFromString(domainStr string) (*FaultDomain, error) {
	domainStr = strings.TrimSpace(domainStr)
	if len(domainStr) == 0 {
		return &FaultDomain{}, nil
	}

	if domainStr == FaultDomainNilStr {
		return nil, nil
	}

	if !strings.HasPrefix(domainStr, FaultDomainSeparator) {
		return nil, errors.New("invalid fault domain")
	}

	domains := strings.Split(domainStr, FaultDomainSeparator)
	domains = domains[1:]

	return NewFaultDomain(domains...)
}

// MustCreateFaultDomainFromString creates a FaultDomain from a string in the
// fault domain path format. If it is not possible to create a valid
// FaultDomain, it panics.
func MustCreateFaultDomainFromString(domainStr string) *FaultDomain {
	fd, err := NewFaultDomainFromString(domainStr)
	if err != nil {
		panic(err)
	}
	return fd
}
