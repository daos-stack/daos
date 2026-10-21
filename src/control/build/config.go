//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// ErrDefaultConfigNotFound indicates that no config file was found in the default locations.
type ErrDefaultConfigNotFound struct {
	Errors []error
}

func (e *ErrDefaultConfigNotFound) Error() string {
	errStrs := []string{}
	for _, err := range e.Errors {
		errStrs = append(errStrs, err.Error())
	}

	return fmt.Sprintf("config file not found in default locations: [%s]", strings.Join(errStrs, "] ["))
}

// IsDefaultConfigNotFound checks the error to ensure it is a config not found error.
func IsDefaultConfigNotFound(err error) bool {
	if _, ok := err.(*ErrDefaultConfigNotFound); ok {
		return true
	}
	return false
}

// ConfigDirs is an ordered list of directories to search for DAOS configuration files.
func ConfigDirs() []string {
	dirs := []string{ConfigDir}
	if ConfigDir != DefaultConfigDir {
		dirs = append(dirs, DefaultConfigDir)
	}
	return dirs
}

// FindConfigFilePath searches for a file with a given name in the configuration directories.
func FindConfigFilePath(filename string) (string, error) {
	if filepath.IsAbs(filename) || filepath.Base(filename) != filename {
		return "", fmt.Errorf("%q already specifies a path", filename)
	}

	var errs []error
	searchDirs := ConfigDirs()
	for _, dir := range searchDirs {
		path := filepath.Join(dir, filename)
		_, err := os.Stat(path)
		if err == nil {
			return path, nil
		}
		errs = append(errs, err)
	}

	return "", &ErrDefaultConfigNotFound{Errors: errs}
}
