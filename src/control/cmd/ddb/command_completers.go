//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"io/fs"
	"path/filepath"
	"strings"
)

func listDir() []string {
	var result []string

	filepath.Walk("/mnt", func(path string, info fs.FileInfo, err error) error {
		if err != nil {
			/* ignore error */
			return nil
		}
		if strings.Contains(path, "vos-") {
			result = append(result, path)
		}
		return nil
	})
	return result
}

func openCompleter(prefix string, args []string) []string {
	suggestions := []string{"-h", "-w", "--write_mode"}
	suggestions = append(suggestions, listDir()...)

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
