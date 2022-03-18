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
	Self          Component
	Other         Component
	Description   string
	StopOnSuccess bool // If set, and the rule is satisfied, stop checking rules.
	Check         func(self, other *VersionedComponent) bool
}

// defaultRules are a set of default rules which should apply regardless
// of release or caller.
var defaultRules = []InteropRule{
	// Should never happen, but just in case. We know that releases
	// prior to 2.0.0 will never be compatible with 2.0.0 or above.
	{
		Description: "no backward compatibility prior to 2.0.0",
		Check: func(self, other *VersionedComponent) bool {
			v2_0 := MustNewVersion("2.0.0")
			return !((self.Version.Equals(v2_0) || self.Version.GreaterThan(v2_0)) &&
				other.Version.LessThan(v2_0))
		},
	},
	// The default DAOS compatibility rule, which is:
	//   - Only components with the same major/minor versions are compatible
	{
		Description: "standard DAOS compatibility",
		Check: func(self, other *VersionedComponent) bool {
			return self.Version.PatchCompatible(other.Version)
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

	// Apply custom rules first (if any), then apply the default rules.
	for _, rule := range append(customRules, defaultRules...) {
		if rule.Self.Matches(self.Component) && rule.Other.Matches(other.Component) {
			if !rule.Check(self, other) {
				return errors.Wrap(errIncompatComponents(self, other), rule.Description)
			}
			if rule.StopOnSuccess {
				break
			}
		}
	}

	// At this point, we can assume that the components are compatible.
	return nil
}
