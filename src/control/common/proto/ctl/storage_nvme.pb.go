// Code generated by protoc-gen-go. DO NOT EDIT.
// source: storage_nvme.proto

package ctl

import (
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
	math "math"
)

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion3 // please upgrade the proto package

// NvmeController represents an NVMe Controller (SSD).
type NvmeController struct {
	Model                string                      `protobuf:"bytes,1,opt,name=model,proto3" json:"model,omitempty"`
	Serial               string                      `protobuf:"bytes,2,opt,name=serial,proto3" json:"serial,omitempty"`
	Pciaddr              string                      `protobuf:"bytes,3,opt,name=pciaddr,proto3" json:"pciaddr,omitempty"`
	Fwrev                string                      `protobuf:"bytes,4,opt,name=fwrev,proto3" json:"fwrev,omitempty"`
	Socketid             int32                       `protobuf:"varint,5,opt,name=socketid,proto3" json:"socketid,omitempty"`
	Healthstats          *NvmeController_Health      `protobuf:"bytes,6,opt,name=healthstats,proto3" json:"healthstats,omitempty"`
	Namespaces           []*NvmeController_Namespace `protobuf:"bytes,7,rep,name=namespaces,proto3" json:"namespaces,omitempty"`
	Smddevices           []*NvmeController_SmdDevice `protobuf:"bytes,8,rep,name=smddevices,proto3" json:"smddevices,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                    `json:"-"`
	XXX_unrecognized     []byte                      `json:"-"`
	XXX_sizecache        int32                       `json:"-"`
}

func (m *NvmeController) Reset()         { *m = NvmeController{} }
func (m *NvmeController) String() string { return proto.CompactTextString(m) }
func (*NvmeController) ProtoMessage()    {}
func (*NvmeController) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{0}
}

func (m *NvmeController) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NvmeController.Unmarshal(m, b)
}
func (m *NvmeController) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NvmeController.Marshal(b, m, deterministic)
}
func (m *NvmeController) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NvmeController.Merge(m, src)
}
func (m *NvmeController) XXX_Size() int {
	return xxx_messageInfo_NvmeController.Size(m)
}
func (m *NvmeController) XXX_DiscardUnknown() {
	xxx_messageInfo_NvmeController.DiscardUnknown(m)
}

var xxx_messageInfo_NvmeController proto.InternalMessageInfo

func (m *NvmeController) GetModel() string {
	if m != nil {
		return m.Model
	}
	return ""
}

func (m *NvmeController) GetSerial() string {
	if m != nil {
		return m.Serial
	}
	return ""
}

func (m *NvmeController) GetPciaddr() string {
	if m != nil {
		return m.Pciaddr
	}
	return ""
}

func (m *NvmeController) GetFwrev() string {
	if m != nil {
		return m.Fwrev
	}
	return ""
}

func (m *NvmeController) GetSocketid() int32 {
	if m != nil {
		return m.Socketid
	}
	return 0
}

func (m *NvmeController) GetHealthstats() *NvmeController_Health {
	if m != nil {
		return m.Healthstats
	}
	return nil
}

func (m *NvmeController) GetNamespaces() []*NvmeController_Namespace {
	if m != nil {
		return m.Namespaces
	}
	return nil
}

func (m *NvmeController) GetSmddevices() []*NvmeController_SmdDevice {
	if m != nil {
		return m.Smddevices
	}
	return nil
}

// Health mirrors bio_dev_state structure.
type NvmeController_Health struct {
	Timestamp uint64 `protobuf:"varint,1,opt,name=timestamp,proto3" json:"timestamp,omitempty"`
	// Device health details
	WarnTempTime    uint32 `protobuf:"varint,3,opt,name=warn_temp_time,json=warnTempTime,proto3" json:"warn_temp_time,omitempty"`
	CritTempTime    uint32 `protobuf:"varint,4,opt,name=crit_temp_time,json=critTempTime,proto3" json:"crit_temp_time,omitempty"`
	CtrlBusyTime    uint64 `protobuf:"varint,5,opt,name=ctrl_busy_time,json=ctrlBusyTime,proto3" json:"ctrl_busy_time,omitempty"`
	PowerCycles     uint64 `protobuf:"varint,6,opt,name=power_cycles,json=powerCycles,proto3" json:"power_cycles,omitempty"`
	PowerOnHours    uint64 `protobuf:"varint,7,opt,name=power_on_hours,json=powerOnHours,proto3" json:"power_on_hours,omitempty"`
	UnsafeShutdowns uint64 `protobuf:"varint,8,opt,name=unsafe_shutdowns,json=unsafeShutdowns,proto3" json:"unsafe_shutdowns,omitempty"`
	MediaErrs       uint64 `protobuf:"varint,9,opt,name=media_errs,json=mediaErrs,proto3" json:"media_errs,omitempty"`
	ErrLogEntries   uint64 `protobuf:"varint,10,opt,name=err_log_entries,json=errLogEntries,proto3" json:"err_log_entries,omitempty"`
	// I/O error counters
	BioReadErrs  uint32 `protobuf:"varint,11,opt,name=bio_read_errs,json=bioReadErrs,proto3" json:"bio_read_errs,omitempty"`
	BioWriteErrs uint32 `protobuf:"varint,12,opt,name=bio_write_errs,json=bioWriteErrs,proto3" json:"bio_write_errs,omitempty"`
	BioUnmapErrs uint32 `protobuf:"varint,13,opt,name=bio_unmap_errs,json=bioUnmapErrs,proto3" json:"bio_unmap_errs,omitempty"`
	ChecksumErrs uint32 `protobuf:"varint,14,opt,name=checksum_errs,json=checksumErrs,proto3" json:"checksum_errs,omitempty"`
	Temperature  uint32 `protobuf:"varint,15,opt,name=temperature,proto3" json:"temperature,omitempty"`
	// Critical warnings
	TempWarn             bool     `protobuf:"varint,16,opt,name=temp_warn,json=tempWarn,proto3" json:"temp_warn,omitempty"`
	AvailSpareWarn       bool     `protobuf:"varint,17,opt,name=avail_spare_warn,json=availSpareWarn,proto3" json:"avail_spare_warn,omitempty"`
	DevReliabilityWarn   bool     `protobuf:"varint,18,opt,name=dev_reliability_warn,json=devReliabilityWarn,proto3" json:"dev_reliability_warn,omitempty"`
	ReadOnlyWarn         bool     `protobuf:"varint,19,opt,name=read_only_warn,json=readOnlyWarn,proto3" json:"read_only_warn,omitempty"`
	VolatileMemWarn      bool     `protobuf:"varint,20,opt,name=volatile_mem_warn,json=volatileMemWarn,proto3" json:"volatile_mem_warn,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *NvmeController_Health) Reset()         { *m = NvmeController_Health{} }
func (m *NvmeController_Health) String() string { return proto.CompactTextString(m) }
func (*NvmeController_Health) ProtoMessage()    {}
func (*NvmeController_Health) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{0, 0}
}

