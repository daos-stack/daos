//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package convert

import ("encoding/json"
	"github.com/pkg/errors"
)
// Types attempts an automatic conversion between the in/out types
// using JSON as an intermediate representation.
func Types(in interface{}, out interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return errors.Wrap(err, "Marshal JSON")
	}

	err = json.Unmarshal(data, out)
	if err != nil {
		return errors.Wrap(err, "Unmarshal JSON")
	}
	return nil
}
