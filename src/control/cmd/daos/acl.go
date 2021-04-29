//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -ldaos_cmd_hdlrs -ldfs -lduns

#include <stdlib.h>
#include "daos.h"
#include "daos_hdlr.h"
*/
import "C"

type containerOverwriteACLCmd struct {
	existingContainerCmd
}

func (cmd *containerOverwriteACLCmd) Execute(args []string) error {
	return nil
}

type containerUpdateACLCmd struct {
	existingContainerCmd
}

func (cmd *containerUpdateACLCmd) Execute(args []string) error {
	return nil
}

type containerDeleteACLCmd struct {
	existingContainerCmd
}

func (cmd *containerDeleteACLCmd) Execute(args []string) error {
	return nil
}

type containerGetACLCmd struct {
	existingContainerCmd
}

func (cmd *containerGetACLCmd) Execute(args []string) error {
	return nil
}

type containerSetOwnerCmd struct {
	existingContainerCmd
}

func (cmd *containerSetOwnerCmd) Execute(args []string) error {
	return nil
}
