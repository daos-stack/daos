provider "google" {
  region = var.region
}

module "daos_client" {
  source             = "../../modules/daos_client"
  project_id         = var.project_id
  network            = var.network
  subnetwork         = var.subnetwork
  subnetwork_project = var.subnetwork_project
  region             = var.region
  zone               = var.zone
  labels             = var.labels

  number_of_instances = var.number_of_instances

  instance_base_name = var.instance_base_name
  os_disk_size_gb    = var.os_disk_size_gb
  os_disk_type       = var.os_disk_type
  template_name      = var.template_name
  mig_name           = var.mig_name
  machine_type       = var.machine_type
  os_project         = var.os_project
  os_family          = var.os_family
}
