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

locals {
  os_project         = var.os_project != null ? var.os_project : var.project_id
  subnetwork_project = var.subnetwork_project != null ? var.subnetwork_project : var.project_id
  # Google Virtual NIC (gVNIC) network interface
  nic_type                     = var.gvnic ? "GVNIC" : "VIRTIO_NET"
  total_egress_bandwidth_tier  = var.gvnic ? "TIER_1" : "DEFAULT"
  allow_insecure               = var.allow_insecure
  certs_install_script_content = var.certs_install_script_content

  client_startup_script = templatefile(
    "${path.module}/templates/daos_startup_script.tftpl",
    {
      allow_insecure = local.allow_insecure
    }
  )
}

data "google_compute_image" "os_image" {
  family  = var.os_family
  project = local.os_project
}

resource "google_compute_instance_template" "daos_sig_template" {
  provider       = google-beta
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
    network            = var.network_name
    subnetwork         = var.subnetwork_name
    subnetwork_project = local.subnetwork_project
    nic_type           = local.nic_type
  }

  network_performance_config {
    total_egress_bandwidth_tier = local.total_egress_bandwidth_tier
  }

  dynamic "service_account" {
    for_each = var.service_account == null ? [] : [var.service_account]
    content {
      email  = lookup(service_account.value, "email", null)
      scopes = lookup(service_account.value, "scopes", null)
    }
  }

  scheduling {
    preemptible       = var.preemptible
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
      inst_type                    = "daos-client"
      enable-oslogin               = "true"
      daos_control_yaml_content    = var.daos_control_yml
      daos_agent_yaml_content      = var.daos_agent_yml
      certs_install_script_content = local.certs_install_script_content
      startup-script               = local.client_startup_script
      # Adding a reference to the instance template used causes the stateful instance to update
      # if the instance template changes. Otherwise there is no explicit dependency and template
      # changes may not occur on the stateful instance
      instance_template = google_compute_instance_template.daos_sig_template.self_link
    }
  }
}
