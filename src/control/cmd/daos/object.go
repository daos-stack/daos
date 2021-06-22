//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

/*
#include <daos.h>

#include "daos_hdlr.h"
*/
import "C"

type objectCmd struct {
	Query objQueryCmd `command:"query" description:"query an object's layout"`
	//ListKeys objListKeysCmd `command:"list-keys" description:"list an object's keys"`
	//Dump     objDumpCmd     `command:"dump" description:"dump an object's contents"`
}

type objBaseCmd struct {
	existingContainerCmd

	ObjectIDFlag oidFlag `long:"oid" short:"i" description:"DAOS object id (deprecated; use positional arg)"`
	Args         struct {
		ObjectID oidFlag `positional-arg-name:"<DAOS object id (HI.LO)>"`
	} `positional-args:"yes"`
}

func (cmd *objBaseCmd) getOid() (C.daos_obj_id_t, error) {
	if cmd.ObjectIDFlag.set && cmd.Args.ObjectID.set {
		return C.daos_obj_id_t{}, errors.New("can't specify --oid and positional argument")
	}
	if cmd.ObjectIDFlag.set {
		cmd.Args.ObjectID.set = true
		cmd.Args.ObjectID.oid = cmd.ObjectIDFlag.oid
	}
	if !cmd.Args.ObjectID.set {
		return C.daos_obj_id_t{}, &flags.Error{flags.ErrRequired, "OID is a required argument"}
	}

	return cmd.Args.ObjectID.oid, nil
}

type objQueryCmd struct {
	objBaseCmd
}

func (cmd *objQueryCmd) Execute(_ []string) error {
	oid, err := cmd.getOid()
	if err != nil {
		return err
	}

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndConnect(ap)
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