func (m *NvmeController_Health) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NvmeController_Health.Unmarshal(m, b)
}
func (m *NvmeController_Health) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NvmeController_Health.Marshal(b, m, deterministic)
}
func (m *NvmeController_Health) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NvmeController_Health.Merge(m, src)
}
func (m *NvmeController_Health) XXX_Size() int {
	return xxx_messageInfo_NvmeController_Health.Size(m)
}
func (m *NvmeController_Health) XXX_DiscardUnknown() {
	xxx_messageInfo_NvmeController_Health.DiscardUnknown(m)
}

var xxx_messageInfo_NvmeController_Health proto.InternalMessageInfo

func (m *NvmeController_Health) GetTimestamp() uint64 {
	if m != nil {
		return m.Timestamp
	}
	return 0
}

func (m *NvmeController_Health) GetWarnTempTime() uint32 {
	if m != nil {
		return m.WarnTempTime
	}
	return 0
}

func (m *NvmeController_Health) GetCritTempTime() uint32 {
	if m != nil {
		return m.CritTempTime
	}
	return 0
}

func (m *NvmeController_Health) GetCtrlBusyTime() uint64 {
	if m != nil {
		return m.CtrlBusyTime
	}
	return 0
}

func (m *NvmeController_Health) GetPowerCycles() uint64 {
	if m != nil {
		return m.PowerCycles
	}
	return 0
}

func (m *NvmeController_Health) GetPowerOnHours() uint64 {
	if m != nil {
		return m.PowerOnHours
	}
	return 0
}

func (m *NvmeController_Health) GetUnsafeShutdowns() uint64 {
	if m != nil {
		return m.UnsafeShutdowns
	}
	return 0
}

func (m *NvmeController_Health) GetMediaErrs() uint64 {
	if m != nil {
		return m.MediaErrs
	}
	return 0
}

func (m *NvmeController_Health) GetErrLogEntries() uint64 {
	if m != nil {
		return m.ErrLogEntries
	}
	return 0
}

func (m *NvmeController_Health) GetBioReadErrs() uint32 {
	if m != nil {
		return m.BioReadErrs
	}
	return 0
}

func (m *NvmeController_Health) GetBioWriteErrs() uint32 {
	if m != nil {
		return m.BioWriteErrs
	}
	return 0
}

