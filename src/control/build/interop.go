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
			Version:   v,
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
	Match         func(self, other *VersionedComponent) bool
}

// Matches returns true if the rule matches the components.
func (rule *InteropRule) Matches(self, other *VersionedComponent) bool {
	// Basic test.
	if !(rule.Self.Matches(self.Component) && rule.Other.Matches(other.Component)) {
		return false
	}

	// Apply custom match logic if the rule has it.
	if rule.Match != nil {
		return rule.Match(self, other)
	}

	return true
}

// defaultRules are a set of default rules which should apply regardless
// of release or caller.
var defaultRules = []*InteropRule{
	// Allow older agents to talk to newer servers, or newer agents to talk
	// to older servers, but only up to the allowed minor delta.
	// NB: Compatibility between a new major release (e.g. v3.0.x) and the
	// previous minor release will need to be explicitly defined via a
	// custom release rule, if appropriate.
	{
		Self:          ComponentServer,
		Other:         ComponentAgent,
		Description:   "server and agent must be within 2 minor versions",
		StopOnSuccess: true,
		Check: func(self, other *VersionedComponent) bool {
			return (self.Version.MajorDelta(other.Version) == 0 &&
				self.Version.MinorDelta(other.Version) <= MaxMinorDelta)
		},
	},
	// Should never happen, but just in case. We know that releases
	// prior to 2.0.0 will never be compatible with 2.0.0 or above.
	{
		Description: "no backward compatibility prior to 2.0.0",
		Check: func(self, other *VersionedComponent) bool {
			v2_0 := MustNewVersion("2.0.0")
			return !(self.Version.GreaterThanOrEquals(v2_0) && other.Version.LessThan(v2_0))
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

// releaseRules are a set of rules which apply to a specific release,
// and are likely to change between releases.
var releaseRules = []*InteropRule{}

// CheckCompatibility checks a pair of versioned components
// for compatibility based on specific interoperability constraints
// or general rules.
//
// The design is aimed at allowing a component to verify compatibility
// with another component, and the logic can be customized by callers
// for specific requirements at the call site.
//
// e.g. "I am server v2.0.0. Am I compatible with agent v1.2.0?"
func CheckCompatibility(self, other *VersionedComponent, customRules ...*InteropRule) error {
	if self == nil || other == nil {
		return errors.New("nil components")
	}

	// If the versions are equal, there's nothing else to check.
	if self.Version.Equals(other.Version) {
		return nil
	}

	// Apply custom rules first (if any), then apply the default rules.
	for _, rule := range append(customRules, append(releaseRules, defaultRules...)...) {
		if rule == nil && rule.Check == nil {
			return errors.New("nil rule or check")
		}
		if rule.Matches(self, other) {
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
