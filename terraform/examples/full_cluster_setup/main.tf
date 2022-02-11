provider "google" {
  region = var.region
}

module "daos_server" {
  source             = "../../modules/daos_server"
  project_id         = var.project_id
  network            = var.network
  subnetwork         = var.subnetwork
  subnetwork_project = var.subnetwork_project
  region             = var.region
  zone               = var.zone
  labels             = var.server_labels

  number_of_instances = var.server_number_of_instances
  daos_disk_count     = var.server_daos_disk_count

  instance_base_name = var.server_instance_base_name
  os_disk_size_gb    = var.server_os_disk_size_gb
  os_disk_type       = var.server_os_disk_type
  template_name      = var.server_template_name
  mig_name           = var.server_mig_name
  machine_type       = var.server_machine_type
  os_project         = var.server_os_project
  os_family          = var.server_os_family
  preemptible        = var.preemptible
}

module "daos_client" {
  source             = "../../modules/daos_client"
  project_id         = var.project_id
  network            = var.network
  subnetwork         = var.subnetwork
  subnetwork_project = var.subnetwork_project
  region             = var.region
  zone               = var.zone
  labels             = var.client_labels

  number_of_instances = var.client_number_of_instances

  instance_base_name = var.client_instance_base_name
  os_disk_size_gb    = var.client_os_disk_size_gb
  os_disk_type       = var.client_os_disk_type
  template_name      = var.client_template_name
  mig_name           = var.client_mig_name
  machine_type       = var.client_machine_type
  os_project         = var.client_os_project
  os_family          = var.client_os_family
  preemptible        = var.preemptible
}
