//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package fault

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault/code"
)

const (
	// ResolutionEmpty is equivalent to an empty string.
	ResolutionEmpty = ""
	// ResolutionUnknown indicates that there is no known
	// resolution for the fault.
	ResolutionUnknown = "no known resolution"
	// ResolutionNone indicates that the fault cannot be
	// resolved.
	ResolutionNone = "none"
)

const (
	UnknownDomainStr      = "unknown"
	UnknownDescriptionStr = "unknown fault"
)

var (
	// UnknownFault represents an unknown fault.
	UnknownFault = &Fault{
		Code:       code.Unknown,
		Resolution: ResolutionUnknown,
	}
)

// Fault represents a well-known error specific to a domain,
// along with an optional potential resolution for the error.
//
// It implements the error interface and can be used
// interchangeably with regular "dumb" errors.
type Fault struct {
	// Domain indicates the group or family for the fault
	// (e.g. storage, network, etc.) It is used as a prefix
	// when displaying the fault.
	Domain string
	// Code is the unique numeric identifier for known faults.
	Code code.Code
	// Description is the main description of the fault. It usually
	// includes the reason for the fault, and therefore it is not
	// necessary to display both Description and Reason.
	Description string
	// Reason is a short (single sentence) description of the
	// fault, to be displayed where brevity is preferred.
	Reason string
	// Resolution is used to suggest possible solutions for
	// the fault, if appropriate.
	Resolution string
}

func sanitizeDomain(inDomain string) (outDomain string) {
	outDomain = UnknownDomainStr
	if inDomain != "" {
		// sanitize the domain to ensure grep friendliness
		outDomain = strings.Join(
			strings.Fields(
				strings.Replace(inDomain, ":", " ", -1),
			), "_")
	}
	return
}

func sanitizeDescription(inDescription string) (outDescription string) {
	outDescription = UnknownDescriptionStr
	if inDescription != "" {
		outDescription = inDescription
	}
	return
}

func (f *Fault) Error() string {
	return fmt.Sprintf("%s: code = %d description = %q",
		sanitizeDomain(f.Domain), f.Code, sanitizeDescription(f.Description))
}

// Equals attempts to compare the given error to this one. If they both
// resolve to the same fault code, then they are considered equivalent.
func (f *Fault) Equals(raw error) bool {
	other, ok := errors.Cause(raw).(*Fault)
	if !ok || other == nil {
		return false
	}
	return f.Code == other.Code
}

// ShowResolutionFor attempts to return the resolution string for the
// given error. If the error is not a fault or does not have a
// resolution set, then the string value of ResolutionUnknown
// is returned.
func ShowResolutionFor(raw error) string {
	fmtStr := "%s: code = %d resolution = %q"

	f, ok := errors.Cause(raw).(*Fault)
	if !ok || f == nil {
		return fmt.Sprintf(fmtStr, UnknownDomainStr, code.Unknown, ResolutionUnknown)
	}
	if f.Resolution == ResolutionEmpty {
		return fmt.Sprintf(fmtStr, sanitizeDomain(f.Domain), f.Code, ResolutionUnknown)
	}
	return fmt.Sprintf(fmtStr, sanitizeDomain(f.Domain), f.Code, f.Resolution)
}

// HasResolution indicates whether or not the error has a resolution
// defined.
func HasResolution(raw error) bool {
	f, ok := errors.Cause(raw).(*Fault)
	if !ok || f == nil || f.Resolution == ResolutionEmpty {
		return false
	}
	return true
}

// IsFault indicates whether or not the error is a Fault
func IsFault(raw error) bool {
	_, ok := errors.Cause(raw).(*Fault)
	return ok
}
