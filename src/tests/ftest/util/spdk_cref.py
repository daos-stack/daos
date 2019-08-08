#!/usr/bin/python
"""
  (C) Copyright 2018 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from ctypes import *

# SPDK api structures
class SpdkPciAddr(Structure):
    _fields_ = [
        ('domain', c_uint),
        ('bus', c_ubyte),
        ('dev', c_ubyte),
        ('func', c_ubyte)
    ]

class SpdkEnvOpts(Structure):
    _fields_ = [
        ('name', c_char_p),
        ('core_mask', c_char_p),
        ('shm_id', c_int),
        ('mem_channel', c_int),
        ('master_core', c_int),
        ('mem_size', c_int),
        ('no_pci', c_bool),
        ('hugepage_single_segments', c_bool),
        ('unlink_hugepage', c_bool),
        ('num_pci_addr', c_size_t),
        ('hugedir', c_char_p),
        ('pci_blacklist', POINTER(SpdkPciAddr)),
        ('pci_whitelist', POINTER(SpdkPciAddr)),
        ('env_context', c_void_p)
    ]

class Cmic(Structure):
    """ Controller multi-path I/O and namespace sharing capabilities. """
    _fields_ = [
        ('multi_port', c_ubyte),
        ('multi_host', c_ubyte),
        ('sr_iov', c_ubyte),
        ('reserved', c_ubyte),
    ]

class VsRegisterBits(Structure):
    """ Indicates tertiary, minor and major versions.

    Documentation for this struct is in spdk_nvme_vs_register union.
    """
    _fields_ = [
        ('ter', c_uint),
        ('mnr', c_uint),
        ('mjr', c_uint)
    ]

class SpdkNvmeVsRegister(Union):
    """ Register nvme version """
    _fields_ = [
        ('raw', c_uint),
        ('bits', VsRegisterBits)
    ]

class Oaes(Structure):
    """ Optional asynchronous events supported."""
    _fields_ = [
        ("reserved1", c_uint),
        ("ns_attribute_notices", c_uint),
        ("fw_activation_notices", c_uint),
        ("reserved2", c_uint)
    ]

class CtrAtt(Structure):
    """ Controller attributes."""
    _fields_ = [
        ('host_id_exhid_supported', c_uint),
        ('non_operational_power_state_permissive_mode', c_uint),
        ('reserved', c_uint)
    ]

class Oacs(Structure):
    """ Optional admin command support."""
    _fields_ = [
        ('security', c_ushort),
        ('format', c_ushort),
        ('firmware', c_ushort),
        ('ns_manage', c_ushort),
        ('device_self_test', c_ushort),
        ('directives', c_ushort),
        ('nvme_mi', c_ushort),
        ('virtualization_management', c_ushort),
        ('doorbell_buffer_config', c_ushort),
        ('oacs_rsvd', c_ushort)
    ]

class Frmw(Structure):
    """ Firmware Updates."""
    _fields_ = [
        ('slot1_ro', c_ubyte),
        ('num_slots', c_ubyte),
        ('activation_without_reset', c_ubyte),
        ('frmw_rsvd', c_ubyte)
    ]

class Lpa(Structure):
    """ Log page attributes."""
    _fields_ = [
        ('ns_smart', c_ubyte),
        ('celp', c_ubyte),
        ('edlp', c_ubyte),
        ('telemetry', c_ubyte),
        ('lpa_rsvd', c_ubyte)
    ]

class Avscc(Structure):
    """ Admin vendor specific command configuration."""
    _fields_ = [
        ('spec_format', c_ubyte),
        ('avscc_rsvd', c_ubyte)
    ]

class Apsta(Structure):
    """ Autonomous power state transition attributes."""
    _fields_ = [
        ('supported', c_ubyte),
        ('apsta_rsvd', c_ubyte)
    ]

class RpmBs(Structure):
    """ Replay protected memory block support"""
    _fields_ = [
        ('num_rpmb_units', c_ubyte),
        ('auth_method', c_ubyte),
        ('reserved1', c_ubyte),
        ('reserved2', c_ubyte),
        ('total_size', c_ubyte),
        ('access_size', c_ubyte),
    ]

class DstoBits(Structure):
    _fields_ = [
        ('one_only', c_ubyte),
        ('reserved', c_ubyte)
    ]

class Dsto(Union):
    """ Device self-test options"""
    _fields_ = [
        ('raw', c_ubyte),
        ('bits', DstoBits)
    ]

class HctmaBits(Structure):
    _fields_ = [
        ('supported', c_ushort),
        ('reserved', c_ushort)
    ]

class Hctma(Union):
    """ Host controlled thermal management attributes."""
    _fields_ = [
        ('raw', c_ushort),
        ('bits', HctmaBits)
    ]

class SanicapBits(Structure):
    _fields_ = [
        ('crypto_erase', c_uint),
        ('block_erase', c_uint),
        ('overwrite', c_uint),
        ('reserved', c_uint)
    ]

class Sanicap(Union):
    """ Sanitize capabilities."""
    _fields_ = [
        ('raw', c_uint),
        ('bits', SanicapBits)
    ]

class Sqes(Structure):
    """ Submission queue entry size."""
    _fields_ = [
        ('min', c_ubyte),
        ('max', c_ubyte)
    ]

class Cqes(Structure):
    """ Completion queue entry size."""
    _fields_ = [
        ('min', c_ubyte),
        ('max', c_ubyte)
    ]

class Oncs(Structure):
    """ Optional nvm command support."""
    _fields_ = [
        ('compare', c_ushort),
        ('write_unc', c_ushort),
        ('dsm', c_ushort),
        ('write_zeroes', c_ushort),
        ('set_features_save', c_ushort),
        ('reservations', c_ushort),
        ('timestamp', c_ushort),
        ('reserved', c_ushort)
    ]

class Fna(Structure):
    """ Format nvm attributes."""
    _fields_ = [
        ('format_all_ns', c_ubyte),
        ('erase_all_ns', c_ubyte),
        ('crypto_erase_supported', c_ubyte),
        ('reserved', c_ubyte)
    ]

class Vwc(Structure):
    """ Volatile write cache."""
    _fields_ = [
        ('present', c_ubyte),
        ('flush_broadcast', c_ubyte),
        ('reserved', c_ubyte)
    ]

class Sgls(Structure):
    """ SGL support."""
    _fields_ = [
        ('supported', c_uint),
        ('prkeyed_sglesent', c_uint),
        ('reserved1', c_uint),
        ('bit_bucket_descriptor', c_uint),
        ('metadata_pointer', c_uint),
        ('oversized_sgl', c_uint),
        ('metadata_address', c_uint),
        ('sgl_offset', c_uint),
        ('transport_sgl', c_uint),
        ('reserved2', c_uint)
    ]

class CtrAttr(Structure):
    """ Controller attributes for nvmf_specific struct."""
    _fields_ = [
        ('ctrlr_model', c_ubyte),
        ('reserved', c_ubyte)
    ]

class NVMfSpecific(Structure):
    """ NVMe over Fabrics-specific fields."""
    _fields_ = [
        ('ioccsz', c_uint),
        ('iorcsz', c_uint),
        ('icdoff', c_ushort),
        ('ctrattr', CtrAttr),
        ('msdbd', c_ubyte),
        ('reserved', c_ubyte * 244)
    ]

class SpdkNvmePowerState(Structure):
    " NVMe power state."
    _fields_ = [
        ('mp', c_ushort),
        ('reserved1', c_ubyte),
        ('mps', c_ubyte),
        ('nops', c_ubyte),
        ('reserved2', c_ubyte),
        ('enlat', c_uint),
        ('exlat', c_uint),
        ('rrt', c_ubyte),
        ('reserved3', c_ubyte),
        ('rrl', c_ubyte),
        ('reserved4', c_ubyte),
        ('rwt', c_ubyte),
        ('reserved5', c_ubyte),
        ('rwl', c_ubyte),
        ('reserved6', c_ubyte),
        ('reserved7 ', c_ubyte * 16)
    ]

class SpdkNvmeCtrlrData(Structure):
    _fields_ = [
        ('vid', c_ushort),
        ('ssvid', c_ushort),
        ('sn', c_byte * 20),
        ('mn', c_byte * 40),
        ('fr', c_ubyte),
        ('rab', c_ubyte),
        ('ieee', c_ubyte),
        ('cmic', Cmic),
        ('mdts', c_ubyte),
        ('cntlid', c_ushort),
        ('ver', SpdkNvmeVsRegister),
        ('rtd3r', c_uint),
        ('rtd3e', c_uint),
        ('oaes', Oaes),
        ('ctratt', CtrAtt),
        ('reserved_100', c_ubyte * 12),
        ('fguid', c_ubyte * 16),
        ('reserved_128', c_ubyte * 128),
        ('oacs', Oacs),
        ('acl', c_ubyte),
        ('aerl', c_ubyte),
        ('frmw', Frmw),
        ('lpa', Lpa),
        ('elpe', c_ubyte),
        ('npss', c_ubyte),
        ('avscc', Avscc),
        ('apsta', Apsta),
        ('wctemp', c_ushort),
        ('cctemp', c_ushort),
        ('mtfa', c_ushort),
        ('hmpre', c_uint),
        ('hmmin', c_uint),
        ('tnvmcap', c_ulonglong * 2),
        ('unvmcap', c_ulonglong * 2),
        ('rpmbs', RpmBs),
        ('edstt', c_ushort),
        ('dsto', Dsto),
        ('fwug', c_ubyte),
        ('kas', c_ushort),
        ('mntmt', c_ushort),
        ('mxtmt', c_ushort),
        ('sanicap', Sanicap),
        ('reserved3', c_ubyte * 180),
        ('sqes', Sqes),
        ('cqes', Cqes),
        ('maxcmd', c_ushort),
        ('nn', c_uint),
        ('oncs', Oncs),
        ('fuses', c_ushort),
        ('fna', Fna),
        ('vwc', Vwc),
        ('awun', c_ushort),
        ('awupf', c_ushort),
        ('nvscc', c_ubyte),
        ('reserved531', c_ubyte),
        ('acwu', c_ushort),
        ('reserved534', c_ushort),
        ('sgls', Sgls),
        ('reserved4', c_ubyte * 228),
        ('subnqn', c_ubyte * 256),
        ('reserved5', c_ubyte * 768),
        ('nvmf_specific', NVMfSpecific),
        ('psd', SpdkNvmePowerState * 32),
        ('vs', c_ubyte * 1024)
    ]

class SpdkNvmeCtrlrOpts(Structure):
    _fields_ = [
        ('num_io_queues', c_uint),
        ('use_cmb_sps', c_bool),
        ('arb_mechanism', c_uint),
        ('keep_alive_timeout_ms', c_uint),
        ('transport_retry_count', c_int),
        ('io_queue_size', c_uint),
        ('hostnqn', c_char),
        ('io_queue_requests', c_uint),
        ('src_addr', c_char),
        ('src_svcid', c_char),
        ('host_id', c_ubyte),
        ('extended_host_id', c_ubyte),
        ('command_set', c_uint),
        ('admin_timeout_ms', c_uint),
        ('header_digest', c_bool),
        ('data_digest', c_bool),
        ('disable_error_logging', c_bool)
    ]

class SpdkNvmeTransportId(Structure):
    _fields_ = [
        ('trtype', c_uint),
        ('adrfan', c_uint),
        ('traddr', c_char),
        ('trsvcid', c_char),
        ('subnqn', c_char)
    ]

class SpdkNvmeCtrlr(Structure):
    pass

class CtrlrEntry(Structure):
    pass

CtrlrEntry._fields_ = [
        ('ctrlr', POINTER(SpdkNvmeCtrlr)),
        ('tr_addr', POINTER(c_char_p)),
        ('next', POINTER(CtrlrEntry))
        ]

# Create callback functions
ATTACH_CALLBACK = CFUNCTYPE(
    c_void_p,
    POINTER(c_void_p),
    POINTER(SpdkNvmeTransportId),
    POINTER(SpdkNvmeCtrlr),
    POINTER(SpdkNvmeCtrlrOpts))

PROBE_CALLBACK = CFUNCTYPE(
    c_bool,
    POINTER(c_void_p),
    POINTER(SpdkNvmeTransportId),
    POINTER(SpdkNvmeCtrlrOpts))