func (m *NvmeController_Health) GetBioUnmapErrs() uint32 {
	if m != nil {
		return m.BioUnmapErrs
	}
	return 0
}

func (m *NvmeController_Health) GetChecksumErrs() uint32 {
	if m != nil {
		return m.ChecksumErrs
	}
	return 0
}

func (m *NvmeController_Health) GetTemperature() uint32 {
	if m != nil {
		return m.Temperature
	}
	return 0
}

func (m *NvmeController_Health) GetTempWarn() bool {
	if m != nil {
		return m.TempWarn
	}
	return false
}

func (m *NvmeController_Health) GetAvailSpareWarn() bool {
	if m != nil {
		return m.AvailSpareWarn
	}
	return false
}

func (m *NvmeController_Health) GetDevReliabilityWarn() bool {
	if m != nil {
		return m.DevReliabilityWarn
	}
	return false
}

func (m *NvmeController_Health) GetReadOnlyWarn() bool {
	if m != nil {
		return m.ReadOnlyWarn
	}
	return false
}

func (m *NvmeController_Health) GetVolatileMemWarn() bool {
	if m != nil {
		return m.VolatileMemWarn
	}
	return false
}

// Namespace represents a namespace created on an NvmeController.
type NvmeController_Namespace struct {
	Id                   uint32   `protobuf:"varint,1,opt,name=id,proto3" json:"id,omitempty"`
	Size                 uint64   `protobuf:"varint,2,opt,name=size,proto3" json:"size,omitempty"`
	Ctrlrpciaddr         string   `protobuf:"bytes,3,opt,name=ctrlrpciaddr,proto3" json:"ctrlrpciaddr,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *NvmeController_Namespace) Reset()         { *m = NvmeController_Namespace{} }
func (m *NvmeController_Namespace) String() string { return proto.CompactTextString(m) }
func (*NvmeController_Namespace) ProtoMessage()    {}
func (*NvmeController_Namespace) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{0, 1}
}

func (m *NvmeController_Namespace) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NvmeController_Namespace.Unmarshal(m, b)
}
func (m *NvmeController_Namespace) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NvmeController_Namespace.Marshal(b, m, deterministic)
}
func (m *NvmeController_Namespace) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NvmeController_Namespace.Merge(m, src)
}
func (m *NvmeController_Namespace) XXX_Size() int {
	return xxx_messageInfo_NvmeController_Namespace.Size(m)
}
func (m *NvmeController_Namespace) XXX_DiscardUnknown() {
	xxx_messageInfo_NvmeController_Namespace.DiscardUnknown(m)
}

var xxx_messageInfo_NvmeController_Namespace proto.InternalMessageInfo

func (m *NvmeController_Namespace) GetId() uint32 {
	if m != nil {
		return m.Id
	}
	return 0
}

func (m *NvmeController_Namespace) GetSize() uint64 {
	if m != nil {
		return m.Size
	}
	return 0
}

func (m *NvmeController_Namespace) GetCtrlrpciaddr() string {
	if m != nil {
		return m.Ctrlrpciaddr
	}
	return ""
}

// SmdDevice represents a blobstore created on a NvmeController_Namespace.
// TODO: this should be embedded in Namespace above
type NvmeController_SmdDevice struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	TgtIds               []int32  `protobuf:"varint,2,rep,packed,name=tgt_ids,json=tgtIds,proto3" json:"tgt_ids,omitempty"`
	State                string   `protobuf:"bytes,3,opt,name=state,proto3" json:"state,omitempty"`
	Rank                 uint32   `protobuf:"varint,4,opt,name=rank,proto3" json:"rank,omitempty"`
	TotalBytes           uint64   `protobuf:"varint,5,opt,name=total_bytes,json=totalBytes,proto3" json:"total_bytes,omitempty"`
	AvailBytes           uint64   `protobuf:"varint,6,opt,name=avail_bytes,json=availBytes,proto3" json:"avail_bytes,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *NvmeController_SmdDevice) Reset()         { *m = NvmeController_SmdDevice{} }
func (m *NvmeController_SmdDevice) String() string { return proto.CompactTextString(m) }
func (*NvmeController_SmdDevice) ProtoMessage()    {}
func (*NvmeController_SmdDevice) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{0, 2}
}

func (m *NvmeController_SmdDevice) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NvmeController_SmdDevice.Unmarshal(m, b)
}
func (m *NvmeController_SmdDevice) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NvmeController_SmdDevice.Marshal(b, m, deterministic)
}
func (m *NvmeController_SmdDevice) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NvmeController_SmdDevice.Merge(m, src)
}
func (m *NvmeController_SmdDevice) XXX_Size() int {
	return xxx_messageInfo_NvmeController_SmdDevice.Size(m)
}
func (m *NvmeController_SmdDevice) XXX_DiscardUnknown() {
	xxx_messageInfo_NvmeController_SmdDevice.DiscardUnknown(m)
}

