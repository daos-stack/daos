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
	TrAddr               string   `protobuf:"bytes,7,opt,name=tr_addr,json=trAddr,proto3" json:"tr_addr,omitempty"`
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

func (m *NvmeController_SmdDevice) GetTrAddr() string {
	if m != nil {
		return m.TrAddr
	}
	return ""
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
	Basic                bool     `protobuf:"varint,3,opt,name=Basic,proto3" json:"Basic,omitempty"`
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

func (m *ScanNvmeReq) GetBasic() bool {
	if m != nil {
		return m.Basic
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
	// 932 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x95, 0x5f, 0x8f, 0x1b, 0x35,
	0x10, 0xc0, 0xb5, 0xb9, 0x24, 0x97, 0x4c, 0xfe, 0xb5, 0xee, 0xa9, 0xac, 0x02, 0x85, 0x10, 0x4e,
	0x28, 0x80, 0x74, 0x42, 0xe5, 0x11, 0x78, 0xe0, 0x4a, 0x51, 0x91, 0x68, 0x0f, 0x39, 0x45, 0x95,
	0x78, 0x59, 0x39, 0xbb, 0xd3, 0xc4, 0x3a, 0x7b, 0xbd, 0xd8, 0xde, 0x44, 0xe1, 0x33, 0xf0, 0x6d,
	0x78, 0xe6, 0x63, 0xf0, 0x7d, 0x90, 0xc7, 0x9b, 0xbb, 0x44, 0x2a, 0x42, 0xbc, 0xad, 0x7f, 0xfe,
	0x79, 0x32, 0x1e, 0xdb, 0x13, 0x60, 0xce, 0x1b, 0x2b, 0xd6, 0x98, 0x95, 0x5b, 0x8d, 0x57, 0x95,
	0x35, 0xde, 0xb0, 0xb3, 0xdc, 0xab, 0xe9, 0x30, 0x37, 0x5a, 0x9b, 0x32, 0xa2, 0xf9, 0x9f, 0x00,
	0xe3, 0x57, 0x5b, 0x8d, 0xcf, 0x4c, 0xe9, 0xad, 0x51, 0x0a, 0x2d, 0xbb, 0x80, 0x8e, 0x36, 0x05,
	0xaa, 0x34, 0x99, 0x25, 0x8b, 0x3e, 0x8f, 0x03, 0xf6, 0x18, 0xba, 0x0e, 0xad, 0x14, 0x2a, 0x6d,
	0x11, 0x6e, 0x46, 0x2c, 0x85, 0xf3, 0x2a, 0x97, 0xa2, 0x28, 0x6c, 0x7a, 0x46, 0x13, 0x87, 0x61,
	0x88, 0xf3, 0x76, 0x67, 0x71, 0x9b, 0xb6, 0x63, 0x1c, 0x1a, 0xb0, 0x29, 0xf4, 0x9c, 0xc9, 0x6f,
	0xd1, 0xcb, 0x22, 0xed, 0xcc, 0x92, 0x45, 0x87, 0xdf, 0x8d, 0xd9, 0x37, 0x30, 0xd8, 0xa0, 0x50,
	0x7e, 0xe3, 0xbc, 0xf0, 0x2e, 0xed, 0xce, 0x92, 0xc5, 0xe0, 0xe9, 0xf4, 0x2a, 0xf7, 0xea, 0xea,
	0x34, 0xc7, 0xab, 0x17, 0xa4, 0xf1, 0x63, 0x9d, 0x7d, 0x0b, 0x50, 0x0a, 0x8d, 0xae, 0x12, 0x39,
	0xba, 0xf4, 0x7c, 0x76, 0xb6, 0x18, 0x3c, 0x7d, 0xf2, 0xae, 0xc5, 0xaf, 0x0e, 0x16, 0x3f, 0x5a,
	0x10, 0x96, 0x3b, 0x5d, 0x14, 0xb8, 0x95, 0x61, 0x79, 0xef, 0xdf, 0x97, 0x2f, 0x75, 0xf1, 0x3d,
	0x59, 0xfc, 0x68, 0xc1, 0xf4, 0xef, 0x0e, 0x74, 0x63, 0x56, 0xec, 0x03, 0xe8, 0x7b, 0xa9, 0xd1,
	0x79, 0xa1, 0x2b, 0x2a, 0x62, 0x9b, 0xdf, 0x03, 0x76, 0x09, 0xe3, 0x9d, 0xb0, 0x65, 0xe6, 0x51,
	0x57, 0x59, 0xc0, 0x54, 0xb7, 0x11, 0x1f, 0x06, 0xfa, 0x1a, 0x75, 0xf5, 0x5a, 0x6a, 0x0c, 0x56,
	0x6e, 0xa5, 0x3f, 0xb2, 0xda, 0xd1, 0x0a, 0xf4, 0xc4, 0xf2, 0x56, 0x65, 0xab, 0xda, 0xed, 0xa3,
	0xd5, 0xa1, 0x9f, 0x1b, 0x06, 0x7a, 0x5d, 0xbb, 0x3d, 0x59, 0x1f, 0xc3, 0xb0, 0x32, 0x3b, 0xb4,
	0x59, 0xbe, 0xcf, 0x15, 0xc6, 0xba, 0xb6, 0xf9, 0x80, 0xd8, 0x33, 0x42, 0x21, 0x50, 0x54, 0x4c,
	0x99, 0x6d, 0x4c, 0x6d, 0x43, 0xfd, 0x28, 0x10, 0xd1, 0x9b, 0xf2, 0x45, 0x60, 0xec, 0x33, 0x78,
	0x50, 0x97, 0x4e, 0xbc, 0xc5, 0xcc, 0x6d, 0x6a, 0x5f, 0x98, 0x5d, 0x19, 0x0a, 0x15, 0xbc, 0x49,
	0xe4, 0xcb, 0x03, 0x66, 0x4f, 0x00, 0x34, 0x16, 0x52, 0x64, 0x68, 0xad, 0x4b, 0xfb, 0xb1, 0x08,
	0x44, 0x9e, 0x5b, 0xeb, 0xd8, 0xa7, 0x30, 0x41, 0x6b, 0x33, 0x65, 0xd6, 0x19, 0x96, 0xde, 0x4a,
	0x74, 0x29, 0x90, 0x33, 0x42, 0x6b, 0x7f, 0x32, 0xeb, 0xe7, 0x11, 0xb2, 0x39, 0x8c, 0x56, 0xd2,
	0x64, 0x16, 0x45, 0x11, 0x23, 0x0d, 0xa8, 0x0a, 0x83, 0x95, 0x34, 0x1c, 0x45, 0x41, 0xb1, 0x2e,
	0x61, 0x1c, 0x9c, 0x9d, 0x95, 0x1e, 0xa3, 0x34, 0x8c, 0xa5, 0x5a, 0x49, 0xf3, 0x26, 0xc0, 0x63,
	0xab, 0x2e, 0xb5, 0xa8, 0xa2, 0x35, 0xba, 0xb3, 0x7e, 0x09, 0x90, 0xac, 0x4f, 0x60, 0x94, 0x6f,
	0x30, 0xbf, 0x75, 0xb5, 0x8e, 0xd2, 0xb8, 0xa9, 0x7a, 0x03, 0x49, 0x9a, 0xc1, 0x20, 0x1c, 0x0b,
	0x5a, 0xe1, 0x6b, 0x8b, 0xe9, 0x24, 0xa6, 0x74, 0x84, 0xd8, 0xfb, 0xd0, 0xa7, 0x83, 0x0b, 0x47,
	0x9a, 0x3e, 0x98, 0x25, 0x8b, 0x1e, 0xef, 0x05, 0xf0, 0x46, 0xd8, 0x92, 0x2d, 0xe0, 0x81, 0xd8,
	0x0a, 0xa9, 0x32, 0x57, 0x09, 0x8b, 0xd1, 0x79, 0x48, 0xce, 0x98, 0xf8, 0x32, 0x60, 0x32, 0xbf,
	0x84, 0x8b, 0x02, 0xb7, 0x99, 0x45, 0x25, 0xc5, 0x4a, 0x2a, 0xe9, 0xf7, 0xd1, 0x66, 0x64, 0xb3,
	0x02, 0xb7, 0xfc, 0x7e, 0x8a, 0x56, 0x5c, 0xc2, 0x98, 0x6a, 0x65, 0x4a, 0xd5, 0xb8, 0x8f, 0xc8,
	0x1d, 0x06, 0x7a, 0x53, 0xaa, 0x68, 0x7d, 0x0e, 0x0f, 0xb7, 0x46, 0x09, 0x2f, 0x15, 0x66, 0x1a,
	0x75, 0x14, 0x2f, 0x48, 0x9c, 0x1c, 0x26, 0x5e, 0xa2, 0x0e, 0xee, 0x74, 0x09, 0xfd, 0xbb, 0xf7,
	0xc2, 0xc6, 0xd0, 0x92, 0x05, 0x5d, 0xe9, 0x11, 0x6f, 0xc9, 0x82, 0x31, 0x68, 0x3b, 0xf9, 0x3b,
	0x52, 0x4b, 0x68, 0x73, 0xfa, 0x66, 0x73, 0xa0, 0xdb, 0x67, 0x4f, 0xbb, 0xc2, 0x09, 0x9b, 0xfe,
	0x95, 0x40, 0xff, 0xee, 0x19, 0x85, 0x28, 0x75, 0xdd, 0xc4, 0xed, 0x73, 0xfa, 0x66, 0xef, 0xc1,
	0xb9, 0x5f, 0xfb, 0x4c, 0x16, 0x2e, 0x6d, 0xcd, 0xce, 0x16, 0x1d, 0xde, 0xf5, 0x6b, 0xff, 0x63,
	0xe1, 0x42, 0x57, 0x09, 0xcf, 0x1d, 0x9b, 0xb8, 0x71, 0x10, 0x42, 0x58, 0x51, 0xde, 0x36, 0x8f,
	0x84, 0xbe, 0xd9, 0x47, 0x30, 0xf0, 0xc6, 0x0b, 0x95, 0xad, 0xf6, 0x1e, 0x5d, 0xf3, 0x32, 0x80,
	0xd0, 0x75, 0x20, 0x41, 0x88, 0x07, 0x11, 0x85, 0xf8, 0x2c, 0x80, 0x50, 0x14, 0x42, 0x12, 0x36,
	0xa3, 0x5d, 0x9c, 0xc7, 0xa6, 0xe7, 0xed, 0x77, 0x45, 0x61, 0xe7, 0xbf, 0xc2, 0xc5, 0x69, 0x53,
	0xe0, 0xe8, 0x6a, 0xe5, 0x8f, 0x9b, 0x61, 0x72, 0xda, 0x0c, 0x17, 0x87, 0xb4, 0x5b, 0xd4, 0xd4,
	0x18, 0x35, 0x16, 0x8e, 0xae, 0x32, 0xa5, 0xc3, 0x65, 0x98, 0x69, 0xb6, 0x32, 0xff, 0x23, 0x81,
	0xf1, 0xcf, 0x16, 0xc3, 0x25, 0x08, 0xbf, 0xc1, 0xf1, 0xb7, 0x50, 0xd2, 0x2a, 0x97, 0xbb, 0x8d,
	0xf4, 0xa8, 0xa4, 0xf3, 0x4d, 0xec, 0x13, 0x16, 0x2e, 0x65, 0x69, 0x37, 0xf5, 0x1a, 0x2b, 0xb1,
	0x46, 0x47, 0x3f, 0xd3, 0xe1, 0xc7, 0x88, 0x7d, 0x08, 0xe0, 0x85, 0x5d, 0xa3, 0xaf, 0x1d, 0x1e,
	0x8e, 0xe5, 0x88, 0x84, 0xca, 0x5a, 0x74, 0xe8, 0xa9, 0x88, 0x3d, 0x1e, 0x07, 0xf3, 0xaf, 0x61,
	0x72, 0x92, 0x8d, 0xab, 0xee, 0xf7, 0x92, 0xfc, 0xd7, 0x5e, 0x6e, 0x60, 0xb0, 0xcc, 0x45, 0x79,
	0xd8, 0xc7, 0xe3, 0x43, 0x8b, 0xa4, 0x95, 0x3d, 0x7e, 0x68, 0x98, 0x0c, 0xda, 0x2f, 0xd1, 0x0b,
	0x4a, 0xba, 0xc7, 0xe9, 0x3b, 0x64, 0x73, 0x2d, 0x9c, 0xcc, 0x29, 0xd1, 0x1e, 0x8f, 0x83, 0x39,
	0xc2, 0xf0, 0x3e, 0xa0, 0xab, 0xd8, 0x17, 0xd0, 0xa5, 0x8b, 0xe5, 0xd2, 0x84, 0x1a, 0xf6, 0xa3,
	0x77, 0x34, 0x6c, 0xde, 0x28, 0xff, 0xe3, 0x0c, 0x26, 0x30, 0xfa, 0xc1, 0x58, 0x2d, 0x7c, 0x93,
	0xf9, 0xaa, 0x4b, 0xff, 0x96, 0x5f, 0xfd, 0x13, 0x00, 0x00, 0xff, 0xff, 0xbd, 0xc8, 0x8b, 0xce,
	0x56, 0x07, 0x00, 0x00,
}
