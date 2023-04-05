//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/jessevdk/go-flags"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

func TestUI_SetPropertiesFlag_UnmarshalFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		settable []string
		deprKeys map[string]string
		fv       string
		expProps map[string]string
		expErr   error
	}{
		"empty": {
			expErr: errors.New("empty property"),
		},
		"invalid property": {
			settable: []string{"a", "b"},
			fv:       "c:b",
			expErr:   errors.New("not a settable property"),
		},
		"empty key": {
			fv:     " :value",
			expErr: errors.New("must not be empty"),
		},
		"empty value": {
			fv:     "key: ",
			expErr: errors.New("must not be empty"),
		},
		"key too long": {
			fv:     strings.Repeat("x", ui.MaxPropKeyLen+1) + ":value",
			expErr: errors.New("key too long"),
		},
		"valid properties": {
			settable: []string{"a", "b"},
			fv:       "b:d,a:c",
			expProps: map[string]string{"b": "d", "a": "c"},
		},
		"arbitrary properties": {
			fv:       "a:b,c:d",
			expProps: map[string]string{"a": "b", "c": "d"},
		},
		"deprecated properties": {
			settable: []string{"a", "b"},
			deprKeys: map[string]string{"e": "a"},
			fv:       "e:b,b:d",
			expProps: map[string]string{"a": "b", "b": "d"},
		},
		"avocado unescaped chars": {
			fv:       `attr4:~@#$%^*-=_+[]{}/?.,~@#$%^*-=_+[]{}/?.:attr11`,
			expProps: map[string]string{"attr4": `~@#$%^*-=_+[]{}/?.`, `~@#$%^*-=_+[]{}/?.`: "attr11"},
		},
		"avocado escaped chars": {
			fv:       `attr5:&()\\;!<>\,\:,&()\\;!<>\,\::attr12`,
			expProps: map[string]string{"attr5": `&()\\;!<>\,\:`, `&()\\;!<>\,\:`: "attr12"},
		},
		", in quotes": {
			fv:       `a:"b,c",b:'d,e',c:"'f,g'",d:'"h,i"',"e,f":g`,
			expProps: map[string]string{"a": `"b,c"`, "b": `'d,e'`, "c": `"'f,g'"`, "d": `'"h,i"'`, `"e,f"`: "g"},
		},
		": in quotes": {
			fv:       `a:"b:c",b:'d:e',c:"'f:g'",d:'"h:i"',"e,f":g`,
			expProps: map[string]string{"a": `"b:c"`, "b": `'d:e'`, "c": `"'f:g'"`, "d": `'"h:i"'`, `"e,f"`: "g"},
		},
		": escaped": {
			fv:       `a:b\:c,b\\\:c:d`,
			expProps: map[string]string{"a": `b\:c`, `b\\\:c`: "d"},
		},
		", escaped": {
			fv:       `a:b\,c,b\\\,c:d`,
			expProps: map[string]string{"a": `b\,c`, `b\\\,c`: "d"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.SetPropertiesFlag{}
			f.SettableKeys(tc.settable...)
			f.DeprecatedKeyMap(tc.deprKeys)

			gotErr := f.UnmarshalFlag(tc.fv)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expProps, f.ParsedProps); diff != "" {
				t.Fatalf("unexpected properties: (-want, +got)\n%s\n", diff)
			}
		})
	}

}

/* NB: Doesn't work with go < 1.18
func FuzzUI_SetPropertiesFlag_UnmarshalFlag(f *testing.F) {
	f.Fuzz(func(t *testing.T, fv string) {
		f := ui.SetPropertiesFlag{}

		// Simple fuzzing; just make sure it doesn't panic on
		// bad inputs.
		gotErr := f.UnmarshalFlag(fv)
		if gotErr != nil {
			t.Log(gotErr)
		}
	})
}
*/