var xxx_messageInfo_NvmeController_SmdDevice proto.InternalMessageInfo

func (m *NvmeController_SmdDevice) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *NvmeController_SmdDevice) GetTgtIds() []int32 {
	if m != nil {
		return m.TgtIds
	}
	return nil
}

func (m *NvmeController_SmdDevice) GetState() string {
	if m != nil {
		return m.State
	}
	return ""
}

func (m *NvmeController_SmdDevice) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *NvmeController_SmdDevice) GetTotalBytes() uint64 {
	if m != nil {
		return m.TotalBytes
	}
	return 0
}

func (m *NvmeController_SmdDevice) GetAvailBytes() uint64 {
	if m != nil {
		return m.AvailBytes
	}
	return 0
}

// NvmeControllerResult represents state of operation performed on controller.
type NvmeControllerResult struct {
	Pciaddr              string         `protobuf:"bytes,1,opt,name=pciaddr,proto3" json:"pciaddr,omitempty"`
	State                *ResponseState `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *NvmeControllerResult) Reset()         { *m = NvmeControllerResult{} }
func (m *NvmeControllerResult) String() string { return proto.CompactTextString(m) }
func (*NvmeControllerResult) ProtoMessage()    {}
func (*NvmeControllerResult) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{1}
}

func (m *NvmeControllerResult) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NvmeControllerResult.Unmarshal(m, b)
}
func (m *NvmeControllerResult) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NvmeControllerResult.Marshal(b, m, deterministic)
}
func (m *NvmeControllerResult) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NvmeControllerResult.Merge(m, src)
}
func (m *NvmeControllerResult) XXX_Size() int {
	return xxx_messageInfo_NvmeControllerResult.Size(m)
}
func (m *NvmeControllerResult) XXX_DiscardUnknown() {
	xxx_messageInfo_NvmeControllerResult.DiscardUnknown(m)
}

var xxx_messageInfo_NvmeControllerResult proto.InternalMessageInfo

func (m *NvmeControllerResult) GetPciaddr() string {
	if m != nil {
		return m.Pciaddr
	}
	return ""
}

func (m *NvmeControllerResult) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

type PrepareNvmeReq struct {
	Pciwhitelist         string   `protobuf:"bytes,1,opt,name=pciwhitelist,proto3" json:"pciwhitelist,omitempty"`
	Nrhugepages          int32    `protobuf:"varint,2,opt,name=nrhugepages,proto3" json:"nrhugepages,omitempty"`
	Targetuser           string   `protobuf:"bytes,3,opt,name=targetuser,proto3" json:"targetuser,omitempty"`
	Reset_               bool     `protobuf:"varint,4,opt,name=reset,proto3" json:"reset,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PrepareNvmeReq) Reset()         { *m = PrepareNvmeReq{} }
func (m *PrepareNvmeReq) String() string { return proto.CompactTextString(m) }
func (*PrepareNvmeReq) ProtoMessage()    {}
func (*PrepareNvmeReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{2}
}

func (m *PrepareNvmeReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareNvmeReq.Unmarshal(m, b)
}
func (m *PrepareNvmeReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareNvmeReq.Marshal(b, m, deterministic)
}
func (m *PrepareNvmeReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareNvmeReq.Merge(m, src)
}
func (m *PrepareNvmeReq) XXX_Size() int {
	return xxx_messageInfo_PrepareNvmeReq.Size(m)
}
func (m *PrepareNvmeReq) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepareNvmeReq.DiscardUnknown(m)
}

var xxx_messageInfo_PrepareNvmeReq proto.InternalMessageInfo

func (m *PrepareNvmeReq) GetPciwhitelist() string {
	if m != nil {
		return m.Pciwhitelist
	}
	return ""
}

func (m *PrepareNvmeReq) GetNrhugepages() int32 {
	if m != nil {
		return m.Nrhugepages
	}
	return 0
}

func (m *PrepareNvmeReq) GetTargetuser() string {
	if m != nil {
		return m.Targetuser
	}
	return ""
}

func (m *PrepareNvmeReq) GetReset_() bool {
	if m != nil {
		return m.Reset_
	}
	return false
}

