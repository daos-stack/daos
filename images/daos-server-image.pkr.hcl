packer {
  required_plugins {
    googlecompute = {
      version = ">= v1.0.11"
      source  = "github.com/hashicorp/googlecompute"
    }
  }
}

variable "daos_repo_base_url" {
  type = string
}
variable "daos_version" {
  type = string
}
variable "project_id" {
  type = string
}
variable "zone" {
  type = string
}

source "googlecompute" "daos-server-centos-7" {
  disk_size               = "20"
  image_family            = "daos-server-centos-7"
  image_guest_os_features = ["GVNIC"]
  image_name              = "daos-server-centos-7-v${formatdate("YYYYMMDD-hhmmss", timestamp())}"
  machine_type            = "n1-standard-16"
  metadata = {
    enable-oslogin = "False"
  }
  project_id              = "${var.project_id}"
  scopes                  = ["https://www.googleapis.com/auth/cloud-platform"]
  source_image_family     = "centos-7"
  source_image_project_id = ["centos-cloud"]
  ssh_username            = "packer"
  zone                    = "${var.zone}"
}

build {
  sources = ["source.googlecompute.daos-server-centos-7"]

  provisioner "shell" {
    environment_vars = ["DAOS_REPO_BASE_URL=${var.daos_repo_base_url}", "DAOS_VERSION=${var.daos_version}", "DAOS_INSTALL_TYPE=server"]
    execute_command  = "echo 'packer' | sudo -S env {{ .Vars }} {{ .Path }}"
    pause_before     = "5s"
    scripts          = [
      "./scripts/tune.sh",
      "./scripts/install_daos.sh"
    ]
  }

}
