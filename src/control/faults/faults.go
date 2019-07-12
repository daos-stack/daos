package faults

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
)

// Resolution represents a potential fault resolution.
type Resolution string

const (
	// ResolutionEmpty is equivalent to an empty string.
	ResolutionEmpty = Resolution("")
	// ResolutionUnknown indicates that there is no known
	// resolution for the fault.
	ResolutionUnknown = Resolution("no known resolution")
	// ResolutionNone indicates that the fault cannot be
	// resolved.
	ResolutionNone = Resolution("none")
)

func (r Resolution) String() string {
	return string(r)
}

const (
	UnknownDomainStr      = "unknown"
	UnknownDescriptionStr = "unknown fault"
)

var (
	// UnknownFault represents an unknown fault.
	UnknownFault = &Fault{
		Code:       CodeUnknown,
		Resolution: ResolutionUnknown,
	}
)

// Fault represents a well-known error specific to a domain,
// along with an optional potential resolution for the error.
//
// It implements the error interface and can be used
// interchangeably with regular "dumb" errors.
type Fault struct {
	Domain      string
	Code        Code
	Description string
	Resolution  Resolution
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
	if !ok {
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
	if !ok {
		return fmt.Sprintf(fmtStr, UnknownDomainStr, CodeUnknown, ResolutionUnknown)
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
	if !ok || f.Resolution == ResolutionEmpty {
		return false
	}
	return true
}
