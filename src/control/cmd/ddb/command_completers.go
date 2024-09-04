//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"io/fs"
	"path/filepath"
	"strings"
)

const (
	defMntPrefix = "/mnt"
)

func listDirVos(match string) (result []string) {
	if strings.HasSuffix(match, "vos-") {
		match = filepath.Dir(match)
	}
	filepath.Walk(match, func(path string, info fs.FileInfo, err error) error {
		if err != nil {
			/* ignore error */
			return nil
		}
		if strings.Contains(path, "vos-") {
			result = append(result, path)
		}
		return nil
	})
	return
}

func openCompleter(prefix string, args []string) []string {
	suggestions := []string{"-h", "-w", "--write_mode"}
	suggestions = append(suggestions, listDirVos(defMntPrefix)...)

	if len(prefix) > 0 {
		var newSuggestions []string
		for _, s := range suggestions {
			if strings.HasPrefix(s, prefix) {
				newSuggestions = append(newSuggestions, strings.Trim(s, prefix))
			}
		}
		suggestions = newSuggestions

	}

	return suggestions

}

func featureCompleter(prefix string, args []string) []string {
	suggestions := []string{"-h", "-e", "--enable", "-d", "--disable", "-s", "--show"}
	suggestions = append(suggestions, listDirVos(defMntPrefix)...)

	if len(prefix) > 0 {
		var newSuggestions []string
		for _, s := range suggestions {
			if strings.HasPrefix(s, prefix) {
				newSuggestions = append(newSuggestions, strings.Trim(s, prefix))
			}
		}
		suggestions = newSuggestions

	}

	return suggestions

}

func listDirPool(match string) (result []string) {
	if strings.HasSuffix(match, "vos-") {
		match = filepath.Dir(match)
	}
	filepath.Walk(match, func(path string, info fs.FileInfo, err error) error {
		if err != nil {
			/* ignore error */
			return nil
		}
		result = append(result, path)
		return nil
	})
	return
}

func rmPoolCompleter(prefix string, args []string) []string {
	suggestions := []string{"-h"}
	suggestions = append(suggestions, listDirPool(defMntPrefix)...)

	if len(prefix) > 0 {
		var newSuggestions []string
		for _, s := range suggestions {
			if strings.HasPrefix(s, prefix) {
				newSuggestions = append(newSuggestions, strings.Trim(s, prefix))
			}
		}
		suggestions = newSuggestions

	}

	return suggestions

}
