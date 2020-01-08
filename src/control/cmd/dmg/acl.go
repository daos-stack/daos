//
// (C) Copyright 2019 Intel Corporation.
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

package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
)

// readACLFile reads in a file representing an ACL, and translates it into an
// AccessControlList structure
func readACLFile(aclFile string) (*client.AccessControlList, error) {
	file, err := os.Open(aclFile)
	if err != nil {
		return nil, errors.WithMessage(err, "opening ACL file")
	}
	defer file.Close()

	acl, err := parseACL(file)
	if err != nil {
		return nil, err
	}
	if acl.Empty() {
		return nil, errors.New(fmt.Sprintf("ACL file '%s' contains no entries", aclFile))
	}

	return acl, nil
}

// isACLFileComment checks whether the line is formatted as a comment for an
// ACL file.
func isACLFileComment(line string) bool {
	return strings.HasPrefix(line, "#")
}

// parseACL reads the content from io.Reader and puts the results into a
// client.AccessControlList structure.
// Assumes that ACE strings are provided one per line.
func parseACL(reader io.Reader) (*client.AccessControlList, error) {
	aceList := make([]string, 0)
	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return nil, errors.WithMessage(err, "reading ACL file")
		}

		line := strings.TrimSpace(scanner.Text())
		if line != "" && !isACLFileComment(line) {
			aceList = append(aceList, line)
		}
	}

	return &client.AccessControlList{Entries: aceList}, nil
}

// formatACL converts the AccessControlList to a human-readable string.
func formatACL(acl *client.AccessControlList) string {
	var builder strings.Builder

	if acl.HasOwner() {
		fmt.Fprintf(&builder, "# Owner: %s\n", acl.Owner)
	}

	if acl.HasOwnerGroup() {
		fmt.Fprintf(&builder, "# Owner Group: %s\n", acl.OwnerGroup)
	}

	builder.WriteString("# Entries:\n")
	if acl.Empty() {
		builder.WriteString("#   None\n")
		return builder.String()
	}

	for _, ace := range acl.Entries {
		fmt.Fprintf(&builder, "%s\n", ace)
	}

	return builder.String()
}
