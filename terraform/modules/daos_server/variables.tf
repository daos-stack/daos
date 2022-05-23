/**
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
variable "project_id" {
  description = "The GCP project to use "
  type        = string
}
variable "region" {
  description = "The GCP region to create and test resources in"
  type        = string
}
variable "zone" {
  description = "The GCP zone to create and test resources in"
  type        = string
}

variable "labels" {
  description = "Set of key/value label pairs to assign to daos-server instances"
  type        = any
  default     = {}
}

variable "os_family" {
  description = "OS GCP image family"
  type        = string
  default     = "daos-server-centos-7"
}

variable "os_project" {
  description = "OS GCP image project name. Defaults to project_id if null."
  default     = null
  type        = string
}

variable "os_disk_size_gb" {
  description = "OS disk size in GB"
  default     = 20
  type        = number
}

variable "os_disk_type" {
  description = "OS disk type ie. pd-ssd, pd-standard"
  default     = "pd-ssd"
  type        = string
}

variable "template_name" {
  description = "MIG template name"
  default     = "daos-server"
  type        = string
}

variable "mig_name" {
  description = "MIG name "
  default     = "daos-server"
  type        = string
}

variable "machine_type" {
  description = "GCP machine type. ie. e2-medium"
  default     = "n2-custom-36-215040"
  type        = string
}

variable "network_name" {
  description = "Name of the GCP network to use"
  default     = "default"
  type        = string
}

variable "subnetwork_name" {
  description = "Name of the GCP sub-network to use"
  default     = "default"
  type        = string
}

variable "subnetwork_project" {
  description = "The GCP project where the subnetwork is defined"
  type        = string
  default     = null
}

variable "instance_base_name" {
  description = "MIG instance base names to use"
  default     = "daos-server"
  type        = string
}

variable "number_of_instances" {
  description = "Number of daos servers to bring up"
  default     = 4
  type        = number
}

variable "daos_disk_type" {
  #TODO: At some point we will support more than local-ssd with NVME
  # interface.  This variable will be useful then. For now its just this.
  description = "Daos disk type to use. For now only suported one is local-ssd"
  default     = "local-ssd"
  type        = string
}

variable "daos_disk_count" {
  description = "Number of local ssd's to use"
  default     = 16
  type        = number
}

variable "service_account" {
  description = "Service account to attach to the instance. See https://www.terraform.io/docs/providers/google/r/compute_instance_template.html#service_account."
  type = object({
    email  = string,
    scopes = set(string)
  })
  default = {
    email = null
    scopes = ["https://www.googleapis.com/auth/devstorage.read_only",
      "https://www.googleapis.com/auth/logging.write",
      "https://www.googleapis.com/auth/monitoring.write",
      "https://www.googleapis.com/auth/servicecontrol",
      "https://www.googleapis.com/auth/service.management.readonly",
      "https://www.googleapis.com/auth/trace.append",
    "https://www.googleapis.com/auth/cloud-platform"]
  }
}

variable "preemptible" {
  description = "If preemptible instances"
  default     = false
  type        = string
}

variable "daos_scm_size" {
  description = "scm_size"
  default     = 200
  type        = number
}

variable "daos_crt_timeout" {
  description = "crt_timeout"
  default     = 300
  type        = number
}

variable "gvnic" {
  description = "Use Google Virtual NIC (gVNIC) network interface"
  default     = false
  type        = bool
}

variable "allow_insecure" {
  description = "Sets the allow_insecure setting in the transport_config section of the daos_*.yml files"
  default     = false
  type        = bool
}

variable "pools" {
  description = "List of pools and containers to be created"
  default     = []
  type = list(object({
    name       = string
    size       = string
    tier_ratio = number
    user       = string
    group      = string
    acls       = list(string)
    properties = map(any)
    containers = list(object({
      name            = string
      type            = string
      user            = string
      group           = string
      acls            = list(string)
      properties      = map(any)
      user_attributes = map(any)
    }))
  }))
}
