//
// (C) Copyright 2020 Intel Corporation.
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

package system

import (
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"math"
	"sort"
	"strings"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	// FaultDomainSeparator is the dividing character for different levels of a
	// fault domain.
	FaultDomainSeparator = "/"

	// FaultDomainNilStr is the string value of a nil FaultDomain.
	FaultDomainNilStr = "(nil)"
)

// FaultDomain represents a multi-layer fault domain.
type FaultDomain struct {
	Domains []string // Hierarchical sequence of fault domain levels
}

func (f *FaultDomain) String() string {
	if f == nil {
		return FaultDomainNilStr
	}
	if f.Empty() {
		return FaultDomainSeparator
	}
	return FaultDomainSeparator + strings.Join(f.Domains, FaultDomainSeparator)
}

// Equals checks if the fault domains are equal.
func (f *FaultDomain) Equals(other *FaultDomain) bool {
	return f.String() == other.String()
}

// NumLevels gets the number of levels in the domain.
func (f *FaultDomain) NumLevels() int {
	if f == nil {
		return 0
	}
	return len(f.Domains)
}

// Empty checks whether the fault domain is empty or not.
func (f *FaultDomain) Empty() bool {
	return f.NumLevels() == 0
}

// Level returns the descriptor for the requested level of the fault domain.
// Level 0 is the bottom level. Level (n - 1) is the top level.
func (f *FaultDomain) Level(level int) (string, error) {
	if level < 0 || level >= f.NumLevels() {
		return "", errors.New("out of range")
	}
	return f.Domains[f.topLevelIdx()-level], nil
}

func (f *FaultDomain) topLevelIdx() int {
	return f.NumLevels() - 1
}

// BottomLevel returns the descriptor for the lowest level of this fault domain.
func (f *FaultDomain) BottomLevel() string {
	bottom, _ := f.Level(0)
	return bottom
}

// TopLevel returns the descriptor for the highest level of this fault domain
// below the root.
func (f *FaultDomain) TopLevel() string {
	top, _ := f.Level(f.topLevelIdx())
	return top
}

// IsAncestorOf determines if this fault domain is an ancestor of the one passed in.
// This includes a 0th-degree ancestor (i.e. it is identical).
func (f *FaultDomain) IsAncestorOf(d *FaultDomain) bool {
	if f.Empty() {
		// root is the ancestor of all
		return true
	}
	if f.NumLevels() > d.NumLevels() {
		// f can't be an ancestor - d is a higher-level domain
		return false
	}
	if f.Equals(d) {
		return true
	}
	for i, domain1 := range f.Domains {
		domain2 := d.Domains[i]
		if domain1 != domain2 {
			return false
		}
	}
	return true
}

// NewChild creates a FaultDomain with a level below this one.
func (f *FaultDomain) NewChild(childLevel string) (*FaultDomain, error) {
	if f == nil {
		return NewFaultDomain(childLevel)
	}
	childDomains := append(f.Domains, childLevel)
	return NewFaultDomain(childDomains...)
}

// MustCreateChild creates a child fault domain. If that is not possible, it
// panics.
func (f *FaultDomain) MustCreateChild(childLevel string) *FaultDomain {
	child, err := f.NewChild(childLevel)
	if err != nil {
		panic(err)
	}
	return child
}

// Uint64 transforms the Fault Domain into an identifying 64-bit integer.
func (f *FaultDomain) Uint64() uint64 {
	hasher := fnv.New64a()
	hasher.Write([]byte(f.String()))
	return hasher.Sum64()
}

// Uint32 transforms the Fault Domain into an identifying 32-bit integer.
func (f *FaultDomain) Uint32() uint32 {
	hasher := fnv.New32a()
	hasher.Write([]byte(f.String()))
	return hasher.Sum32()
}

// NewFaultDomain creates a FaultDomain from a sequence of strings representing
// individual levels of the domain.
// For each level of the domain, we assume case insensitivity and trim
// leading/trailing whitespace.
func NewFaultDomain(domains ...string) (*FaultDomain, error) {
	for i := range domains {
		domains[i] = strings.TrimSpace(domains[i])
		if domains[i] == "" {
			return nil, errors.New("invalid fault domain")
		}
		domains[i] = strings.ToLower(domains[i])
	}

	return &FaultDomain{
		Domains: domains,
	}, nil
}

