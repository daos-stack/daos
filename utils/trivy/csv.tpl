{{ range . }}
Trivy Vulnerability Scan Results ({{- .Target -}})
VulnerabilityID,Severity,CVSS Score,Title,Library,Vulnerable Version,Fixed Version,Information URL,Triage Information
{{ range .Vulnerabilities }}
    {{- .VulnerabilityID }},
    {{- .Severity }},
    {{- range $key, $value := .CVSS }}
        {{- if (eq $key "nvd") }}
            {{- .V3Score -}}
        {{- end }}
    {{- end }},
    {{- quote .Title }},
    {{- quote .PkgName }},
    {{- quote .InstalledVersion }},
    {{- quote .FixedVersion }},
    {{- .PrimaryURL }}
{{ else -}}
    No vulnerabilities found at this time.
{{ end }}
Trivy Dependency Scan Results ({{ .Target }})
ID,Name,Version,Notes
{{ range .Packages -}}
    {{- quote .ID }},
    {{- quote .Name }},
    {{- quote .Version }}
{{ else -}}
    No dependencies found at this time.
{{ end }}
{{ end }}
