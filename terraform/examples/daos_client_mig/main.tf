/**
 * Copyright 2021 Google LLC
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
