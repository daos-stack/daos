---
description: "Finish the current daos failure-timeline analysis without cleanup"
name: "daos-log-timeline-end"
argument-hint: "Finalize the timeline and report retained artifact usage"
agent: "agent"
---

Finish the current daos failure-timeline analysis.

Perform final validation and reporting only. Do not begin new broad searches, download anything, switch branches, apply a stash, or modify the DAOS repository. Do not delete, wipe, truncate, or clean the designated artifact/analysis directory.

Report:

- The final chronological timeline and key findings
- Uncertainty and evidence boundaries
- daos branch/ref and scanner/parser provenance
- Artifact-relative output paths
- The designated artifact directory and its disk usage
- A reminder that all artifacts were intentionally retained and that cleanup is manual

Before presenting Jira-ready text, ensure that no absolute developer-specific paths, credentials, or tokens appear. End with this exact marker on its own line:

DAOS LOG TIMELINE ANALYSIS COMPLETE
