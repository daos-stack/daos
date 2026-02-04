//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

var (
	vosRegexp = regexp.MustCompile(`^.+/vos-[1-9]*[[:digit:]]$`)
)

func listVosFiles(match string) (result []string) {
	result = []string{}

	matches, err := filepath.Glob(match + "*")
	if err != nil {
		return
	}
	for _, match := range matches {
		path := filepath.Clean(match)
		fi, err := os.Stat(path)
		if err != nil {
			continue
		}

		switch mode := fi.Mode(); {
		case mode.IsDir():
			result = append(result, path+string(os.PathSeparator))
		case mode.IsRegular():
			if !vosRegexp.MatchString(path) {
				continue
			}
			result = append(result, path)
		}
	}

	return
}

func listPoolDirs(match string) (result []string) {
	result = []string{}

	matches, err := filepath.Glob(match + "*")
	if err != nil {
		return
	}

	for _, match := range matches {
		path := filepath.Clean(match)
		fi, err := os.Stat(path)
		if err != nil {
			continue
		}
		if !fi.Mode().IsDir() {
			continue
		}
		entries, err := os.ReadDir(path)
		if err != nil {
			continue
		}
		if len(entries) == 0 {
			continue
		}
		has_dir := false
		is_pool := false
		for _, entry := range entries {
			if entry.IsDir() {
				has_dir = true
				continue
			}
			if !entry.Type().Perm().IsRegular() {
				continue
			}
			if !vosRegexp.MatchString(filepath.Join(path, entry.Name())) {
				continue
			}
			result = append(result, path)
			is_pool = true
			break
		}
		if has_dir && !is_pool {
			result = append(result, path+string(os.PathSeparator))
		}
	}

	return
}

func appendSuggestion(suggestions []string, suggestion string, prefix string) []string {
	if len(prefix) == 0 {
		return append(suggestions, suggestion)
	}

	if !strings.HasPrefix(suggestion, prefix) {
		return suggestions
	}

	if len(suggestion) > 2 && suggestion[1] == prefix[0] {
		// Workaround to properly handle invalid prefix management
		return append(suggestions, suggestion)
	}
	return append(suggestions, strings.TrimPrefix(suggestion, prefix))
}

func filterSuggestions(prefix string, initialSuggestions, additionalSuggestions []string) (suggestions []string) {
	for _, suggestion := range initialSuggestions {
		suggestions = appendSuggestion(suggestions, suggestion, prefix)
	}

	for _, suggestion := range additionalSuggestions {
		suggestions = appendSuggestion(suggestions, suggestion, prefix)
	}

	return
}

func openCompleter(prefix string, args []string) []string {
	return filterSuggestions(
		prefix,
		[]string{"-w", "--write_mode", "-p", "--db_path=", "-h", "--help"},
		listVosFiles(prefix),
	)
}

func featureCompleter(prefix string, args []string) []string {
	return filterSuggestions(
		prefix,
		[]string{"-e", "--enable", "-d", "--disable", "-s", "--show", "-h", "--help"},
		listVosFiles(prefix))
}

func rmPoolCompleter(prefix string, args []string) []string {
	return filterSuggestions(
		prefix,
		[]string{"-h", "--help"},
		listVosFiles(prefix))
}
