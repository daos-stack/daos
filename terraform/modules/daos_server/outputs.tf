
output "access_points" {
  description = "List of DAOS servers to use as access points"
  value = local.access_points
  depends_on = [
    local.access_points
  ]
}
