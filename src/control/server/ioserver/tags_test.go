//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ioserver

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

type subConfig struct {
	NestedIntOpt int `cmdShortFlag:"-n" cmdLongFlag:"--nested_int"`
}

type testConfig struct {
	NonzeroIntOpt    int    `cmdShortFlag:"-z,nonzero" cmdLongFlag:"--zero,nonzero"`
	IntOpt           int    `cmdShortFlag:"-i" cmdLongFlag:"--int"`
	UintOpt          uint32 `cmdShortFlag:"-t" cmdLongFlag:"--uint"`
	StringOpt        string `cmdShortFlag:"-s" cmdLongFlag:"--string"`
	SetBoolOpt       bool   `cmdShortFlag:"-b" cmdLongFlag:"--set_bool"`
	UnsetBoolOpt     bool   `cmdShortFlag:"-u" cmdLongFlag:"--unset_bool"`
	IntEnv           int    `cmdEnv:"INT_ENV"`
	StringEnv        string `cmdEnv:"STRING_ENV"`
	SetBoolEnv       bool   `cmdEnv:"SET_BOOL_ENV"`
	UnsetBoolEnv     bool   `cmdEnv:"UNSET_BOOL_ENV"`
	IntPtrOpt        *int   `cmdShortFlag:"-p" cmdLongFlag:"--int_ptr"`
	UnsetIntPtrOpt   *int   `cmdShortFlag:"-r" cmdLongFlag:"--unset_int_ptr"`
	Nested           subConfig
	NestedPointer    *subConfig
	NilNestedPointer *subConfig
	CircularRef      *testConfig
}

func intRef(in int) *int {
	return &in
}

var testStruct = &testConfig{
	IntOpt:     -1,
	UintOpt:    1,
	StringOpt:  "stringOpt",
	SetBoolOpt: true,
	IntEnv:     -1,
	StringEnv:  "stringEnv",
	SetBoolEnv: true,
	IntPtrOpt:  intRef(4),
	Nested: subConfig{
		NestedIntOpt: 2,
	},
	NestedPointer: &subConfig{
		NestedIntOpt: 3,
	},
}

func TestParseLongFlags(t *testing.T) {
	got, err := parseCmdTags(testStruct, longFlagTag, joinLongArgs, nil)
	if err != nil {
		t.Fatal(err)
	}

	want := []string{
		"--int=-1",
		"--uint=1",
		"--string=stringOpt",
		"--set_bool",
		"--int_ptr=4",
		"--nested_int=2",
		"--nested_int=3",
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestParseShortFlags(t *testing.T) {
	got, err := parseCmdTags(testStruct, shortFlagTag, joinShortArgs, nil)
	if err != nil {
		t.Fatal(err)
	}

	want := []string{
		"-i", "-1",
		"-t", "1",
		"-s", "stringOpt",
		"-b",
		"-p", "4",
		"-n", "2",
		"-n", "3",
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestParseEnvVars(t *testing.T) {
	got, err := parseCmdTags(testStruct, envTag, joinEnvVars, nil)
	if err != nil {
		t.Fatal(err)
	}

	want := []string{
		"INT_ENV=-1",
		"STRING_ENV=stringEnv",
		"SET_BOOL_ENV=true",
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestCircularRef(t *testing.T) {
	circular := *testStruct
	circular.CircularRef = &circular

	got, err := parseCmdTags(&circular, shortFlagTag, joinShortArgs, nil)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{
		"-i", "-1",
		"-t", "1",
		"-s", "stringOpt",
		"-b",
		"-p", "4",
		"-n", "2",
		"-n", "3",
	}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("(-want, +got):\n%s", diff)
	}
}

func TestUnhandledType(t *testing.T) {
	test := struct {
		Bad interface{} `cmdShortFlag:"blerp"`
	}{
		Bad: 1,
	}

	_, err := parseCmdTags(&test, shortFlagTag, joinShortArgs, nil)
	if err == nil {
		t.Fatal("expected error, got nil")
	}
}

func TestNilJoinFunction(t *testing.T) {
	_, err := parseCmdTags(testStruct, shortFlagTag, nil, nil)
	if err == nil {
		t.Fatal("expected error, got nil")
	}
}
