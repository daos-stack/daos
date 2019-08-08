#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
'''
from ctypes import *
from spdk_cref import *

class SPDKError(Exception):
    pass

class NVMeError(Exception):
    pass

class SPDK_Env(object):
    """ SPDK testing utilities. """
    def __init__(self, path):
        """ Initialize the SPDK environment.

        Args:
            path (str): Path to shared object library

        Raises:
            SPDKError: Error calling any of SPDK functions.
        """
        self.libspdk = CDLL(path+"libspdk.so")
        self.set_restype()

        # Create opts object/struct
        opts = spdk_env_opts()

        # Initialize the SPDK env
        self.libspdk.spdk_env_opts_init(byref(opts))
        if self.libspdk.spdk_env_init(byref(opts)) < 0:
            raise SPDKError("Could not initialize SPDK env!")

    def set_restype(self):
        """ Set the return type for common functions.
        """
        self.libspdk.spdk_nvme_ns_get_id.restype = POINTER(c_uint)
        self.libspdk.spdk_nvme_ns_is_active.restype = POINTER(c_bool)
        self.libspdk.spdk_nvme_ctrlr_get_data.restype = POINTER(
            spdk_nvme_ctrlr_data)

class NVMeNamespaceData(object):
    """ NVMe namespace details."""
    def __init__(self, ns_id=None, size=None, ctrlr_pci_addr=None):
        self.ns_id = ns_id
        self.size = size
        self.active = None
        self.ns_p = None

class NVMeControllerData(object):
    """ NVMe controller details."""

    def __init__(self, model=None, serial=None, pci_addr=None, fw_rev=None,
        cdata=None):
        self.model = model
        self.serial = serial
        self.pci_addr = pci_addr
        self.fw_rev = fw_rev
        self.cdata = None
        self.ctrlr_p = None
        self.ns_list = []

class NVMe(SPDK_Env):
    """ NVMe testing utilities """

    def __init__(self, path):
        """ Setup the NVMe driver functions and variables.

        Args:
            path (str): Path to shared object library
        """
        super(NVMe, self).__init__(path)
        self.ctrlr_list = []

    def _py_attach_cb(self, cb_ctx, trid, ctrlr, opts):
        """ Report the device that has been attached to the userspace
        NVMe driver.

        Attach_cb will be called for each controller
        after the SPDK NVMe driver has completed initializing the
        controller we chose to attach.

        Args:
            cb_ctx (c_void_p): Opaque value passed to spdk_nvme_attach_cb()
            trid (spdk_nvme_transport_id): NVMe transport identifier.
            ctrlr (spdk_nvme_ctrlr): Opaque handle to NVMe controller.
            opts (spdk_nvme_ctrlr_opts): NVMe controller initialization options
                that were actually used.
        """
        ctrlr_data = NVMeControllerData()
        ctrlr_data.cdata = self.libspdk.spdk_nvme_ctrlr_get_data(ctrlr)

        # Get number of namespaces for given NVMe controller
        num_ns = self.libspdk.spdk_nvme_ctrlr_get_num_ns(ctrlr)
        for nsid in range(1, num_ns+1):
            ns = self.libspdk.spdk_nvme_ctrlr_get_ns(ctrlr, nsid)
            if ns == None:
                continue

            ns_data = NVMeNamespaceData()
            ns_data.ns_p = ns

            # Append ns to ctrlr ns_list
            ctrlr_data.ns_list.append(ns_data)

        # # Append ctrlr to list
        self.ctrlr_list.append(ctrlr_data)

    def _py_probe_cb(self, cb_ctx, trid, opts):
        """ Return true to attach the device.

        probe_cb will be called for each NVMe controller found, giving our
        application a choice on whether to attach to each controller.

        Args:
            cb_ctx (c_void_p): Opaque value passed to spdk_nvme_attach_cb()
            trid (spdk_nvme_transport_id): NVMe transport identifier.
            opts (spdk_nvme_ctrlr_opts): NVMe controller initialization options
                that were actually used.
        """
        return True

    def Discover(self):
        """Discover the NVMe devices.

        Will call spdk_nvme_probe to attach the userspace NVMe driver to each
        device found if desired.

        Raises:
            NVMeError: Error calling the nvme probe function.
        """

        attach_cb = ATTACH_CALLBACK(self._py_attach_cb)
        probe_cb = PROBE_CALLBACK(self._py_probe_cb)

        rc = self.libspdk.spdk_nvme_probe(None, None, probe_cb, attach_cb, None)
        if rc < 0:
            raise NVMeError("Failed to probe nvme devices!")