// MustCreateFaultDomain creates a FaultDomain from a sequence of strings
// representing individual levels of the domain. If it is not possible to
// create a valid FaultDomain, it panics.
func MustCreateFaultDomain(domains ...string) *FaultDomain {
	fd, err := NewFaultDomain(domains...)
	if err != nil {
		panic(err)
	}
	return fd
}

// NewFaultDomainFromString creates a FaultDomain from a string in the fault
// domain path format (example: /rack0/pdu1/hostname).
// If the string isn't formatted correctly, returns an error.
func NewFaultDomainFromString(domainStr string) (*FaultDomain, error) {
	domainStr = strings.TrimSpace(domainStr)
	if len(domainStr) == 0 || domainStr == FaultDomainSeparator {
		return &FaultDomain{}, nil
	}

	if domainStr == FaultDomainNilStr {
		return nil, nil
	}

	if !strings.HasPrefix(domainStr, FaultDomainSeparator) {
		return nil, errors.New("invalid fault domain")
	}

	domains := strings.Split(domainStr, FaultDomainSeparator)
	domains = domains[1:]

	return NewFaultDomain(domains...)
}

// MustCreateFaultDomainFromString creates a FaultDomain from a string in the
// fault domain path format. If it is not possible to create a valid
// FaultDomain, it panics.
func MustCreateFaultDomainFromString(domainStr string) *FaultDomain {
	fd, err := NewFaultDomainFromString(domainStr)
	if err != nil {
		panic(err)
	}
	return fd
}

type (
	// FaultDomainTree is a node in a tree of FaultDomain objects.
	FaultDomainTree struct {
		Domain   *FaultDomain
		Children []*FaultDomainTree
	}
)

// WithNodeDomain changes the domain of the FaultDomainTree node.
func (t *FaultDomainTree) WithNodeDomain(domain *FaultDomain) *FaultDomainTree {
	if t == nil {
		t = NewFaultDomainTree()
	}
	t.Domain = domain
	return t
}

// AddDomain adds a child fault domain, including intermediate nodes, to the
// fault domain tree.
func (t *FaultDomainTree) AddDomain(domain *FaultDomain) error {
	if t == nil {
		return errors.New("can't add to nil FaultDomainTree")
	}

	if domain.Empty() {
		return errors.New("can't add empty fault domain to tree")
	}

	domainAsTree := NewFaultDomainTree(domain)

	return t.Merge(domainAsTree)
}

// Merge merges another FaultDomainTree into this one.
func (t *FaultDomainTree) Merge(t2 *FaultDomainTree) error {
	if t == nil {
		return errors.New("can't merge into nil FaultDomainTree")
	}

	if t2 == nil {
		return nil // nothing to do
	}

	// To merge, tree domains must match at the top.
	if !t.Domain.Equals(t2.Domain) {
		return errors.New("trees cannot be merged")
	}

	t.mergeTree(t2)
	return nil
}

func (t *FaultDomainTree) mergeTree(toBeMerged *FaultDomainTree) {
	for _, m := range toBeMerged.Children {
		foundBranch := false
		for _, p := range t.Children {
			if p.Domain.Equals(m.Domain) {
				foundBranch = true
				p.mergeTree(m)
				break
			}
		}
		if !foundBranch {
			t.Children = append(t.Children, m)
			sort.Slice(t.Children, func(i, j int) bool {
				return t.Children[i].Domain.BottomLevel() < t.Children[j].Domain.BottomLevel()
			})
		}
	}

	return
}

// RemoveDomain removes a given fault domain from the tree.
func (t *FaultDomainTree) RemoveDomain(domain *FaultDomain) error {
	if t == nil {
		return errors.New("nil FaultDomainTree")
	}
	if domain == nil {
		return nil // nothing to remove
	}
	if domain.Equals(t.Domain) {
		return errors.New("cannot remove root fault domain from tree")
	}
	for i := len(t.Children) - 1; i >= 0; i-- {
		child := t.Children[i]
		if child.Domain.Equals(domain) {
			// found it
			t.Children = append(t.Children[:i], t.Children[i+1:]...)
			return nil
		}
		if child.Domain.IsAncestorOf(domain) {
			return child.RemoveDomain(domain)
		}
	}
	return nil // not found - consider it already removed
}

