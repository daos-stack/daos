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

variable "access_points" {
  description = "List of servers to add to client .yml files"
  default     = ["daos-server-0001"]
  type        = list(string)
}
