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
	"io"
	"os"
	"strings"

	"github.com/pkg/errors"
)

// readACLFile reads in a file representing an ACL, and translates it into a
// list of ACE strings.
func readACLFile(aclFile string) ([]string, error) {
	file, err := os.Open(aclFile)
	if err != nil {
		return nil, errors.WithMessage(err, "opening ACL file")
	}
	defer file.Close()

	return parseACL(file)
}

// isACLFileComment checks whether the line is formatted as a comment for an
// ACL file.
func isACLFileComment(line string) bool {
	return strings.HasPrefix(line, "#")
}

// parseACL reads the content from io.Reader and puts the results into a list
// of Access Control Entry strings.
// Assumes that ACE strings are provided one per line.
func parseACL(reader io.Reader) ([]string, error) {
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

	return aceList, nil
}
