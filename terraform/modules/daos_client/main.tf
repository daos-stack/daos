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

data "google_compute_image" "os_image" {
  family  = var.os_family
  project = var.os_project
}

resource "google_compute_instance_template" "daos_sig_template" {
  name           = var.template_name
  machine_type   = var.machine_type
  can_ip_forward = false
  tags           = ["daos-client"]
  project        = var.project_id
  region         = var.region
  labels         = var.labels

  disk {
    source_image = data.google_compute_image.os_image.self_link
    auto_delete  = true
    boot         = true
    disk_type    = var.os_disk_type
    disk_size_gb = var.os_disk_size_gb
  }

  network_interface {
    network            = var.network
    subnetwork         = var.subnetwork
    subnetwork_project = var.subnetwork_project
  }

  service_account {
    scopes = var.daos_service_account_scopes
  }

  scheduling {
    preemptible = var.preemptible
    automatic_restart = false
  }
}

resource "google_compute_instance_group_manager" "daos_sig" {
  description = "Stateful Instance group for DAOS clients"
  name        = var.mig_name

  version {
    instance_template = google_compute_instance_template.daos_sig_template.self_link
  }

  base_instance_name = var.instance_base_name
  zone               = var.zone
  project            = var.project_id
}


resource "google_compute_per_instance_config" "named_instances" {
  zone                   = var.zone
  project                = var.project_id
  instance_group_manager = google_compute_instance_group_manager.daos_sig.name
  count                  = var.number_of_instances
  name                   = format("%s-%04d", var.instance_base_name, sum([count.index, 1]))
  preserved_state {
    metadata = {
      inst_type      = "daos-client"
      enable-oslogin = "true"
      // Adding a reference to the instance template used causes the stateful instance to update
      // if the instance template changes. Otherwise there is no explicit dependency and template
      // changes may not occur on the stateful instance
      instance_template = google_compute_instance_template.daos_sig_template.self_link
    }
  }
}
