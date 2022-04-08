provider "google" {
  region = var.region
}

module "daos_server" {
  source              = "../../modules/daos_server"
  project_id          = var.project_id
  region              = var.region
  zone                = var.zone
  network_name        = var.network_name
  subnetwork_project  = var.subnetwork_project
  subnetwork_name     = var.subnetwork_name
  number_of_instances = var.server_number_of_instances
  labels              = var.server_labels
  preemptible         = var.server_preemptible
  mig_name            = var.server_mig_name
  template_name       = var.server_template_name
  instance_base_name  = var.server_instance_base_name
  machine_type        = var.server_machine_type
  os_family           = var.server_os_family
  os_project          = var.server_os_project
  os_disk_type        = var.server_os_disk_type
  os_disk_size_gb     = var.server_os_disk_size_gb
  daos_disk_count     = var.server_daos_disk_count
  daos_disk_type      = var.server_daos_disk_type
  daos_crt_timeout    = var.server_daos_crt_timeout
  daos_scm_size       = var.server_daos_scm_size

  service_account = var.server_service_account
  pools           = var.server_pools
}
