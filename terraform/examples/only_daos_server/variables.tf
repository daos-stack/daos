variable "project_id" {
  description = "The GCP project to use "
  type        = string
  default     = null
}
variable "region" {
  description = "The GCP region to create and test resources in"
  type        = string
  default     = null
}
variable "zone" {
  description = "The GCP zone to create and test resources in"
  type        = string
  default     = null
}

variable "network" {
  description = "GCP network to use"
  default     = "default"
  type        = string
}

variable "subnetwork_project" {
  description = "The GCP project where the subnetwork is defined"
  type        = string
  default     = null
}

variable "subnetwork" {
  description = "GCP sub-network to use"
  default     = "default"
  type        = string
}

variable "server_number_of_instances" {
  description = "Number of daos servers to bring up"
  default     = null
  type        = number
}

variable "server_labels" {
  description = "Set of key/value label pairs to assign to daos-server instances"
  type        = any
  default     = {}
}

variable "server_preemptible" {
  description = "If preemptible server instances"
  default     = true
  type        = string
}

variable "server_mig_name" {
  description = "MIG name "
  default     = null
  type        = string
}

variable "server_template_name" {
  description = "MIG template name"
  default     = null
  type        = string
}

variable "server_instance_base_name" {
  description = "MIG instance base names to use"
  default     = null
  type        = string
}

variable "server_machine_type" {
  description = "GCP machine type. e.g. e2-medium"
  default     = null
  type        = string
}

variable "server_os_family" {
  description = "OS GCP image family"
  default     = null
  type        = string
}

variable "server_os_project" {
  description = "OS GCP image project name"
  default     = null
  type        = string
}

variable "server_os_disk_type" {
  description = "OS disk type e.g. pd-ssd, pd-standard"
  default     = "pd-ssd"
  type        = string
}

variable "server_os_disk_size_gb" {
  description = "OS disk size in GB"
  default     = 20
  type        = number
}

variable "server_daos_disk_count" {
  description = "Number of local ssd's to use"
  default     = null
  type        = number
}

variable "server_daos_crt_timeout" {
  description = "crt_timeout"
  default     = null
  type        = number
}

variable "server_daos_scm_size" {
  description = "scm_size"
  default     = null
  type        = number
}
