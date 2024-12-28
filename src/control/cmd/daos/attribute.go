//
// (C) Copyright 2018-2021 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

type attrType int

const (
	poolAttr attrType = iota
	contAttr
)

func (at attrType) String() string {
	switch at {
	case poolAttr:
		return "pool"
	case contAttr:
		return "container"
	default:
		return "unknown"
	}
}

type (
	attrCmd interface {
		MustLogCtx() context.Context
		cmdutil.JSONOutputter
		logging.Logger
	}

	attrListerGetter interface {
		ListAttributes(context.Context) ([]string, error)
		GetAttributes(context.Context, ...string) (daos.AttributeList, error)
	}

	attrSetter interface {
		SetAttributes(context.Context, ...*daos.Attribute) error
	}

	attrDeleter interface {
		DeleteAttributes(context.Context, ...string) error
	}
)

func listAttributes(cmd attrCmd, alg attrListerGetter, at attrType, id string, verbose bool) error {
	var attrs daos.AttributeList
	if !verbose {
		attrNames, err := alg.ListAttributes(cmd.MustLogCtx())
		if err != nil {
			return errors.Wrapf(err, "failed to list attributes for %s %s", at, id)
		}
		attrs = attrListFromNames(attrNames)
	} else {
		var err error
		attrs, err = alg.GetAttributes(cmd.MustLogCtx())
		if err != nil {
			return errors.Wrapf(err, "failed to get attributes for %s %s", at, id)
		}
	}

	if cmd.JSONOutputEnabled() {
		if verbose {
			return cmd.OutputJSON(attrs.AsMap(), nil)
		}
		return cmd.OutputJSON(attrs.AsList(), nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for %s %s:", at, id)
	pretty.PrintAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

func getAttributes(cmd attrCmd, alg attrListerGetter, at attrType, id string, names ...string) error {
	attrs, err := alg.GetAttributes(cmd.MustLogCtx(), names...)
	if err != nil {
		return errors.Wrapf(err, "failed to get attributes for %s %s", at, id)
	}

	if cmd.JSONOutputEnabled() {
		// Maintain compatibility with older behavior.
		if len(names) == 1 && len(attrs) == 1 {
			return cmd.OutputJSON(attrs[0], nil)
		}
		return cmd.OutputJSON(attrs, nil)
	}

	var bld strings.Builder
	title := fmt.Sprintf("Attributes for %s %s:", at, id)
	pretty.PrintAttributes(&bld, title, attrs...)

	cmd.Info(bld.String())

	return nil
}

func setAttributes(cmd attrCmd, as attrSetter, at attrType, id string, attrMap map[string]string) error {
	if len(attrMap) == 0 {
		return errors.New("attribute name and value are required")
	}

	attrs := make(daos.AttributeList, 0, len(attrMap))
	for key, val := range attrMap {
		attrs = append(attrs, &daos.Attribute{
			Name:  key,
			Value: []byte(val),
		})
	}

	if err := as.SetAttributes(cmd.MustLogCtx(), attrs...); err != nil {
		return errors.Wrapf(err, "failed to set attributes on %s %s", at, id)
	}
	cmd.Infof("Attributes successfully set on %s %q", at, id)

	return nil
}

func delAttributes(cmd attrCmd, ad attrDeleter, at attrType, id string, names ...string) error {
	attrsString := strings.Join(names, ",")
	if err := ad.DeleteAttributes(cmd.MustLogCtx(), names...); err != nil {
		return errors.Wrapf(err, "failed to delete attributes %s on %s %s", attrsString, at, id)
	}
	cmd.Infof("Attribute(s) %s successfully deleted on %s %q", attrsString, at, id)

	return nil
}
