//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

/*
#include "util.h"
*/
import "C"

type objectCmd struct {
	Query objQueryCmd `command:"query" description:"query an object's layout"`
	//ListKeys objListKeysCmd `command:"list-keys" description:"list an object's keys"`
	//Dump     objDumpCmd     `command:"dump" description:"dump an object's contents"`
}

type objBaseCmd struct {
	existingContainerCmd

	ObjectIDFlag OidFlag `long:"oid" short:"i" description:"DAOS object id (deprecated; use positional arg)"`
	Args         struct {
		ObjectID OidFlag `positional-arg-name:"<DAOS object id (HI.LO)>"`
	} `positional-args:"yes"`
}

func (cmd *objBaseCmd) getOid() (C.daos_obj_id_t, error) {
	if cmd.ObjectIDFlag.Set && cmd.Args.ObjectID.Set {
		return C.daos_obj_id_t{}, errors.New("can't specify --oid and positional argument")
	}
	if cmd.ObjectIDFlag.Set {
		cmd.Args.ObjectID.Set = true
		cmd.Args.ObjectID.Oid = cmd.ObjectIDFlag.Oid
	}
	if !cmd.Args.ObjectID.Set {
		return C.daos_obj_id_t{}, &flags.Error{flags.ErrRequired, "OID is a required argument"}
	}

	return cmd.Args.ObjectID.Oid, nil
}

type objQueryCmd struct {
	objBaseCmd
}

func (cmd *objQueryCmd) Execute(_ []string) error {
	oid, err := cmd.getOid()
	if err != nil {
		return err
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(C.DAOS_COO_RO, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	ap.oid = oid
	rc := C.obj_query_hdlr(ap)

	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to query object %s", cmd.Args.ObjectID.String())
	}

	return nil
}
