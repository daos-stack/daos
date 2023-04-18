//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/pkg/errors"
)

// AccessControlList is a structure for the access control list.
type AccessControlList struct {
	Entries    []string `json:"ACL"`       // Access Control Entries in short string format
	Owner      string   `json:"OwnerUser"` // User that owns the resource
	OwnerGroup string   // Group that owns the resource
}

func (acl *AccessControlList) MarshalJSON() ([]byte, error) {
	return json.Marshal(acl.Entries)
}

// Empty checks whether there are any entries in the AccessControlList
func (acl *AccessControlList) Empty() bool {
	if acl == nil || len(acl.Entries) == 0 {
		return true
	}
	return false
}

// HasOwner checks whether the AccessControlList has an owner user.
func (acl *AccessControlList) HasOwner() bool {
	if acl == nil {
		return false
	}

	if acl.Owner != "" {
		return true
	}
	return false
}

// HasOwnerGroup checks whether the AccessControlList has an owner group.
func (acl *AccessControlList) HasOwnerGroup() bool {
	if acl == nil {
		return false
	}

	if acl.OwnerGroup != "" {
		return true
	}
	return false
}

// String displays the AccessControlList in a basic string format.
func (acl *AccessControlList) String() string {
	if acl == nil {
		return "nil"
	}
	return fmt.Sprintf("%+v", *acl)
}

// ReadACLFile reads in a file representing an ACL, and translates it into an
// AccessControlList structure
func ReadACLFile(aclFile string) (*AccessControlList, error) {
	file, err := os.Open(aclFile)
	if err != nil {
		return nil, errors.WithMessage(err, "opening ACL file")
	}
	defer file.Close()

	acl, err := ParseACL(file)
	if err != nil {
		return nil, err
	}
	if acl.Empty() {
		return nil, errors.New(fmt.Sprintf("ACL file '%s' contains no entries", aclFile))
	}

	return acl, nil
}

// isACLFileComment checks whether the line is formatted as a comment for an
// ACL file.
func isACLFileComment(line string) bool {
	return strings.HasPrefix(line, "#")
}

// ParseACL reads the content from io.Reader and puts the results into a
// AccessControlList structure.
// Assumes that ACE strings are provided one per line.
func ParseACL(reader io.Reader) (*AccessControlList, error) {
	aceList := make([]string, 0)
	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return nil, errors.WithMessage(err, "reading ACL file")
		}

		line := strings.TrimSpace(scanner.Text())
		if line != "" && !isACLFileComment(line) {
			aceList = append(aceList, line)
		}
	}

	return &AccessControlList{Entries: aceList}, nil
}

// FormatACL converts the AccessControlList to a human-readable string.
func FormatACL(acl *AccessControlList, verbose bool) string {
	var builder strings.Builder

	if acl.HasOwner() {
		fmt.Fprintf(&builder, "# Owner: %s\n", acl.Owner)
	}

	if acl.HasOwnerGroup() {
		fmt.Fprintf(&builder, "# Owner Group: %s\n", acl.OwnerGroup)
	}

	builder.WriteString("# Entries:\n")
	if acl.Empty() {
		builder.WriteString("#   None\n")
		return builder.String()
	}

	for _, ace := range acl.Entries {
		if verbose {
			fmt.Fprintf(&builder, "# %s\n", getVerboseACE(ace))
		}
		fmt.Fprintf(&builder, "%s\n", ace)
	}

	return builder.String()
}

// FormatACLDefault formats the AccessControlList in non-verbose mode.
func FormatACLDefault(acl *AccessControlList) string {
	return FormatACL(acl, false)
}

func getVerboseACE(shortACE string) string {
	if shortACE == "" {
		return ""
	}

	const (
		// Field indices
		ACETypes = iota
		ACEFlags
		ACEIdentity
		ACEPerms
		ACENumFields // Must be last
	)

	fields := strings.Split(shortACE, ":")
	if len(fields) != ACENumFields {
		return "invalid ACE"
	}

	var b strings.Builder

	fmt.Fprintf(&b, "%s:", getVerboseType(fields[ACETypes]))

	if fields[ACEFlags] != "" {
		b.WriteString(getVerboseFlags(fields[ACEFlags]))
	}
	b.WriteString(":")

	fmt.Fprintf(&b, "%s:", getVerboseIdentity(fields[ACEIdentity]))

	b.WriteString(getVerbosePermissions(fields[ACEPerms]))

	return b.String()
}

func getVerboseType(field string) string {
	types := map[string]string{
		"A": "Allow",
	}

	return getVerboseField(field, types)
}

func getVerboseFlags(field string) string {
	flags := map[string]string{
		"G": "Group",
	}

	return getVerboseField(field, flags)
}

func getVerboseIdentity(field string) string {
	specialIDs := map[string]string{
		"OWNER@":    "Owner",
		"GROUP@":    "Owner-Group",
		"EVERYONE@": "Everyone",
	}

	identity := field
	if identity == "" {
		identity = "None"
	} else if v, ok := specialIDs[identity]; ok {
		identity = v
	}

	return identity
}

func getVerbosePermissions(field string) string {
	perms := map[string]string{
		"r": "Read",
		"w": "Write",
	}

	return getVerboseField(field, perms)
}

func getVerboseField(field string, verbose map[string]string) string {
	if field == "" {
		return "None"
	}

	var b strings.Builder
	for i, c := range field {
		if i != 0 {
			b.WriteString("/")
		}
		var verboseVal string
		if v, ok := verbose[string(c)]; ok {
			verboseVal = v
		} else {
			verboseVal = "Unknown"
		}
		b.WriteString(verboseVal)
	}

	return b.String()
}