// IsRoot verifies if the FaultDomainTree is a root node.
func (t *FaultDomainTree) IsRoot() bool {
	if t == nil {
		return false
	}
	if t.Domain.Empty() {
		return true
	}
	return false
}

// IsLeaf verifies if the FaultDomainTree is a leaf node.
func (t *FaultDomainTree) IsLeaf() bool {
	if t == nil {
		return false
	}
	if len(t.Children) == 0 {
		return true
	}
	return false
}

// IsBalanced verifies that all the branches of the tree are the same depth.
func (t *FaultDomainTree) IsBalanced() bool {
	if t == nil {
		return true
	}
	maxDepth := t.getMaxDepth()
	minDepth := t.getMinDepth()
	return maxDepth == minDepth
}

func (t *FaultDomainTree) getMaxDepth() int {
	maxDepth := 0
	for _, c := range t.Children {
		childMax := c.getMaxDepth() + 1
		if childMax > maxDepth {
			maxDepth = childMax
		}
	}
	return maxDepth
}

func (t *FaultDomainTree) getMinDepth() int {
	if len(t.Children) == 0 {
		return 0
	}
	minDepth := math.MaxInt32
	for _, c := range t.Children {
		childMin := c.getMinDepth() + 1
		if childMin < minDepth {
			minDepth = childMin
		}
	}
	return minDepth
}

func (t *FaultDomainTree) String() string {
	if t == nil {
		return "(nil)"
	}

	var b strings.Builder
	b.WriteString("FaultDomainTree:\n")
	t.nodeToString(&b, 0, true)
	return b.String()
}

func (t *FaultDomainTree) nodeToString(w io.Writer, depth int, fullDomain bool) {
	nodeStr := t.Domain.BottomLevel()
	if fullDomain {
		nodeStr = t.Domain.String()
	}
	fmt.Fprintf(w, "%s- %s\n", strings.Repeat("  ", depth), nodeStr)
	for _, c := range t.Children {
		c.nodeToString(w, depth+1, false)
	}
}

// ToProto converts the FaultDomainTree into a list of protobuf structures,
// in the order of a breadth-first traversal.
func (t *FaultDomainTree) ToProto() []*mgmtpb.FaultDomain {
	if t == nil {
		return nil
	}

	result := make([]*mgmtpb.FaultDomain, 0)

	queue := make([]*FaultDomainTree, 0)
	queue = append(queue, t)

	for len(queue) > 0 {
		cur := queue[0]
		queue = queue[1:]

		result = append(result, cur.toProtoSingle())
		for _, child := range cur.Children {
			queue = append(queue, child)
		}
	}
	return result
}

func (t *FaultDomainTree) toProtoSingle() *mgmtpb.FaultDomain {
	result := &mgmtpb.FaultDomain{
		Domain: t.Domain.String(),
		Id:     t.Domain.Uint32(),
	}
	for _, child := range t.Children {
		result.Children = append(result.Children, child.Domain.Uint32())
	}
	return result
}

// NewFaultDomainTree creates a FaultDomainTree including all the
// passed-in fault domains.
func NewFaultDomainTree(domains ...*FaultDomain) *FaultDomainTree {
	tree := &FaultDomainTree{
		Domain:   MustCreateFaultDomain(), // Empty fault domain will not fail
		Children: make([]*FaultDomainTree, 0),
	}
	for _, d := range domains {
		subtree := faultDomainTreeFromDomain(d)
		tree.mergeTree(subtree)
	}
	return tree
}

func faultDomainTreeFromDomain(d *FaultDomain) *FaultDomainTree {
	tree := NewFaultDomainTree()
	if !d.Empty() {
		node := tree
		for i := 0; i < d.NumLevels(); i++ {
			childDomain := MustCreateFaultDomain(d.Domains[:i+1]...)
			child := NewFaultDomainTree().WithNodeDomain(childDomain)
			node.Children = append(node.Children, child)
			node = child
		}
	}
	return tree
}
