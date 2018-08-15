package mgmt_test

import (
	"fmt"
	"reflect"
	"testing"

	. "go-spdk/nvme"
	. "modules/mgmt"

	pb "modules/mgmt/proto"
)

func mockController() Controller {
	return Controller{
		ID:      int32(12345),
		Model:   "ABC",
		Serial:  "123ABC",
		PCIAddr: "1:2:3.0",
		FWRev:   "1.0.0",
	}
}
func mockNamespace(ctrlr *Controller) Namespace {
	return Namespace{
		ID:      int32(54321),
		Size:    int32(99999),
		CtrlrID: int32(12345),
	}
}

// MockNvme struct implements Nvme interface
type mockNvme struct{}

func (mock *mockNvme) SpdkInit() error {
	return nil
}
func (mock *mockNvme) Discover() ([]Controller, []Namespace, error) {
	c := mockController()
	return []Controller{c}, []Namespace{mockNamespace(&c)}, nil
}

func NewTestControlServer() *ControlService {
	return &ControlService{Nvme: &mockNvme{}}
}

func assertEqual(
	t *testing.T, a interface{}, b interface{}, message string) {

	// reflect.DeepEqual() may not be suitable for nontrivial
	// struct element comparisons, go-cmp should then be used
	// but will introduce a third party dep.
	if reflect.DeepEqual(a, b) {
		return
	}
	if len(message) == 0 {
		message = fmt.Sprintf("%v != %v", a, b)
	}
	t.Fatal(message)
}

func TestListNVMe(t *testing.T) {
	s := NewTestControlServer()

	cs, nss, _ := s.Nvme.Discover()

	nvmeControllers := LoadControllers(cs)
	nvmeNamespaces := LoadNamespaces(nvmeControllers, nss)

	// expected results
	c := &pb.NVMeController{
		Id:      12345,
		Model:   "ABC",
		Serial:  "123ABC",
		Pciaddr: "1:2:3.0",
		Fwrev:   "1.0.0",
	}
	ns := &pb.NVMeNamespace{
		Controller: c, Id: 54321, Capacity: 99999}

	assertEqual(t, nvmeControllers[0], c, "")
	assertEqual(t, nvmeNamespaces[0], ns, "")
}
