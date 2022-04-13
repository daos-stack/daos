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

variable "server_labels" {
  description = "Set of key/value label pairs to assign to daos-server instances"
  type        = any
  default     = {}
}

variable "server_os_family" {
  description = "OS GCP image family"
  type        = string
  default     = "daos-server-centos-7"
}

variable "server_os_project" {
  description = "OS GCP image project name. Defaults to project_id if null."
  default     = null
  type        = string
}

variable "server_os_disk_size_gb" {
  description = "OS disk size in GB"
  default     = 20
  type        = number
}

variable "server_os_disk_type" {
  description = "OS disk type ie. pd-ssd, pd-standard"
  default     = "pd-ssd"
  type        = string
}

variable "server_template_name" {
  description = "MIG template name"
  default     = "daos-server"
  type        = string
}

variable "server_mig_name" {
  description = "MIG name "
  default     = "daos-server"
  type        = string
}

variable "server_machine_type" {
  description = "GCP machine type. ie. e2-medium"
  default     = "n2-custom-36-215040"
  type        = string
}

variable "server_instance_base_name" {
  description = "MIG instance base names to use"
  default     = "daos-server"
  type        = string
}

variable "server_number_of_instances" {
  description = "Number of daos servers to bring up"
  default     = 4
  type        = number
}

variable "server_daos_disk_type" {
  #TODO: At some point we will support more than local-ssd with NVME
  # interface.  This variable will be useful then. For now its just this.
  description = "Daos disk type to use. For now only suported one is local-ssd"
  default     = "local-ssd"
  type        = string
}

variable "server_daos_disk_count" {
  description = "Number of local ssd's to use"
  default     = 16
  type        = number
}

variable "server_service_account" {
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
    "https://www.googleapis.com/auth/trace.append"]
  }
}

variable "server_preemptible" {
  description = "If preemptible instances"
  default     = false
  type        = string
}

variable "server_pools" {
  description = "If provided, this module will generate a script to create a list of pools. pool attributes have to be specified in a format acceptable by [dmg](https://docs.daos.io/v2.0/admin/pool_operations/) and daos."
  default     = []
  type = list(object({
    pool_name  = string
    pool_size  = string
    containers = list(string)
    })
  )
}

variable "server_daos_scm_size" {
  description = "scm_size"
  default     = 200
  type        = number
}

variable "server_daos_crt_timeout" {
  description = "crt_timeout"
  default     = 300
  type        = number
}

variable "server_gvnic" {
  description = "Use Google Virtual NIC (gVNIC) network interface on DAOS servers"
  default     = false
  type        = bool
}

variable "client_labels" {
  description = "Set of key/value label pairs to assign to daos-client instances"
  type        = any
  default     = {}
}

variable "client_os_family" {
  description = "OS GCP image family"
  default     = "daos-client-hpc-centos-7"
  type        = string
}

variable "client_os_project" {
  description = "OS GCP image project name. Defaults to project_id if null."
  default     = null
  type        = string
}

variable "client_os_disk_size_gb" {
  description = "OS disk size in GB"
  default     = 20
  type        = number
}

variable "client_os_disk_type" {
  description = "OS disk type ie. pd-ssd, pd-standard"
  default     = "pd-ssd"
  type        = string
}

variable "client_template_name" {
  description = "MIG template name"
  default     = "daos-client"
  type        = string
}

variable "client_mig_name" {
  description = "MIG name "
  default     = "daos-client"
  type        = string
}

variable "client_machine_type" {
  description = "GCP machine type. ie. c2-standard-16"
  default     = "c2-standard-16"
  type        = string
}

variable "client_instance_base_name" {
  description = "MIG instance base names to use"
  default     = "daos-client"
  type        = string
}

variable "client_number_of_instances" {
  description = "Number of daos clients to bring up"
  default     = 4
  type        = number
}

variable "client_service_account" {
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
    "https://www.googleapis.com/auth/trace.append"]
  }
}

variable "client_preemptible" {
  description = "If preemptible instances"
  default     = false
  type        = string
}

variable "client_gvnic" {
  description = "Use Google Virtual NIC (gVNIC) network interface on DAOS clients"
  default     = false
  type        = bool
}
