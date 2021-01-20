//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ioserver

import (
	"fmt"
	"reflect"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

const (
	shortFlagTag = "cmdShortFlag"
	longFlagTag  = "cmdLongFlag"
	envTag       = "cmdEnv"

	nonZero = "nonzero"
)

type (
	refMap  map[uintptr]struct{}
	tagOpts map[string]struct{}
	joinFn  func(args []string) []string
)

func joinLongArgs(args []string) []string {
	switch len(args) {
	case 2:
		return []string{args[0] + "=" + args[1]}
	case 1:
		return []string{args[0]}
	default:
		return nil
	}
}

func joinShortArgs(args []string) []string {
	switch len(args) {
	case 2:
		return []string{args[0], args[1]}
	case 1:
		return []string{args[0]}
	default:
		return nil
	}
}

func joinEnvVars(args []string) []string {
	switch len(args) {
	case 2:
		return joinLongArgs(args)
	case 1:
		return []string{args[0] + "=true"}
	default:
		return nil
	}
}

func (opts tagOpts) hasOpt(name string) bool {
	_, found := opts[name]
	return found
}

func parseTag(in string) (tag string, opts tagOpts) {
	optList := strings.Split(in, ",")
	tag = optList[0]

	opts = make(tagOpts)
	for _, opt := range optList {
		opts[opt] = struct{}{}
	}

	return
}

func parseCmdTags(in interface{}, tagFilter string, joiner joinFn, seenRefs refMap) (out []string, err error) {
	if joiner == nil {
		return nil, errors.New("nil joinFn")
	}
	if seenRefs == nil {
		seenRefs = make(refMap)
	}

	inVal := reflect.ValueOf(in)

	// don't process nil/circular refs
	if inVal.Kind() == reflect.Uintptr || inVal.Kind() == reflect.Ptr {
		if inVal.IsNil() {
			return
		}

		if _, seen := seenRefs[inVal.Pointer()]; seen {
			return
		}
		seenRefs[inVal.Pointer()] = struct{}{}
	}
	inVal = reflect.Indirect(inVal)

	if inVal.Kind() != reflect.Struct {
		return
	}

	for i := 0; i < inVal.NumField(); i++ {
		f := inVal.Type().Field(i)
		fVal := inVal.Field(i)

		if tagVal, ok := f.Tag.Lookup(tagFilter); ok {
			tag, opts := parseTag(tagVal)

			switch f.Type.Kind() {
			case reflect.String:
				if fVal.String() != "" {
					out = append(out, joiner([]string{tag, fVal.String()})...)
				}
			case reflect.Bool:
				isSet := fVal.Bool()
				if isSet {
					out = append(out, joiner([]string{tag})...)
				}
			case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
				if fVal.Int() == 0 && opts.hasOpt(nonZero) {
					continue
				}
				strVal := strconv.FormatInt(fVal.Int(), 10)
				out = append(out, joiner([]string{tag, strVal})...)
			case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
				if fVal.Uint() == 0 && opts.hasOpt(nonZero) {
					continue
				}
				strVal := strconv.FormatUint(fVal.Uint(), 10)
				out = append(out, joiner([]string{tag, strVal})...)
			case reflect.Uintptr, reflect.Ptr:
				if fVal.IsNil() {
					continue
				}

				// For a non-nil pointer value, we just need to
				// construct a temporary struct to wrap the
				// dereferenced value and then run it through
				// this function again.
				iVal := reflect.Indirect(fVal)
				typ := reflect.StructOf([]reflect.StructField{
					{
						Name: f.Name,
						Type: iVal.Type(),
						Tag:  f.Tag,
					},
				})
				v := reflect.New(typ).Elem()
				v.Field(0).Set(iVal)

				deref, err := parseCmdTags(v.Addr().Interface(), tagFilter, joiner, seenRefs)
				if err != nil {
					return nil, err
				}
				out = append(out, deref...)
			default:
				return nil, fmt.Errorf("unhandled tag type %s", f.Type.Kind())
			}

			continue
		}

		nested, err := parseCmdTags(fVal.Interface(), tagFilter, joiner, seenRefs)
		if err != nil {
			return nil, err
		}
		out = append(out, nested...)
	}

	return
}
