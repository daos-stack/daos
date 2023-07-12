//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"encoding/json"
	"fmt"
	"strings"
)

func revString(version string) string {
	if ReleaseBuild {
		return version
	}

	revParts := []string{version}
	if Revision != "" {
		switch VCS {
		case "git":
			revParts = append(revParts, fmt.Sprintf("g%7s", Revision)[0:7])
		default:
			revParts = append(revParts, Revision)
		}
		if DirtyBuild {
			revParts = append(revParts, "dirty")
		}
	}
	return strings.Join(revParts, "-")
}

// String returns a string containing the name, version, and for non-release builds,
// the revision of the binary.
func String(name string) string {
	return fmt.Sprintf("%s version %s", name, revString(DaosVersion))
}

// MarshalJSON returns a JSON string containing a structured representation of
// the binary build info.
func MarshalJSON(name string) ([]byte, error) {
	return json.Marshal(&struct {
		Name     string `json:"name"`
		Version  string `json:"version"`
		Revision string `json:"revision,omitempty"`
		Dirty    bool   `json:"dirty,omitempty"`
		Release  bool   `json:"release,omitempty"`
	}{
		Name:     name,
		Version:  DaosVersion,
		Revision: Revision,
		Dirty:    DirtyBuild,
		Release:  ReleaseBuild,
	})
}
