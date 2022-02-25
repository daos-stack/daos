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
  default     = null
  type        = string
}

variable "os_project" {
  description = "OS GCP image project name"
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
  default     = "n2-custom-20-131072"
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
  default     = "daos-server"
  type        = string
}

variable "number_of_instances" {
  description = "Number of daos servers to bring up"
  default     = 4
  type        = number
}

variable "daos_disk_count" {
  description = "Number of local ssd's to use"
  default     = 16
  type        = number
}
