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
	"os"
	"strings"

	"github.com/pkg/errors"
)

// readableFile is an interface for a file that can be read from the filesystem
type readableFile interface {
	Close() error
	Read(p []byte) (n int, err error)
}

// fileOpener is an interface for opening a file.
type fileOpener interface {
	OpenFile(filename string) (readableFile, error)
}

// fileOpenerImpl is a concrete implementation of the fileOpener interface
type fileOpenerImpl struct{}

// OpenFile calls into the OS to open a file
func (e *fileOpenerImpl) OpenFile(filename string) (readableFile, error) {
	return os.Open(filename)
}

// newFileOpener creates a new instance of a fileOpener
func newFileOpener() fileOpener {
	return &fileOpenerImpl{}
}

// readACLFile opens and reads in the ACL file line by line, assuming ACEs are
// provided line by line.
func readACLFile(opener fileOpener, aclFile string) ([]string, error) {
	file, err := opener.OpenFile(aclFile)
	if err != nil {
		return nil, errors.WithMessage(err, "opening ACL file")
	}
	defer file.Close()

	aclList := make([]string, 0)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		if err = scanner.Err(); err != nil {
			return nil, errors.WithMessage(err, "reading ACL file")
		}

		line := strings.TrimSpace(scanner.Text())
		if line != "" {
			aclList = append(aclList, line)
		}
	}

	return aclList, nil
}
