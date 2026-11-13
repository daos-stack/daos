---
applyTo: "docs/kb/**,utils/kb/**"
description: "How to maintain the DAOS knowledge base"
---
# Maintaining the DAOS knowledge base

- Never hand-edit `docs/kb/generated/*`; run `utils/kb/kb.py extract`.
- Every claim in a card must be checkable in code. Reference code as `<path>::<symbol>` or
  `<symbol>()`; no line numbers. `utils/kb/kb.py lint` verifies them.
- Cards start with frontmatter (`id`, `kind`, `depends_on`, `used_by`, `related`; lists in
  `[a, b]` form) that `extract` merges into `generated/graph.json`.
- Keep cards under ~200 lines with sections: Purpose, File map, Entry points, Key structures,
  Threading, Invariants and gotchas, Tests, Where to look for. Flows are ordered hop lists plus a
  Mermaid sequence diagram.
- When adding a card, also add a scoped `.github/instructions/<module>.instructions.md` that
  points at it, and list it in `docs/kb/README.md`.
- Run `utils/kb/kb.py check` before committing.
