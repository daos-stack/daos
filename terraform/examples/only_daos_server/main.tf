/**
 * Copyright 2022 Intel Corporation
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
  allow_insecure      = var.allow_insecure
}
