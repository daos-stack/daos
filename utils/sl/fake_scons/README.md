# fake_scons — pylint shim for SCons files

This directory contains a minimal stub implementation of the SCons API.

## Purpose

It exists solely to allow **pylint** to analyze SCons-based Python files
(e.g. `SConstruct`, `SConscript`, `site_scons/`) without crashing on
`ImportError`. Its functions deliberately do nothing — returning empty
values is sufficient for static analysis.

`daos_pylint.py` adds this path only temporarily inside its own subprocess
via `--init-hook`. It is never exported to the shell environment.

## ⚠️ Warning — do not add to PYTHONPATH

**This directory must not be added to `PYTHONPATH` outside of a pylint run.**

If `utils/sl/fake_scons` is on `PYTHONPATH` when a Python script imports
`SCons.Script` at runtime, it will pick up these no-op stubs instead of the
real SCons installation. This can cause silent failures or crashes in scripts
that depend on real SCons behavior (e.g. the clang-format pre-commit hook
invoking `site_scons/site_tools/extra/extra.py`).
