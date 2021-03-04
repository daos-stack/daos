//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"encoding/json"
	"fmt"
	"strings"
	"unicode"

	"github.com/pkg/errors"
)

// Includes returns true if string target in slice.
func Includes(ss []string, target string) bool {
	return Index(ss, target) >= 0
}

// Index returns first index of target string,
// -1 if no match
func Index(ss []string, target string) int {
	for i, s := range ss {
		if s == target {
			return i
		}
	}
	return -1
}

// All returns true if all strings returns true from f.
func All(ss []string, f func(string) bool) bool {
	for _, s := range ss {
		if !f(s) {
			return false
		}
	}
	return true
}

// Any returns true if any of the strings in the slice returns true from f.
func Any(ss []string, f func(string) bool) bool {
	for _, s := range ss {
		if f(s) {
			return true
		}
	}
	return false
}

// Map returns new slice with f applied to each string in original.
func Map(ss []string, f func(string) string) (nss []string) {
	nss = make([]string, len(ss))
	for i, s := range ss {
		nss[i] = f(s)
	}
	return
}

// Filter returns new slice with only strings that return true from f.
func Filter(ss []string, f func(string) bool) (nss []string) {
	for _, s := range ss {
		if f(s) {
			nss = append(nss, s)
		}
	}
	return
}

// FilterStringMatches checks whether a filter string matches the actual string
// in a case-insensitive way. If the filter string is empty, a match is assumed.
func FilterStringMatches(filterStr, actualStr string) bool {
	return filterStr == "" || strings.EqualFold(actualStr, filterStr)
}

// IsAlphabetic checks of a string just contains alphabetic characters.
func IsAlphabetic(s string) bool {
	for _, r := range s {
		if !unicode.IsLetter(r) {
			return false
		}
	}
	return true
}

// Pluralise appends "s" to input string unless n==1.
func Pluralise(s string, n int) string {
	if n == 1 {
		return s
	}
	return s + "s"
}

// ConcatErrors builds single error from error slice.
func ConcatErrors(scanErrors []error, err error) error {
	if err != nil {
		scanErrors = append(scanErrors, err)
	}

	errStr := "scan error(s):\n"
	for _, err := range scanErrors {
		errStr += fmt.Sprintf("  %s\n", err.Error())
	}

	return errors.New(errStr)
}

// ParseNumberList converts a comma-separated string of numbers to a slice.
func ParseNumberList(stringList string, output interface{}) error {
	str := fmt.Sprintf("[%s]", stringList)
	if err := json.Unmarshal([]byte(str), output); err != nil {
		// return more user-friendly errors for malformed inputs
		switch je := err.(type) {
		case *json.SyntaxError:
			return errors.Errorf("unable to parse %q: must be a comma-separated list of numbers", stringList)
		case *json.UnmarshalTypeError:
			return errors.Errorf("invalid input: %s can not be stored in %s", je.Value, je.Type)
		default:
			// other errors are more likely to be programmer-caused
			return err
		}
	}
	return nil
}

// DedupeStringSlice is responsible for returning a slice based on
// the input with any duplicates removed.
func DedupeStringSlice(in []string) []string {
	keys := make(map[string]struct{})

	for _, el := range in {
		keys[el] = struct{}{}
	}

	out := make([]string, 0, len(keys))
	for key := range keys {
		out = append(out, key)
	}

	return out
}

// StringSliceHasDuplicates checks whether there are duplicate strings in the
// slice. If so, it returns true.
func StringSliceHasDuplicates(slice []string) bool {
	found := make(map[string]bool)

	for _, s := range slice {
		if _, ok := found[s]; ok {
			return true
		}
		found[s] = true
	}
	return false
}

// PercentageString returns string representation of percentage given
// nominator and denominator unsigned integers.
func PercentageString(part, total uint64) string {
	if total == 0 {
		return "N/A"
	}
	if part == 0 {
		return fmt.Sprintf("%v %%", 0)
	}

	return fmt.Sprintf("%v %%",
		int((float64(part)/float64(total))*float64(100)))
}