type PrepareNvmeResp struct {
	State                *ResponseState `protobuf:"bytes,1,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *PrepareNvmeResp) Reset()         { *m = PrepareNvmeResp{} }
func (m *PrepareNvmeResp) String() string { return proto.CompactTextString(m) }
func (*PrepareNvmeResp) ProtoMessage()    {}
func (*PrepareNvmeResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{3}
}

func (m *PrepareNvmeResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareNvmeResp.Unmarshal(m, b)
}
func (m *PrepareNvmeResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareNvmeResp.Marshal(b, m, deterministic)
}
func (m *PrepareNvmeResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareNvmeResp.Merge(m, src)
}
func (m *PrepareNvmeResp) XXX_Size() int {
	return xxx_messageInfo_PrepareNvmeResp.Size(m)
}
func (m *PrepareNvmeResp) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepareNvmeResp.DiscardUnknown(m)
}

var xxx_messageInfo_PrepareNvmeResp proto.InternalMessageInfo

func (m *PrepareNvmeResp) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

type ScanNvmeReq struct {
	Health               bool     `protobuf:"varint,1,opt,name=Health,proto3" json:"Health,omitempty"`
	Meta                 bool     `protobuf:"varint,2,opt,name=Meta,proto3" json:"Meta,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScanNvmeReq) Reset()         { *m = ScanNvmeReq{} }
func (m *ScanNvmeReq) String() string { return proto.CompactTextString(m) }
func (*ScanNvmeReq) ProtoMessage()    {}
func (*ScanNvmeReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{4}
}

func (m *ScanNvmeReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanNvmeReq.Unmarshal(m, b)
}
func (m *ScanNvmeReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanNvmeReq.Marshal(b, m, deterministic)
}
func (m *ScanNvmeReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanNvmeReq.Merge(m, src)
}
func (m *ScanNvmeReq) XXX_Size() int {
	return xxx_messageInfo_ScanNvmeReq.Size(m)
}
func (m *ScanNvmeReq) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanNvmeReq.DiscardUnknown(m)
}

var xxx_messageInfo_ScanNvmeReq proto.InternalMessageInfo

func (m *ScanNvmeReq) GetHealth() bool {
	if m != nil {
		return m.Health
	}
	return false
}

func (m *ScanNvmeReq) GetMeta() bool {
	if m != nil {
		return m.Meta
	}
	return false
}

