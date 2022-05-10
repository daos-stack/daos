plugin "google" {
  enabled = true
  version = "0.16.1"
  source  = "github.com/terraform-linters/tflint-ruleset-google"
}
rule "terraform_deprecated_index" {
  enabled = true
}
rule "terraform_unused_declarations" {
  enabled = true
}
rule "terraform_documented_variables" {
  enabled = true
}
rule "terraform_comment_syntax" {
  enabled = true
}
rule "terraform_documented_outputs" {
  enabled = true
}
rule "terraform_documented_variables" {
  enabled = true
}
rule "terraform_typed_variables" {
  enabled = true
}
rule "terraform_naming_convention" {
  enabled = true
}
rule "terraform_required_version" {
  enabled = true
}
rule "terraform_required_providers" {
  enabled = true
}
rule "terraform_unused_required_providers" {
  enabled = true
}
