//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"fmt"
	"os"

	"github.com/pkg/errors"
)

// StorCmd is the struct representing the top-level storage subcommand.
type StorCmd struct {
	List   ListStorCmd   `command:"list" alias:"l" description:"List SCM and NVMe storage attached to remote servers."`
	Format FormatStorCmd `command:"format" alias:"f" description:"Format SCM and NVMe storage attached to remote servers."`
}

// ListStorCmd is the struct representing the list storage subcommand.
type ListStorCmd struct{}

// run NVMe and SCM storage query on all connected servers
func listStor() {
	cCtrlrs, cModules := conns.ListStorage()

	fmt.Printf(
		unpackFormat(cCtrlrs),
		"NVMe SSD controller and constituent namespace")

	fmt.Printf(unpackFormat(cModules), "SCM module")
}

// Execute is run when ListStorCmd activates
func (s *ListStorCmd) Execute(args []string) error {
	if err := connectHosts(); err != nil {
		return errors.Wrap(err, "unable to connect to hosts")
	}

	listStor()

	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}

// FormatStorCmd is the struct representing the format storage subcommand.
type FormatStorCmd struct{}

// run NVMe and SCM storage format on all connected servers
func formatStor() {
	fmt.Println(
		"This is a destructive operation and storage devices " +
			"specified in the server config file will be erased.\n" +
			"Please be patient as it may take several minutes.\n" +
			"Are you sure you want to continue? (yes/no)")

	if getConsent() {
		fmt.Printf(
			unpackFormat(conns.FormatStorage()),
			"storage format result")
	}
}

// Execute is run when FormatStorCmd activates
func (s *FormatStorCmd) Execute(args []string) error {
	if err := connectHosts(); err != nil {
		return errors.Wrap(err, "unable to connect to hosts")
	}

	formatStor()

	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}

// todo: implement burn-in and firmware update subcommands

//func getUpdateParams(c *ishell.Context) (*pb.UpdateNvmeParams, error) {
//	// disable the '>>>' for cleaner same line input.
//	c.ShowPrompt(false)
//	defer c.ShowPrompt(true) // revert after user input.
//
//	c.Print("Please enter firmware image file-path: ")
//	path := c.ReadLine()
//
//	c.Print("Please enter slot you would like to update [default 0]: ")
//	slotRaw := c.ReadLine()
//
//	var slot int32
//
//	if slotRaw != "" {
//		slot, err := strconv.Atoi(slotRaw)
//		if err != nil {
//			return nil, fmt.Errorf("%s is not a number", slotRaw)
//		}
//
//		if slot >= 0 && slot < 7 {
//			return nil, fmt.Errorf("%d needs to be a number between 0 and 7", slot)
//		}
//	}
//
//	return &pb.UpdateNvmeParams{
//		Ctrlr: nil, Path: strings.TrimSpace(path), Slot: slot}, nil
//}

//func getFioConfig(c *ishell.Context) (configPath string, err error) {
//	// fetch existing configuration files
//	paths, err := mgmtClient.FetchFioConfigPaths()
//	if err != nil {
//		return
//	}
//	// cut prefix to display filenames not full path
//	configChoices := functional.Map(
//		paths, func(p string) string { return filepath.Base(p) })
//	// add custom path option
//	configChoices = append(configChoices, "custom path")
//
//	choiceIdx := c.MultiChoice(
//		configChoices,
//		"Select the fio configuration you would like to run on the selected controllers.")
//
//	// if custom path selected (last index), process input
//	if choiceIdx == len(configChoices)-1 {
//		// disable the '>>>' for cleaner same line input.
//		c.ShowPrompt(false)
//		defer c.ShowPrompt(true) // revert after user input.
//
//		c.Print("Please enter fio configuration file-path [has file extension .fio]: ")
//		path := c.ReadLine()
//
//		if path == "" {
//			return "", fmt.Errorf("no filepath provided")
//		}
//		if filepath.Ext(path) != ".fio" {
//			return "", fmt.Errorf("provided filepath does not end in .fio")
//		}
//		if !filepath.IsAbs(path) {
//			return "", fmt.Errorf("provided filepath is not absolute")
//		}
//
//		configPath = path
//	} else {
//		configPath = paths[choiceIdx]
//	}
//
//	return
//}

//func nvmeTaskLookup(
//	c *ishell.Context, ctrlrs []*pb.NvmeController, feature string) error {
//
//	switch feature {
//	case "nvme-fw-update":
//		params, err := getUpdateParams(c)
//		if err != nil {
//			c.Println("Problem reading user inputs: ", err)
//			return err
//		}
//
//		for _, ctrlr := range ctrlrs {
//			c.Printf("\nController: %+v\n", ctrlr)
//			c.Printf(
//				"\t- Updating firmware on slot %d with image %s.\n",
//				params.Slot, params.Path)
//
//			params.Ctrlr = ctrlr
//
//			newFwrev, err := mgmtClient.UpdateNvmeCtrlr(params)
//			if err != nil {
//				c.Println("\nProblem updating firmware: ", err)
//				return err
//			}
//			c.Printf(
//				"\nSuccessfully updated firmware from revision %s to %s!\n",
//				params.Ctrlr.Fwrev, newFwrev)
//		}
//	case "nvme-burn-in":
//		configPath, err := getFioConfig(c)
//		if err != nil {
//			c.Println("Problem reading user inputs: ", err)
//			return err
//		}
//
//		for _, ctrlr := range ctrlrs {
//			c.Printf("\nController: %+v\n", ctrlr)
//			c.Printf(
//				"\t- Running burn-in validation with spdk fio plugin using job file %s.\n\n",
//				filepath.Base(configPath))
//			_, err := mgmtClient.BurnInNvme(ctrlr.Id, configPath)
//			if err != nil {
//				return err
//			}
//		}
//	default:
//		c.Printf("Sorry, task '%s' has not been implemented.\n", feature)
//	}
//
//	return nil
//}