func TestUI_SetPropertiesFlag_Complete(t *testing.T) {
	comps := ui.CompletionMap{
		"alpha":    []string{"alpha1", "alpha2"},
		"bravo":    []string{"bravo1", "bravo2", "bravo3"},
		"zulu":     []string{"zulu2", "zulu4", "zulu3", "zulu1"},
		"aardvark": []string{"aardvark1", "aardvark2", "aardvark3"},
		"banana":   []string{"banana1", "banana2", "banana3"},
		"charlie":  []string{},
	}

	for name, tc := range map[string]struct {
		match   string
		expSugg []flags.Completion
	}{
		"no match": {
			match: "quork",
		},
		"empty": {
			match: "",
			expSugg: []flags.Completion{
				{Item: "aardvark:"},
				{Item: "alpha:"},
				{Item: "banana:"},
				{Item: "bravo:"},
				{Item: "charlie:"},
				{Item: "zulu:"},
			},
		},
		"multi key match": {
			match: "a",
			expSugg: []flags.Completion{
				{Item: "aardvark:"},
				{Item: "alpha:"},
			},
		},
		"key nosep": {
			match: "bravo",
			expSugg: []flags.Completion{
				{Item: "bravo:"},
			},
		},
		"full key": {
			match: "zulu:",
			expSugg: []flags.Completion{
				{Item: "zulu1"},
				{Item: "zulu2"},
				{Item: "zulu3"},
				{Item: "zulu4"},
			},
		},
		"key plus partial value": {
			match: "banana:ban",
			expSugg: []flags.Completion{
				{Item: "banana1"},
				{Item: "banana2"},
				{Item: "banana3"},
			},
		},
		"key no value completions": {
			match: "charlie:",
		},
		"key:val,": {
			match: "banana:banana1,",
			expSugg: []flags.Completion{
				{Item: "banana:banana1,aardvark:"},
				{Item: "banana:banana1,alpha:"},
				{Item: "banana:banana1,bravo:"},
				{Item: "banana:banana1,charlie:"},
				{Item: "banana:banana1,zulu:"},
			},
		},
		"key:val,key": {
			match: "banana:banana1,bravo",
			expSugg: []flags.Completion{
				{Item: "banana:banana1,bravo:"},
			},
		},
		"key:val,key:": {
			match: "banana:banana1,bravo:",
			expSugg: []flags.Completion{
				{Item: "bravo1"},
				{Item: "bravo2"},
				{Item: "bravo3"},
			},
		},
		"key:val,key:val,key:": {
			match: "banana:banana1,bravo:bravo2,alpha:",
			expSugg: []flags.Completion{
				{Item: "alpha1"},
				{Item: "alpha2"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.SetPropertiesFlag{}
			f.SetCompletions(comps)

			gotSugg := f.Complete(tc.match)
			if diff := cmp.Diff(tc.expSugg, gotSugg); diff != "" {
				t.Fatalf("unexpected suggestions (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestUI_GetPropertiesFlag_UnmarshalFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		gettable []string
		deprKeys map[string]string
		fv       string
		expKeys  []string
		expErr   error
	}{
		"empty": {
			expErr: errors.New("must not be empty"),
		},
		"invalid property": {
			gettable: []string{"a", "b"},
			fv:       "c",
			expErr:   errors.New("not a gettable property"),
		},
		"key too long": {
			fv:     strings.Repeat("x", ui.MaxPropKeyLen+1),
			expErr: errors.New("key too long"),
		},
		"key is just a space": {
			fv:     " ",
			expErr: errors.New("must not be empty"),
		},
		"key contains unescaped property separator": {
			fv:     "a:b",
			expErr: errors.New("cannot contain"),
		},
		"key contains escaped property separator": {
			fv:      `a\:b`,
			expKeys: []string{`a\:b`},
		},
		"quoted properties with commas": {
			fv:      `"a,b",c`,
			expKeys: []string{`"a,b"`, `c`},
		},
		"valid properties": {
			gettable: []string{"a", "b"},
			fv:       "a,b",
			expKeys:  []string{"a", "b"},
		},
		"arbitrary properties": {
			fv:      "a,c",
			expKeys: []string{"a", "c"},
		},
		"deprecated properties": {
			gettable: []string{"a", "b"},
			deprKeys: map[string]string{"e": "a"},
			fv:       "e,b",
			expKeys:  []string{"a", "b"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.GetPropertiesFlag{}
			f.GettableKeys(tc.gettable...)
			f.DeprecatedKeyMap(tc.deprKeys)

			gotErr := f.UnmarshalFlag(tc.fv)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expKeys, f.ParsedProps.ToSlice()); diff != "" {
				t.Fatalf("unexpected properties: (-want, +got)\n%s\n", diff)
			}
		})
	}

}

func TestUI_GetPropertiesFlag_Complete(t *testing.T) {
	comps := ui.CompletionMap{
		"alpha":    []string{"alpha1", "alpha2"},
		"bravo":    []string{"bravo1", "bravo2", "bravo3"},
		"zulu":     []string{"zulu2", "zulu4", "zulu3", "zulu1"},
		"aardvark": []string{"aardvark1", "aardvark2", "aardvark3"},
		"banana":   []string{"banana1", "banana2", "banana3"},
		"charlie":  []string{},
	}

	for name, tc := range map[string]struct {
		match   string
		expSugg []flags.Completion
	}{
		"no match": {
			match: "quork",
		},
		"empty": {
			match: "",
			expSugg: []flags.Completion{
				{Item: "aardvark"},
				{Item: "alpha"},
				{Item: "banana"},
				{Item: "bravo"},
				{Item: "charlie"},
				{Item: "zulu"},
			},
		},
		"multi key match": {
			match: "a",
			expSugg: []flags.Completion{
				{Item: "aardvark"},
				{Item: "alpha"},
			},
		},
		"key": {
			match: "banana",
			expSugg: []flags.Completion{
				{Item: "banana"},
			},
		},
		"key,": {
			match: "banana,",
			expSugg: []flags.Completion{
				{Item: "banana,aardvark"},
				{Item: "banana,alpha"},
				{Item: "banana,bravo"},
				{Item: "banana,charlie"},
				{Item: "banana,zulu"},
			},
		},
		"key,key": {
			match: "banana,bravo",
			expSugg: []flags.Completion{
				{Item: "banana,bravo"},
			},
		},
		"key,key,": {
			match: "banana,bravo,",
			expSugg: []flags.Completion{
				{Item: "banana,bravo,aardvark"},
				{Item: "banana,bravo,alpha"},
				{Item: "banana,bravo,charlie"},
				{Item: "banana,bravo,zulu"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.GetPropertiesFlag{}
			f.SetCompletions(comps)

			gotSugg := f.Complete(tc.match)
			if diff := cmp.Diff(tc.expSugg, gotSugg); diff != "" {
				t.Fatalf("unexpected suggestions (-want, +got)\n%s\n", diff)
			}
		})
	}
}
