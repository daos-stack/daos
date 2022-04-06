//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"encoding/json"
	"fmt"

	"github.com/pkg/errors"
)

const (
	FindingStatusOpen     FindingStatus = ""
	FindingStatusResolved FindingStatus = "Resolved"
	FindingStatusIgnored  FindingStatus = "Ignored"
)

// TODO: Define stuff on the engine side so that we can use
// values in both places.
const (
	FindingClassUnknown FindingClass = iota
	FindingClassMember
	FindingClassPool

	FindingCodeUnknown FindingCode = iota
	FindingCodeScannedPoolMissing
	FindingCodePoolLabelMismatch
	FindingCodeMsPoolMissing
)

type (
	FindingClass int

	FindingCode int

	FindingStatus string

	FindingResolution struct {
		ID          uint32 `json:"id"`
		Description string `json:"description"`
	}

	Finding struct {
		ID          string               `json:"id"`
		Class       FindingClass         `json:"class"`
		Code        FindingCode          `json:"code"`
		Status      FindingStatus        `json:"status"`
		Ignorable   bool                 `json:"ignorable"`
		Description string               `json:"description"`
		Details     []string             `json:"details"`
		Resolution  *FindingResolution   `json:"resolution"`
		Resolutions []*FindingResolution `json:"resolutions"`
	}
)

func AnnotateFinding(f *Finding) *Finding {
	switch f.Code {
	case FindingCodeScannedPoolMissing:
		if f.Description == "" && len(f.Details) == 2 {
			f.Description = fmt.Sprintf("Scanned pool service %s (%q) missing from MS", f.Details[0], f.Details[1])
		}
		if len(f.Resolutions) == 0 {
			f.AddResolutions("Ignore", "Attempt to reconstruct MS entry from engine scan", "Remove the pool components from storage targets")
		}
	case FindingCodePoolLabelMismatch:
		if f.Description == "" && len(f.Details) == 3 {
			f.Description = fmt.Sprintf("Pool %s label mismatch; MS: %q, Svc: %q", f.Details[0], f.Details[1], f.Details[2])
		}
		if len(f.Resolutions) == 0 {
			f.AddResolutions("Ignore", "Use the pool label stored in the MS", "Use the pool label in the pool service")
		}
	case FindingCodeMsPoolMissing:
		if f.Description == "" && len(f.Details) == 2 {
			f.Description = fmt.Sprintf("MS pool %s (%q) not found in engine scan", f.Details[0], f.Details[1])
		}
		if len(f.Resolutions) == 0 {
			f.AddResolutions("Ignore", "Remove the MS entry")
		}
	}

	// Last resort...
	if f.Description == "" {
		f.Description = fmt.Sprintf("%s %d", f.Class, f.Code)
	}

	return f
}

func (fs FindingStatus) String() string {
	if fs == FindingStatusOpen {
		return "Open"
	}
	return string(fs)
}

func (fc FindingClass) String() string {
	switch fc {
	case FindingClassMember:
		return "Member"
	case FindingClassPool:
		return "Pool"
	default:
		return "Unknown"
	}
}

func (fc *FindingClass) FromString(s string) error {
	switch s {
	case "Member":
		*fc = FindingClassMember
	case "Pool":
		*fc = FindingClassPool
	default:
		return errors.Errorf("unknown finding class %q", s)
	}
	return nil
}

func (fc FindingClass) MarshalJSON() ([]byte, error) {
	return []byte(`"` + fc.String() + `"`), nil
}

func (fc *FindingClass) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	return fc.FromString(s)
}

func (f *Finding) SetStatus(status FindingStatus) error {
	if status == FindingStatusIgnored && !f.Ignorable {
		return errors.Errorf("finding %s is not ignorable", f.ID)
	}

	f.Status = status
	return nil
}

func (f *Finding) SetResolution(res *FindingResolution) error {
	if res == nil {
		return errors.New("resolution is nil")
	}

	for _, r := range f.Resolutions {
		if r.ID == res.ID {
			f.Resolution = res
			f.Status = FindingStatusResolved
			return nil
		}
	}

	return errors.Errorf("resolution %d not found", res.ID)
}

func (f *Finding) AddResolutions(desc ...string) {
	start := len(f.Resolutions)
	for i := start; i < start+len(desc); i++ {
		f.Resolutions = append(f.Resolutions, &FindingResolution{
			ID:          uint32(i),
			Description: desc[i],
		})
	}
}

func (f *Finding) SetIgnorable() *Finding {
	f.Ignorable = true
	return f
}

func (f *Finding) WithResolutions(resolutions ...string) *Finding {
	f.Resolutions = nil
	f.AddResolutions(resolutions...)
	return f
}

func NewPoolFinding(code FindingCode, details ...string) *Finding {
	return AnnotateFinding(&Finding{
		Code:    code,
		Class:   FindingClassPool,
		Details: details,
	})
}

func NewMemberFinding(code FindingCode, details ...string) *Finding {
	return AnnotateFinding(&Finding{
		Code:    code,
		Class:   FindingClassMember,
		Details: details,
	})
}
