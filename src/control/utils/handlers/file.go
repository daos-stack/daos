//
// (C) Copyright 2018-2019 Intel Corporation.
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

package handlers

import (
	"bufio"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	yaml "gopkg.in/yaml.v2"
)

// GetAbsInstallPath retrieves absolute path of files in daos install dir
func GetAbsInstallPath(relPath string) (string, error) {
	ex, err := os.Executable()
	if err != nil {
		return "", err
	}
	return filepath.Join(filepath.Dir(ex), "..", relPath), nil
}

// GetFilePaths return full file paths in given directory with
// matching file extensions
func GetFilePaths(dir string, ext string) ([]string, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	extension := ext
	// if extension has been provided without '.' prefix, add one
	if filepath.Ext(ext) == "" {
		extension = fmt.Sprintf(".%s", ext)
	}
	var matchingFiles []string
	for _, file := range files {
		if filepath.Ext(file.Name()) == extension {
			matchingFiles = append(
				matchingFiles,
				fmt.Sprintf("%s/%s", dir, file.Name()))
		}
	}
	return matchingFiles, nil
}

// SplitFile separates file content into contiguous sections separated by
// a blank line.
func SplitFile(path string) (sections [][]string, err error) {
	file, err := os.Open(path)
	if err != nil {
		return
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	var lines []string
	for scanner.Scan() {
		if scanner.Text() == "" {
			sections = append(sections, lines)
			lines = make([]string, 0)
		} else {
			lines = append(lines, scanner.Text())
		}
	}
	if len(lines) > 0 {
		sections = append(sections, lines)
	}

	return
}

// OpenNewFile overrides existing or creates new file with default options
func OpenNewFile(path string) (*os.File, error) {
	return os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
}

// WriteSlice writes string slice to specified file, overwriting and creating
// if non-existent.
func WriteSlice(path string, slice []string) (err error) {
	file, err := OpenNewFile(path)
	if err != nil {
		return
	}
	defer file.Close()

	sep := "\n"
	for _, line := range slice {
		if _, err = file.WriteString(line + sep); err != nil {
			return
		}
	}
	file.Sync()
	return
}

// WriteString writes string to specified file, wrapper around WriteSlice.
func WriteString(path string, s string) error {
	return WriteSlice(path, []string{s})
}

// StructsToStrings returns yaml representation (as a list of strings) of any
// interface but avoids fields/lines prefixed with xxx_ such as added by
// protobuf boilerplate.
func StructsToString(i interface{}) (lines string, err error) {
	s, err := yaml.Marshal(i)
	if err != nil {
		return
	}
	for _, l := range strings.Split(string(s), "\n") {
		if !strings.Contains(l, "xxx_") {
			lines = lines + l + "\n"
		}
	}
	return
}

// PrintStructs dumps friendly YAML representation of structs to stdout
// proceeded with "name" identifier.
func PrintStructs(name string, i interface{}) {
	println(name + ":")
	s, err := StructsToString(i)
	if err != nil {
		println("Unable to YAML encode response: ", err.Error())
		return
	}
	println(s)
}
