//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"fmt"

	"github.com/pkg/errors"
)

const (
	// MaxMajorDelta is the default maximum major version delta
	// allowed between two components.
	MaxMajorDelta = 0
	// MaxMinorDelta is the default maximum minor version delta
	// allowed between two components.
	MaxMinorDelta = 2
)

// Component is a component of the system.
type Component string

// Matches returns true if the component is equal to other
// or one of the components is a wildcard.
func (c Component) Matches(other Component) bool {
	return c == other || c == ComponentAny || other == ComponentAny
}

func (c Component) String() string {
	return string(c)
}

var (
	// ComponentAny is a wildcard component.
	ComponentAny = Component("")
	// ComponentServer represents the control plane server.
	ComponentServer = Component("server")
	// ComponentAdmin represents the Control API client.
	ComponentAdmin = Component("admin")
	// ComponentAgent represents the compute node agent.
	ComponentAgent = Component("agent")
)

// NewVersionedComponent creates a new VersionedComponent.
func NewVersionedComponent(comp Component, version string) (*VersionedComponent, error) {
	v, err := NewVersion(version)
	if err != nil {
		return nil, err
	}

	switch comp {
	case ComponentServer, ComponentAdmin, ComponentAgent, ComponentAny:
		return &VersionedComponent{
			Component: comp,
			Version:   *v,
		}, nil
	default:
		return nil, errors.Errorf("invalid component %q", comp)
	}
}

// VersionedComponent is a component with a version.
type VersionedComponent struct {
	Version   Version
	Component Component
}

func (vc *VersionedComponent) String() string {
	return fmt.Sprintf("%s:%s", vc.Component, vc.Version)
}

// InteropRule is a rule for checking compatibility between two
// versioned components.
type InteropRule struct {
	Self        Component
	Other       Component
	Description string
	CheckFn     func(self, other *VersionedComponent) bool
}

// defaultRules are a set of default rules which should apply regardless
// of release or caller.
var defaultRules = []InteropRule{
	// For the 2.0/2.2 releases, at least, the servers must be
	// be patch-compatible (i.e. the major/minor versions must
	// be the same). This rule may need to be relaxed or modified
	// for future releases to allow servers to interoperate
	// across minor versions.
	{
		Self:        ComponentServer,
		Other:       ComponentServer,
		Description: "server/server must be patch-compatible",
		CheckFn: func(self, other *VersionedComponent) bool {
			return self.Version.PatchCompatible(other.Version)
		},
	},
	{
		Description: "no backward compatibility prior to 2.0.0",
		CheckFn: func(self, other *VersionedComponent) bool {
			v2_0 := MustNewVersion("2.0.0")
			return !((self.Version.Equals(v2_0) || self.Version.GreaterThan(v2_0)) &&
				other.Version.LessThan(v2_0))
		},
	},
}

// CheckCompatibility checks a pair of versioned components
// for compatibility based on specific interoperability constraints
// or general rules.
//
// The design is aimed at allowing a component to verify compatibility
// with another component, and the logic can be customized by callers
// for specific requirements at the call site.
//
// e.g. "I am server v2.0.0. Am I compatible with agent v1.2.0?"
func CheckCompatibility(self, other *VersionedComponent, customRules ...InteropRule) error {
	if self == nil || other == nil {
		return errors.New("nil components")
	}

	// If the versions are equal, there's nothing else to check.
	if self.Version.Equals(other.Version) {
		return nil
	}

	// Apply flexible rule-based checks first.
	for _, rule := range append(customRules, defaultRules...) {
		if rule.Self.Matches(self.Component) && rule.Other.Matches(other.Component) {
			if !rule.CheckFn(self, other) {
				return errors.Wrap(errIncompatComponents(self, other), rule.Description)
			}
		}
	}

	// NB: The following rules loosely follow the semantic versioning spec.
	// https://semver.org/

	// If only the patch versions are different, then by default
	// we assume that the components are compatible.
	if self.Version.PatchCompatible(other.Version) {
		return nil
	}

	// If the major versions are beyond the max allowed delta,
	// the components are incompatible.
	if self.Version.MajorDelta(other.Version) > MaxMajorDelta {
		return errors.Wrap(errIncompatComponents(self, other), "major version delta too large")
	}

	// If the major versions are close enough, but the minor versions
	// are beyond the max allowed delta, then the components are
	// incompatible. This rule differs from the semantic versioning
	// spec, but honors the DAOS compatibility pledge of -1/+1
	// compatibility.
	if self.Version.MinorDelta(other.Version) > MaxMinorDelta {
		return errors.Wrap(errIncompatComponents(self, other), "minor version delta too large")
	}

	// At this point, we can assume that the components are compatible.
	return nil
}
