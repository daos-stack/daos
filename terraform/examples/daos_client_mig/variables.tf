/**
 * Copyright 2021 Google LLC
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
  description = "Set of key/value label pairs to assign to daos-client instances"
  type        = any
  default     = {}
}

variable "os_family" {
  description = "OS GCP image family"
  default     = null
}

variable "os_project" {
  description = "OS GCP image project name"
  default     = null
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
  default     = "daos-client"
  type        = string
}

variable "mig_name" {
  description = "MIG name "
  default     = "daos-client"
  type        = string
}

variable "machine_type" {
  description = "GCP machine type. ie. e2-medium"
  default     = "n2-highmem-16"
  type        = string
}

variable "network" {
  description = "GCP network to use"
  type        = string
}

variable "subnetwork" {
  description = "GCP sub-network to use"
  type        = string
}

variable "subnetwork_project" {
  description = "The GCP project where the subnetwork is defined"
  type        = string
}

variable "instance_base_name" {
  description = "MIG instance base names to use"
  default     = "daos-client"
  type        = string
}

variable "number_of_instances" {
  description = "Number of daos clients to bring up"
  default     = 2
  type        = number
}
