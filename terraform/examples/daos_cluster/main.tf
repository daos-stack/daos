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
  service_account     = var.server_service_account
  pools               = var.server_pools
  gvnic               = var.server_gvnic
}

module "daos_client" {
  source              = "../../modules/daos_client"
  project_id          = var.project_id
  region              = var.region
  zone                = var.zone
  network_name        = var.network_name
  subnetwork_project  = var.subnetwork_project
  subnetwork_name     = var.subnetwork_name
  number_of_instances = var.client_number_of_instances
  labels              = var.client_labels
  preemptible         = var.client_preemptible
  mig_name            = var.client_mig_name
  template_name       = var.client_template_name
  instance_base_name  = var.client_instance_base_name
  machine_type        = var.client_machine_type
  os_family           = var.client_os_family
  os_project          = var.client_os_project
  os_disk_type        = var.client_os_disk_type
  os_disk_size_gb     = var.client_os_disk_size_gb
  service_account     = var.client_service_account
  daos_agent_yml      = module.daos_server.daos_agent_yml
  daos_control_yml    = module.daos_server.daos_control_yml
  gvnic               = var.client_gvnic
}