type ScanNvmeResp struct {
	Ctrlrs               []*NvmeController `protobuf:"bytes,1,rep,name=ctrlrs,proto3" json:"ctrlrs,omitempty"`
	State                *ResponseState    `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}          `json:"-"`
	XXX_unrecognized     []byte            `json:"-"`
	XXX_sizecache        int32             `json:"-"`
}

func (m *ScanNvmeResp) Reset()         { *m = ScanNvmeResp{} }
func (m *ScanNvmeResp) String() string { return proto.CompactTextString(m) }
func (*ScanNvmeResp) ProtoMessage()    {}
func (*ScanNvmeResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{5}
}

func (m *ScanNvmeResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanNvmeResp.Unmarshal(m, b)
}
func (m *ScanNvmeResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanNvmeResp.Marshal(b, m, deterministic)
}
func (m *ScanNvmeResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanNvmeResp.Merge(m, src)
}
func (m *ScanNvmeResp) XXX_Size() int {
	return xxx_messageInfo_ScanNvmeResp.Size(m)
}
func (m *ScanNvmeResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanNvmeResp.DiscardUnknown(m)
}

var xxx_messageInfo_ScanNvmeResp proto.InternalMessageInfo

func (m *ScanNvmeResp) GetCtrlrs() []*NvmeController {
	if m != nil {
		return m.Ctrlrs
	}
	return nil
}

func (m *ScanNvmeResp) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

type FormatNvmeReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *FormatNvmeReq) Reset()         { *m = FormatNvmeReq{} }
func (m *FormatNvmeReq) String() string { return proto.CompactTextString(m) }
func (*FormatNvmeReq) ProtoMessage()    {}
func (*FormatNvmeReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_b4b1a62bc89112d2, []int{6}
}

func (m *FormatNvmeReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FormatNvmeReq.Unmarshal(m, b)
}
func (m *FormatNvmeReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FormatNvmeReq.Marshal(b, m, deterministic)
}
func (m *FormatNvmeReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FormatNvmeReq.Merge(m, src)
}
func (m *FormatNvmeReq) XXX_Size() int {
	return xxx_messageInfo_FormatNvmeReq.Size(m)
}
func (m *FormatNvmeReq) XXX_DiscardUnknown() {
	xxx_messageInfo_FormatNvmeReq.DiscardUnknown(m)
}

var xxx_messageInfo_FormatNvmeReq proto.InternalMessageInfo

func init() {
	proto.RegisterType((*NvmeController)(nil), "ctl.NvmeController")
	proto.RegisterType((*NvmeController_Health)(nil), "ctl.NvmeController.Health")
	proto.RegisterType((*NvmeController_Namespace)(nil), "ctl.NvmeController.Namespace")
	proto.RegisterType((*NvmeController_SmdDevice)(nil), "ctl.NvmeController.SmdDevice")
	proto.RegisterType((*NvmeControllerResult)(nil), "ctl.NvmeControllerResult")
	proto.RegisterType((*PrepareNvmeReq)(nil), "ctl.PrepareNvmeReq")
	proto.RegisterType((*PrepareNvmeResp)(nil), "ctl.PrepareNvmeResp")
	proto.RegisterType((*ScanNvmeReq)(nil), "ctl.ScanNvmeReq")
	proto.RegisterType((*ScanNvmeResp)(nil), "ctl.ScanNvmeResp")
	proto.RegisterType((*FormatNvmeReq)(nil), "ctl.FormatNvmeReq")
}

func init() {
	proto.RegisterFile("storage_nvme.proto", fileDescriptor_b4b1a62bc89112d2)
}

var fileDescriptor_b4b1a62bc89112d2 = []byte{
	// 909 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x95, 0x5f, 0x8f, 0x1b, 0x35,
	0x10, 0xc0, 0xb5, 0xb9, 0x24, 0x97, 0x4c, 0xfe, 0x5d, 0xdd, 0x53, 0x59, 0x05, 0x0a, 0x21, 0x9c,
	0x50, 0x00, 0xe9, 0x84, 0xca, 0x13, 0x02, 0x5e, 0x5a, 0x8a, 0x8a, 0x44, 0x5b, 0xe4, 0x14, 0x55,
	0xe2, 0x65, 0xe5, 0xec, 0x4e, 0x13, 0xeb, 0xec, 0xf5, 0x62, 0x7b, 0x13, 0x85, 0xcf, 0xc0, 0x27,
	0xe0, 0x99, 0xaf, 0xc4, 0xf7, 0x41, 0x1e, 0x6f, 0xee, 0x12, 0xe9, 0x10, 0xe2, 0x6d, 0xfd, 0xdb,
	0xdf, 0x4c, 0x66, 0xc7, 0xf6, 0x04, 0x98, 0xf3, 0xc6, 0x8a, 0x35, 0x66, 0xe5, 0x56, 0xe3, 0x75,
	0x65, 0x8d, 0x37, 0xec, 0x2c, 0xf7, 0x6a, 0x3a, 0xcc, 0x8d, 0xd6, 0xa6, 0x8c, 0x68, 0xfe, 0x27,
	0xc0, 0xf8, 0xd5, 0x56, 0xe3, 0x33, 0x53, 0x7a, 0x6b, 0x94, 0x42, 0xcb, 0x2e, 0xa1, 0xa3, 0x4d,
	0x81, 0x2a, 0x4d, 0x66, 0xc9, 0xa2, 0xcf, 0xe3, 0x82, 0x3d, 0x82, 0xae, 0x43, 0x2b, 0x85, 0x4a,
	0x5b, 0x84, 0x9b, 0x15, 0x4b, 0xe1, 0xbc, 0xca, 0xa5, 0x28, 0x0a, 0x9b, 0x9e, 0xd1, 0x8b, 0xc3,
	0x32, 0xe4, 0x79, 0xb7, 0xb3, 0xb8, 0x4d, 0xdb, 0x31, 0x0f, 0x2d, 0xd8, 0x14, 0x7a, 0xce, 0xe4,
	0x37, 0xe8, 0x65, 0x91, 0x76, 0x66, 0xc9, 0xa2, 0xc3, 0x6f, 0xd7, 0xec, 0x5b, 0x18, 0x6c, 0x50,
	0x28, 0xbf, 0x71, 0x5e, 0x78, 0x97, 0x76, 0x67, 0xc9, 0x62, 0xf0, 0x64, 0x7a, 0x9d, 0x7b, 0x75,
	0x7d, 0x5a, 0xe3, 0xf5, 0x0b, 0xd2, 0xf8, 0xb1, 0xce, 0xbe, 0x03, 0x28, 0x85, 0x46, 0x57, 0x89,
	0x1c, 0x5d, 0x7a, 0x3e, 0x3b, 0x5b, 0x0c, 0x9e, 0x3c, 0xbe, 0x2f, 0xf8, 0xd5, 0xc1, 0xe2, 0x47,
	0x01, 0x21, 0xdc, 0xe9, 0xa2, 0xc0, 0xad, 0x0c, 0xe1, 0xbd, 0x7f, 0x0f, 0x5f, 0xea, 0xe2, 0x7b,
	0xb2, 0xf8, 0x51, 0xc0, 0xf4, 0xef, 0x0e, 0x74, 0x63, 0x55, 0xec, 0x03, 0xe8, 0x7b, 0xa9, 0xd1,
	0x79, 0xa1, 0x2b, 0x6a, 0x62, 0x9b, 0xdf, 0x01, 0x76, 0x05, 0xe3, 0x9d, 0xb0, 0x65, 0xe6, 0x51,
	0x57, 0x59, 0xc0, 0xd4, 0xb7, 0x11, 0x1f, 0x06, 0xfa, 0x06, 0x75, 0xf5, 0x46, 0x6a, 0x0c, 0x56,
	0x6e, 0xa5, 0x3f, 0xb2, 0xda, 0xd1, 0x0a, 0xf4, 0xc4, 0xf2, 0x56, 0x65, 0xab, 0xda, 0xed, 0xa3,
	0xd5, 0xa1, 0x9f, 0x1b, 0x06, 0xfa, 0xb4, 0x76, 0x7b, 0xb2, 0x3e, 0x86, 0x61, 0x65, 0x76, 0x68,
	0xb3, 0x7c, 0x9f, 0x2b, 0x8c, 0x7d, 0x6d, 0xf3, 0x01, 0xb1, 0x67, 0x84, 0x42, 0xa2, 0xa8, 0x98,
	0x32, 0xdb, 0x98, 0xda, 0x86, 0xfe, 0x51, 0x22, 0xa2, 0xaf, 0xcb, 0x17, 0x81, 0xb1, 0xcf, 0xe0,
	0xa2, 0x2e, 0x9d, 0x78, 0x87, 0x99, 0xdb, 0xd4, 0xbe, 0x30, 0xbb, 0x32, 0x34, 0x2a, 0x78, 0x93,
	0xc8, 0x97, 0x07, 0xcc, 0x1e, 0x03, 0x68, 0x2c, 0xa4, 0xc8, 0xd0, 0x5a, 0x97, 0xf6, 0x63, 0x13,
	0x88, 0x3c, 0xb7, 0xd6, 0xb1, 0x4f, 0x61, 0x82, 0xd6, 0x66, 0xca, 0xac, 0x33, 0x2c, 0xbd, 0x95,
	0xe8, 0x52, 0x20, 0x67, 0x84, 0xd6, 0xfe, 0x64, 0xd6, 0xcf, 0x23, 0x64, 0x73, 0x18, 0xad, 0xa4,
	0xc9, 0x2c, 0x8a, 0x22, 0x66, 0x1a, 0x50, 0x17, 0x06, 0x2b, 0x69, 0x38, 0x8a, 0x82, 0x72, 0x5d,
	0xc1, 0x38, 0x38, 0x3b, 0x2b, 0x3d, 0x46, 0x69, 0x18, 0x5b, 0xb5, 0x92, 0xe6, 0x6d, 0x80, 0xc7,
	0x56, 0x5d, 0x6a, 0x51, 0x45, 0x6b, 0x74, 0x6b, 0xfd, 0x12, 0x20, 0x59, 0x9f, 0xc0, 0x28, 0xdf,
	0x60, 0x7e, 0xe3, 0x6a, 0x1d, 0xa5, 0x71, 0xd3, 0xf5, 0x06, 0x92, 0x34, 0x83, 0x41, 0xd8, 0x16,
	0xb4, 0xc2, 0xd7, 0x16, 0xd3, 0x49, 0x2c, 0xe9, 0x08, 0xb1, 0xf7, 0xa1, 0x4f, 0x1b, 0x17, 0xb6,
	0x34, 0xbd, 0x98, 0x25, 0x8b, 0x1e, 0xef, 0x05, 0xf0, 0x56, 0xd8, 0x92, 0x2d, 0xe0, 0x42, 0x6c,
	0x85, 0x54, 0x99, 0xab, 0x84, 0xc5, 0xe8, 0x3c, 0x20, 0x67, 0x4c, 0x7c, 0x19, 0x30, 0x99, 0x5f,
	0xc2, 0x65, 0x81, 0xdb, 0xcc, 0xa2, 0x92, 0x62, 0x25, 0x95, 0xf4, 0xfb, 0x68, 0x33, 0xb2, 0x59,
	0x81, 0x5b, 0x7e, 0xf7, 0x8a, 0x22, 0xae, 0x60, 0x4c, 0xbd, 0x32, 0xa5, 0x6a, 0xdc, 0x87, 0xe4,
	0x0e, 0x03, 0x7d, 0x5d, 0xaa, 0x68, 0x7d, 0x0e, 0x0f, 0xb6, 0x46, 0x09, 0x2f, 0x15, 0x66, 0x1a,
	0x75, 0x14, 0x2f, 0x49, 0x9c, 0x1c, 0x5e, 0xbc, 0x44, 0x1d, 0xdc, 0xe9, 0x12, 0xfa, 0xb7, 0xf7,
	0x85, 0x8d, 0xa1, 0x25, 0x0b, 0x3a, 0xd2, 0x23, 0xde, 0x92, 0x05, 0x63, 0xd0, 0x76, 0xf2, 0x77,
	0xa4, 0x91, 0xd0, 0xe6, 0xf4, 0xcc, 0xe6, 0x40, 0xa7, 0xcf, 0x9e, 0x4e, 0x85, 0x13, 0x36, 0xfd,
	0x2b, 0x81, 0xfe, 0xed, 0x35, 0x0a, 0x59, 0xea, 0xba, 0xc9, 0xdb, 0xe7, 0xf4, 0xcc, 0xde, 0x83,
	0x73, 0xbf, 0xf6, 0x99, 0x2c, 0x5c, 0xda, 0x9a, 0x9d, 0x2d, 0x3a, 0xbc, 0xeb, 0xd7, 0xfe, 0xc7,
	0xc2, 0x85, 0xa9, 0x12, 0xae, 0x3b, 0x36, 0x79, 0xe3, 0x22, 0xa4, 0xb0, 0xa2, 0xbc, 0x69, 0x2e,
	0x09, 0x3d, 0xb3, 0x8f, 0x60, 0xe0, 0x8d, 0x17, 0x2a, 0x5b, 0xed, 0x3d, 0xba, 0xe6, 0x66, 0x00,
	0xa1, 0xa7, 0x81, 0x04, 0x21, 0x6e, 0x44, 0x14, 0xe2, 0xb5, 0x00, 0x42, 0x24, 0xcc, 0x7f, 0x85,
	0xcb, 0xd3, 0xbb, 0xcf, 0xd1, 0xd5, 0xca, 0x1f, 0xcf, 0xbc, 0xe4, 0x74, 0xe6, 0x2d, 0x0e, 0xd5,
	0xb5, 0x68, 0x76, 0x31, 0x9a, 0x1f, 0x1c, 0x5d, 0x65, 0x4a, 0x87, 0xcb, 0xf0, 0xa6, 0xa9, 0x78,
	0xfe, 0x47, 0x02, 0xe3, 0x9f, 0x2d, 0x86, 0xbd, 0x0e, 0xbf, 0xc1, 0xf1, 0xb7, 0xd0, 0xb9, 0x2a,
	0x97, 0xbb, 0x8d, 0xf4, 0xa8, 0xa4, 0xf3, 0x4d, 0xee, 0x13, 0x16, 0xce, 0x5e, 0x69, 0x37, 0xf5,
	0x1a, 0x2b, 0xb1, 0x46, 0x47, 0x3f, 0xd3, 0xe1, 0xc7, 0x88, 0x7d, 0x08, 0xe0, 0x85, 0x5d, 0xa3,
	0xaf, 0x1d, 0x1e, 0xba, 0x7f, 0x44, 0x42, 0x03, 0x2d, 0x3a, 0xf4, 0xd4, 0xab, 0x1e, 0x8f, 0x8b,
	0xf9, 0x37, 0x30, 0x39, 0xa9, 0xc6, 0x55, 0x77, 0xdf, 0x92, 0xfc, 0xd7, 0xb7, 0x7c, 0x0d, 0x83,
	0x65, 0x2e, 0xca, 0xc3, 0x77, 0x3c, 0x3a, 0x4c, 0x42, 0x8a, 0xec, 0xf1, 0xc3, 0x5c, 0x64, 0xd0,
	0x7e, 0x89, 0x5e, 0x50, 0xd1, 0x3d, 0x4e, 0xcf, 0x73, 0x84, 0xe1, 0x5d, 0xa8, 0xab, 0xd8, 0x17,
	0xd0, 0xa5, 0x93, 0xe2, 0xd2, 0x84, 0x26, 0xf0, 0xc3, 0x7b, 0x26, 0x30, 0x6f, 0x94, 0xff, 0xd1,
	0xed, 0x09, 0x8c, 0x7e, 0x30, 0x56, 0x0b, 0xdf, 0xd4, 0xb8, 0xea, 0xd2, 0xdf, 0xdf, 0x57, 0xff,
	0x04, 0x00, 0x00, 0xff, 0xff, 0xfd, 0x08, 0x5a, 0x29, 0x27, 0x07, 0x00, 0x00,
}
