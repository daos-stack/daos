package common

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestCommon_ParseInts(t *testing.T) {
	for name, tc := range map[string]struct {
		ints    string
		expInts []uint32
		expErr  error
	}{
		"empty":              {"", []uint32{}, nil},
		"valid single":       {"0", []uint32{0}, nil},
		"valid multiple":     {"0,1,2,3", []uint32{0, 1, 2, 3}, nil},
		"invalid alphabetic": {"0,A,", nil, errors.New("invalid character 'A' looking for beginning of value")},
		"invalid negative":   {"-1", nil, errors.New("json: cannot unmarshal number -1 into Go value of type uint32")},
	} {
		t.Run(name, func(t *testing.T) {
			gotInts, gotErr := ParseInts(tc.ints)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expInts, gotInts); diff != "" {
				t.Fatalf("unexpected integer list (-want, +got):\n%s\n", diff)
			}
		})
	}
}
