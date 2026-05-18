# Running DAOS Functional Tests

This document describes how to set up one or more nodes with
[Ansible](https://www.ansible.com/) to run DAOS avocado functional tests. The playbook
configures every node in the cluster (OS limits, hugepages, users/groups, packages) and
generates helper bash scripts for building DAOS binaries and launching test suites.

---

## Table of Contents

- [User Guide](#user-guide)
  - [Prerequisites](#prerequisites)
    - [SSH and Sudo Pre-configuration](#ssh-and-sudo-pre-configuration)
  - [Ansible Inventory](#ansible-inventory)
  - [Network Provider](#network-provider)
  - [Running the Playbook](#running-the-playbook)
  - [Validating the Setup](#validating-the-setup)
  - [Installing DAOS Binaries](#installing-daos-binaries)
  - [Launching Functional Tests](#launching-functional-tests)
- [Developer Guide](#developer-guide)
  - [Architecture Overview](#architecture-overview)
  - [Role Descriptions](#role-descriptions)
  - [Proxy Configuration](#proxy-configuration)
  - [Variable Reference](#variable-reference)
  - [Idempotency Design](#idempotency-design)
  - [Linting](#linting)
  - [Molecule Unit Tests](#molecule-unit-tests)
  - [Adding a New Role](#adding-a-new-role)

---

## User Guide

### Prerequisites

#### Target Nodes

One or more bare-metal or virtual nodes running a supported Linux distribution are required.
The supported distributions are **Rocky Linux 8** and **Rocky Linux 9**.

Nodes should be selected according to the hardware requirements of the tests to run
(e.g. PMem, VMD, InfiniBand). YAML configuration files for specific tests may need to be
adapted to match the available hardware.

> :warning: It is **strongly discouraged** to apply this playbook on nodes used for
> day-to-day development. The playbook modifies system-level settings (hugepages, PAM limits,
> kernel parameters, RPM packages) that may break custom configurations previously set by the
> user.

#### Ansible Installation

Install all required tools (Ansible, linters, Molecule) from the directory containing this README:

```bash
python3 -m pip install --user --requirement requirements.txt
ansible-galaxy collection install --upgrade community.general ansible.posix
```

#### SSH and Sudo Pre-configuration

Ansible connects to each node as the ansible user (as defined by `ansible_user` in the
inventory) and elevates privileges with `sudo`. Two conditions must be met before running
the playbook:

1. **SSH key authentication** — the control node's public key must be authorized on each target
   node for the ansible user.
2. **Passwordless sudo** — the ansible user must be allowed to run commands as root without a
   password prompt.

The `scripts/setup-ssh-sudo.sh` helper automates both steps from a fresh cluster where only
password-based access is available. The `-u` flag defaults to the current user if omitted:

```bash
# Using a ClusterShell nodeset (username defaults to current user)
scripts/setup-ssh-sudo.sh -w "node[1-4]" -p user_password -r root_password

# Using an Ansible inventory file with an explicit user
scripts/setup-ssh-sudo.sh -i my-inventory.yml -u ansible_user -p user_password -r root_password
```

The script:
1. Generates an SSH key pair if none exists at `~/.ssh/id_ed25519`.
2. Copies the public key to each target node for the ansible user using `sshpass`.
3. Adds the public key to root's `authorized_keys` on each target node.
4. Creates a passwordless sudoers entry (`/etc/sudoers.d/<user>`) via root SSH.
5. Verifies SSH and `sudo -n` work on every node before exiting.

**Requirements:** `sshpass` and `ssh-copy-id` (both typically available in distro packages).
`nodeset` from the `clustershell` package is required for nodeset notation (`-w`).

If the nodes are already accessible by key and have passwordless sudo configured, this step
can be skipped.

---

### Ansible Inventory

The [Ansible inventory](https://docs.ansible.com/ansible/latest/user_guide/intro_inventory.html)
lists the nodes in the sub-cluster and assigns them to groups based on their role.

#### Host Groups

| Group | Description |
|---|---|
| `daos_dev` | Single node used to build DAOS and launch functional tests. Also acts as a client. |
| `daos_servers` | Nodes where DAOS server engines will run. |
| `daos_clients` | Nodes where client workloads (`ior`, `fio`, etc.) will run. |

Nodes can belong to multiple groups (e.g. a node can be both a server and a client).

#### Inventory Variables

**Mandatory variables** (must be defined in the inventory):

| Variable | Scope | Description |
|---|---|---|
| `daos_runtime_dir` | all | Shared directory for DAOS runtime files (must be accessible with the same path on all nodes). |
| `daos_launch_username` | all | User account that will run the `daos-launch.sh` script. |
| `daos_source_dir` | `daos_dev` | Path to the DAOS source tree on the dev node. |
| `daos_build_dir` | `daos_dev` | Path to the DAOS build output directory on the dev node. |

**Optional variables** (defaults are shown):

| Variable | Default | Description |
|---|---|---|
| `daos_ofi_provider` | `ofi+tcp` | OFI network provider used by DAOS engines. Also controls which network driver RPMs are installed (e.g. `mlx5` for InfiniBand). See [Network Provider](#network-provider) for the full list of supported values. |
| `daos_ofi_interface` | *(none)* | Network interface passed to `launch.py`. When unset, DAOS selects the interface automatically. |
| `daos_hugepages_nb` | `0` | Number of 2 MB hugepages to allocate on server nodes. When `0`, the count is computed automatically as `cores × sockets × daos_hugepages_multiplier`. |
| `daos_hugepages_multiplier` | `1024` | Hugepages-per-physical-core multiplier used when `daos_hugepages_nb` is `0`. The default of `1024` reserves 512 hugepages per VOS engine target plus 512 per sys-xstream (required for MD-on-SSD). Set to `512` for PMEM-only setups that do not use sys-xstreams. |
| `daos_grub_update_enabled` | `true` | When `true`, the playbook also sets hugepages in the GRUB bootloader kernel command line (Phase 2) using `grubby` on RHEL/Rocky or editing `/etc/default/grub` on Debian/Ubuntu. Boot-time reservation eliminates memory fragmentation. A **reboot is required** for the change to take effect; a warning is printed by the playbook. Set to `false` in CI containers that have no real bootloader. |
| `daos_http_proxy` | *(inventory only)* | HTTP/HTTPS proxy URL forwarded to all playbook tasks at play level. When set, `gem`, `pip`, `curl`, and similar commands automatically use it. Not written permanently to the system; scoped to the playbook run only. **Must be set in the inventory**, not in `vars/defaults.yml` (see [Proxy Configuration](#proxy-configuration)). |
| `daos_proxy_bypass` | `"localhost,127.0.0.1"` | Comma-separated list of hosts that bypass the proxy (passed as `no_proxy` / `NO_PROXY`). When unset, defaults to `localhost,127.0.0.1`. |
| `daos_proxy_nofwd` | `[]` | List of tool names whose task files bypass the proxy. By default the list is empty — all tasks receive the proxy. Add `dnf` if your dnf repositories are hosted on a local Artifactory or mirror that is not reachable through the proxy. Tasks in included task files that match an entry are run with an empty proxy environment. **Must be set in the inventory**. See [Proxy Configuration](#proxy-configuration) for the full explanation. |
| `daos_python_version` | `python3.11` | Python interpreter used for `pip` tasks on all nodes. |
| `daos_goproxy` | `direct` | Go module proxy (`GOPROXY`) used when running `go mod download` on the dev node. |

#### Example Inventory

```yaml
all:
  vars:
    daos_runtime_dir: /home/foo/daos
    daos_launch_username: foo
    daos_http_proxy: http://myproxy.net:8080
    # daos_proxy_bypass: "localhost,127.0.0.1"  # optional, shown with defaults
    # daos_proxy_nofwd: [dnf]  # optional: add dnf if repositories are local/non-proxied
  children:
    daos_dev:
      vars:
        daos_source_dir: /home/foo/work/daos
        daos_build_dir: /home/foo/work/daos/build
        daos_ofi_interface: eth0
      hosts:
        node1:
    daos_servers:
      vars:
        daos_hugepages_nb: 8192
      hosts:
        node1:
        node2:
    daos_clients:
      hosts:
        node2:
```

Verify the inventory content with:

```bash
ansible-inventory --graph --vars --inventory my-inventory.yml
```

---

### Network Provider

The `daos_ofi_provider` variable selects the fabric provider used by all DAOS engines. Only one
provider may be specified for an entire DAOS installation; every server and client must use the
same value.

The following providers are officially supported by DAOS (see the
[DAOS Hardware Requirements](https://docs.daos.io/latest/admin/hardware/) and
[UCX Fabric Support](https://docs.daos.io/latest/admin/ucx/) documentation):

| Provider | Fabric | Notes |
|---|---|---|
| `ofi+tcp` | Ethernet (TCP/IP) or IPoIB | **Default.** Works out of the box on any network. Recommended for testing and development environments. |
| `ofi+verbs` | InfiniBand (RDMA) | High-performance RDMA via libfabric. Requires MLNX\_OFED drivers installed before DAOS. Supersedes `ofi+verbs;ofi_rxm` (the `ofi_rxm` auxiliary transport is now built into libfabric). |
| `ofi+verbs;ofi_rxm` | InfiniBand (RDMA) | Legacy form of the verbs provider. Accepted for backwards compatibility; `ofi+verbs` is preferred. Requires MLNX\_OFED. |
| `ucx+dc_x` | InfiniBand (RDMA) | UCX-based provider, **recommended for InfiniBand** production deployments since DAOS 2.4. Requires MLNX\_OFED and the `mercury-ucx` RPM package. |

> :bulb: To discover the providers available on a configured DAOS system, run:
> ```bash
> dmg network scan -p ofi+tcp
> dmg network scan --provider ofi+verbs
> ```

When `daos_ofi_provider` contains `verbs` (e.g. `ofi+verbs` or `ofi+verbs;ofi_rxm`), the
playbook automatically installs the required MLNX\_OFED MOFED driver packages defined in the
`mlnx_deps.rpms` variable. UCX support requires the `mercury-ucx` RPM to be installed manually
in addition to MLNX\_OFED.

---

### Running the Playbook

Once the inventory is ready, run the `ftest.yml` playbook:

```bash
ansible-playbook -i my-inventory.yml ftest.yml
```

When the play succeeds:
- All nodes are configured (OS limits, hugepages, users, packages).
- The scripts `daos-make.sh`, `daos-launch.sh`, and `daos-launch_nlt.sh` are installed in
  `daos_runtime_dir` on the dev node.

> :bulb: The playbook is fully
> [idempotent](https://docs.ansible.com/ansible/latest/reference_appendices/glossary.html#term-Idempotency)
> and can be re-run at any time without side effects.

To validate the playbook syntax before running it:

```bash
ansible-playbook --syntax-check -i my-inventory.yml ftest.yml
```

---

### Validating the Setup

After a successful playbook run, the configuration can be validated end-to-end by building
DAOS and running a basic functional test from the dev node.

**Step 1 — Build DAOS:**

```bash
ssh dev-node
/scratch/user/daos-install/daos-make.sh
```

**Step 2 — Deploy configuration files** for `daos_server`, `daos_control`, and `daos_agent`
(usually `/etc/daos/*.yml`) on the appropriate nodes. This step is outside the scope of the
ansible playbook and depends on the specific DAOS storage layout.

**Step 3 — Run a quick functional test:**

```bash
/scratch/user/daos-install/daos-launch.sh -sv -- \
  --test_servers server1,server2 \
  --test_clients client1 \
  --nvme auto_md_on_ssd \
  --provide ofi+tcp \
  PoolAutotestTest,test_pool_autotest
```

A clean run confirms that: packages are installed, hugepages and kernel parameters are
configured, users/groups are in place, and the dev helpers (`daos-launch.sh`) are functional.

> :information_source: `daos-launch.sh` is installed by the `daos_dev` role into
> `daos_runtime_dir`. Its `--help` output lists all available options.



Build and install DAOS binaries using the generated `daos-make.sh` script on the dev node:

```bash
/home/foo/daos/daos-make.sh --help
```

The script wraps the DAOS `scons` build system with convenience options (build dependencies,
parallel jobs, target selection, etc.).

---

### Launching Functional Tests

Run the functional test suite using the generated `daos-launch.sh` script:

```bash
/home/foo/daos/daos-launch.sh --help
```

Arguments after `--` are passed directly to the `launch.py` script. For example, to run all
tests tagged `hello_world` with auto-detected NVMe devices:

```bash
/home/foo/daos/daos-launch.sh -v -- --nvme=auto hello_world
```

---

## Developer Guide

### Architecture Overview

The playbook is structured as a set of Ansible **roles**, one per node type. Each role
encapsulates related configuration tasks and can be independently tested with Molecule.

```
ftest/
├── ftest.yml                  # Top-level playbook (orchestrates all roles)
├── ansible.cfg                # Ansible configuration
├── inventory-sample.yml       # Annotated inventory template
├── requirements.txt           # Python dependencies (ansible, virtualenv)
├── vars/
│   ├── defaults.yml           # Default values for all variables
│   ├── Rocky8.yml             # Rocky Linux 8 specific vars (package lists)
│   └── Rocky9.yml             # Rocky Linux 9 specific vars (package lists)
└── roles/
    ├── daos_common/           # Applied to every node in the cluster
    ├── daos_server/           # Applied to daos_servers group
    ├── daos_client/           # Applied to daos_clients and daos_dev groups
    ├── daos_dev/              # Applied to daos_dev group
    └── daos_post/             # Post-install tasks applied to every node
```

#### Playbook Execution Order

```
ftest.yml
  ├── [all hosts]      → daos_common   (sudoers, coredumps, base packages)
  ├── [daos_dev]       → daos_client   (daos_agent user/group, client libs)
  ├── [daos_dev]       → daos_dev      (build tools, pip/go deps, helper scripts)
  ├── [daos_servers]   → daos_server   (users/groups, limits, hugepages, packages)
  ├── [daos_clients]   → daos_client   (daos_agent user/group, client libs)
  └── [all hosts]      → daos_post     (Python ftest requirements via pip)
```

---

### Role Descriptions

#### `daos_common`

Applied to every node. Handles configuration that every host needs regardless of its role.

| Sub-task file | Replaces | What it does |
|---|---|---|
| `coredumps.yml` | `setup-coredumps.sh` | Configures `DefaultLimitCORE=infinity` in systemd, sets the core dump pattern via `sysctl`, and creates the `/var/daos_dumps` directory. |
| `base_deps.yml` | *(inline)* | Installs common RPM packages from `daos_base_deps.rpms` (defined per distro in `vars/Rocky{8,9}.yml`). |

#### `daos_server`

Applied to `daos_servers` hosts. Configures everything needed to run `daos_server` engines.

| Sub-task file | Replaces | What it does |
|---|---|---|
| `users_groups.yml` | `setup-server_entries.sh` | Creates `daos_server` (UID 666), `daos_daemons` (GID 668), and `daos_metrics` (GID 669) system accounts. |
| `limits.yml` | `setup-nofile.sh` + `setup-memlock.sh` | Sets `DefaultLimitNOFILE` and `DefaultLimitMEMLOCK` in `systemd/system.conf`, and writes PAM limits files. |
| `hugepages.yml` | `setup-huge_pages.sh` | Configures hugepages in four phases: **(1) sysctl** — sets `vm.nr_hugepages` at runtime (immediate, no reboot, but fragmentation-dependent); **(2) GRUB/bootloader** — adds `default_hugepagesz=2M hugepagesz=2M hugepages=N` to the kernel cmdline for fragmentation-free reservation at boot (RHEL/Rocky uses `grubby`, Debian/Ubuntu edits `/etc/default/grub` + `update-grub`; controlled by `daos_grub_update_enabled`); **(3) THP** — deploys a `disable-thp.service` systemd unit to disable Transparent Huge Pages; **(4) reboot warning** — prints a warning when the running kernel was not started with the expected hugepages parameter. |
| `dependencies.yml` | *(inline)* | Installs server RPM packages from `daos_server_deps.rpms`; conditionally installs MOFED drivers for InfiniBand. |

Templates: `daos_server.service.j2`, `disable-thp.service.j2`.

#### `daos_client`

Applied to `daos_clients` and `daos_dev` hosts. Sets up the DAOS agent user and client
runtime configuration.

| Sub-task file | Replaces | What it does |
|---|---|---|
| `users_groups.yml` | `setup-client_entries.sh` | Creates `daos_agent` (UID 667) and shared daemon groups. |
| `dependencies.yml` | *(inline)* | Installs client RPM packages from `daos_client_deps.rpms`; conditionally installs MOFED. |

Templates: `daos_agent.service.j2`, `daos-ld.so.conf.j2` (adds DAOS libs to the dynamic linker).

#### `daos_dev`

Applied exclusively to the `daos_dev` host. Sets up the development and test-launch environment.

| Sub-task file | Replaces | What it does |
|---|---|---|
| `users_groups.yml` | `setup-dev_entries.sh` | Adds the launch user's NSS entry via `/etc/passwd` if the user does not have a system account (e.g. LDAP user with no local entry). |
| `build_deps.yml` | `install_dev-el{8,9}.sh` + `daos.spec` | Installs the full build dependency package set from `daos_build_deps.rpms` (distro-specific), enables the Ruby module, and installs `fpm` via `gem`. |
| `pip_go_deps.yml` | *(inline)* | Orchestrates `pip_deps.yml` and `go_deps.yml`: installs `requirements-build.txt` and `requirements-utest.txt` system-wide with `pip` (so executables like `meson` and `ninja` land in `/usr/local/bin` and are accessible in PATH), then runs `go mod download` in `src/control/`. |

Templates: `daos-make.sh.j2`, `daos-launch.sh.j2`, `daos-launch_nlt.sh.j2`.

> **DRY pattern**: `daos_dev/tasks/main.yml` reuses server users/groups by calling
> `include_role: name: daos_server tasks_from: users_groups.yml` instead of duplicating the
> account definitions.

#### `daos_post`

Applied to every node as the final step. Installs the Python packages needed by the avocado
functional test framework.

| What it does |
|---|
| Copies `requirements-ftest.txt` from the Ansible controller to the host and installs it system-wide with `pip`. |

The following table summarises which DAOS pip requirements files are installed and on which nodes:

| File | Installed by | Nodes |
|---|---|---|
| `requirements-build.txt` | `daos_dev` / `pip_deps.yml` | `daos_dev` only |
| `requirements-utest.txt` | `daos_dev` / `pip_deps.yml` | `daos_dev` only |
| `requirements-ftest.txt` | `daos_post` | all nodes |

All installs are system-wide (`become: true`, no `--user`) so that executables land in
`/usr/local/bin` (in PATH) rather than `~/.local/bin` (not in PATH on cluster nodes).

The Python interpreter used is controlled by the `daos_python_version` variable
(default: `python3.11`).

---

### Proxy Configuration

The proxy is **scoped to the playbook run** — it is forwarded to tasks via Ansible's
`environment:` mechanism and is **never written permanently** to the nodes (no
`/etc/environment` entries, no `/etc/profile.d/` scripts).

#### How it works

Every play in `ftest.yml` declares `environment: "{{ daos_proxy_env }}"`. This makes
`http_proxy`, `https_proxy`, `HTTP_PROXY`, `HTTPS_PROXY`, `no_proxy`, and `NO_PROXY`
available to all tasks by default — including `gem install`, `pip install`, `curl`, etc.

Certain tools must **bypass** the proxy (e.g. `dnf` tasks that target local Artifactory
repositories). There are two patterns for declaring exceptions, chosen based on how
homogeneous the task file is:

**Pattern A — `apply:` at the `include_tasks` call site** (use when _all_ tasks in the
included file share the same proxy requirement):

```yaml
- name: Install base OS dependencies     # base_deps.yml is dnf-only
  include_tasks:
    file: base_deps.yml
    apply:
      environment: "{{ daos_noproxy_env if 'dnf' in (daos_proxy_nofwd | default([])) else daos_proxy_env }}"
```

> **Why not use this everywhere?** Ansible's `apply: environment:` has *higher* precedence
> than the included task's own `environment:` keyword. If you need some tasks inside the
> file to use the proxy (e.g. `gem install`) while others bypass it (e.g. `dnf`), using
> `apply:` for the whole file will incorrectly strip the proxy from tasks that need it.

**Pattern B — `block:` inside the task file** (use when the file has _mixed_ proxy
requirements, e.g. `build_deps.yml` contains both dnf tasks and `gem install`):

```yaml
# build_deps.yml
- block:
    - name: Install packages via dnf
      become: true
      dnf: { name: "{{ pkgs }}", state: present }
    # ... more dnf tasks ...
  environment: "{{ daos_noproxy_env if 'dnf' in (daos_proxy_nofwd | default([])) else daos_proxy_env }}"

- name: Install fpm build tool via gem    # outside the block — explicit proxy required
  become: true
  community.general.gem:
    name: fpm
    state: present
    user_install: false
  environment: "{{ daos_proxy_env }}"
```

> **Important**: tasks that use `become: true` (or any task) inside a dynamically-included
> file (`include_tasks`) must set `environment:` **explicitly** — they cannot rely on the
> play-level `environment:` being propagated.  Ansible's dynamic include resolution does not
> always forward the play-level environment through the become wrapper, so proxy variables
> may be silently absent even when the play sets them.

The `include_tasks` call in `main.yml` uses no `apply:`, so the block inside handles the
dnf exception while the gem task outside the block carries its own explicit proxy env:

```yaml
- name: Install DAOS build dependencies
  include_tasks: build_deps.yml    # no apply: — block inside handles the dnf exception
```

#### Adding a new exception

1. Add the tool name to `daos_proxy_nofwd` in the inventory.
2. Choose the right pattern:
   - **Pattern A**: if the entire task file uses that tool, add `apply: environment:` to
     the `include_tasks` call in the role's `main.yml`.
   - **Pattern B**: if the file mixes tools (some needing proxy, some not), wrap the
     proxy-bypassing tasks in a `block:` with the conditional `environment:` inside the
     task file itself.

A common example: dnf repositories hosted on a local Artifactory that is not reachable
through the proxy:

```yaml
all:
  vars:
    daos_http_proxy: http://proxy.example.com:8080
    daos_proxy_nofwd:
      - dnf    # dnf repositories are local, bypass proxy
```

#### Reusing the mechanism in another playbook

```yaml
- hosts: all
  vars_files: [path/to/vars/defaults.yml]
  environment: "{{ daos_proxy_env }}"    # all tasks get proxy by default
  roles:
    - my_role
```

In `my_role/tasks/main.yml` (Pattern A — uniform file):

```yaml
- name: Install packages (dnf — bypass proxy)
  include_tasks:
    file: pkg_install.yml
    apply:
      environment: "{{ daos_noproxy_env if 'dnf' in (daos_proxy_nofwd | default([])) else daos_proxy_env }}"
```

#### Activation

Set `daos_http_proxy` in the inventory (group or host vars):

```yaml
all:
  vars:
    daos_http_proxy: http://proxy.example.com:8080
    # daos_proxy_bypass: "localhost,127.0.0.1,*.internal.corp"   # optional
    # daos_proxy_nofwd: [dnf]   # optional: add dnf if repositories are local/non-proxied
```

When `daos_http_proxy` is not set in the inventory, `daos_proxy_env` contains empty strings —
equivalent to no proxy. No tasks are affected.

---

### Variable Reference

Variables are loaded in this precedence order (lowest to highest).  **Ansible's
`vars_files` (position 3) has higher precedence than inventory group variables
(position 1–2)**, which is why proxy variables such as `daos_http_proxy` must be
set in the inventory — not in `vars/defaults.yml`.

1. Inventory host/group variables — set in `inventory.yml` `all.vars` or
   `group_vars/`.
2. `roles/<role>/defaults/main.yml` — role-level defaults (overridden by inventory).
3. `vars/defaults.yml` — loaded via `vars_files:`, overrides inventory group vars.
   Only contains computed helper dicts (`daos_proxy_env`, `daos_noproxy_env`) and
   non-cluster-specific defaults (hugepages multiplier, OFI provider, etc.).
4. `vars/Rocky{8,9}.yml` — loaded dynamically by each role based on
   `ansible_distribution + ansible_distribution_major_version`; contains distro-specific
   package lists.
5. Play vars (vars: key in a play) — highest short of extra-vars.

The distro-specific variable files define the following package list keys:

| Variable key | Used by |
|---|---|
| `daos_base_deps.rpms` | `daos_common` |
| `daos_server_deps.rpms` | `daos_server` |
| `daos_client_deps.rpms` | `daos_client` |
| `daos_build_deps.rpms` | `daos_dev` |
| `daos_build_deps.rpms_x86_64` | `daos_dev` (x86\_64 only, e.g. `ipmctl` on el8) |

---

### Idempotency Design

All tasks use native Ansible modules instead of shell scripts to guarantee idempotency. Key
design decisions:

- **Users and groups**: `group` and `user` modules with `state: present` — safe to re-run.
- **System files**: `lineinfile` and `copy` (with `force: false` where appropriate) — no
  changes if the content already matches.
- **Packages**: `dnf` with `state: present` — never downgrades installed packages.
- **sysctl parameters**: `sysctl` module with `reload: "{{ daos_sysctl_reload }}"` — the
  `daos_sysctl_reload` variable (default `true`) controls whether the kernel parameter is
  applied immediately; set it to `false` in environments where kernel-level changes are not
  possible (e.g. Molecule containers).
- **Systemd tasks**: every `systemd` module call and every handler that issues
  `daemon-reexec`/`daemon-reload` is guarded with `when: ansible_service_mgr == 'systemd'`.
  This allows the playbook to run correctly inside Docker containers where systemd is not
  the init system, without any test-specific conditionals in the production task files.
- **`fpm` installation**: `community.general.gem` with `state: present user_install: false`
  — idempotent gem installation.
- **Ruby module**: the `dnf` module enable task uses
  `changed_when: '"Nothing to do" not in result.stdout'` for correct idempotency reporting.

---

### Linting

Two linters are used to validate the playbook and roles:

- **[yamllint](https://yamllint.readthedocs.io/)** — validates YAML syntax and style against
  the project configuration in `/.yamllint.yaml` at the repository root.
- **[ansible-lint](https://ansible-lint.readthedocs.io/)** — validates Ansible best practices
  (module usage, task naming, idempotency patterns).

Both are included in `requirements.txt` and can be run with the `scripts/lint.sh` wrapper:

```bash
# Run both linters on the full ftest directory
scripts/lint.sh

# Run yamllint only
scripts/lint.sh --yaml-only

# Run ansible-lint only
scripts/lint.sh --ansible-only

# Lint a single role
scripts/lint.sh roles/daos_server
```

#### YAML Style Rules

The project yamllint configuration (`.yamllint.yaml`) enforces the following rules relevant to
Ansible/Molecule files:

| Rule | Setting | Implication |
|---|---|---|
| `document-start` | `present: false` | Do **not** add `---` at the top of YAML files. |
| `line-length` | max 100 | Keep lines within 100 characters; add `# yamllint disable rule:line-length` for long task lines. |
| `indentation` | 2 spaces | Use 2-space indentation throughout. |
| `commas` | no extra spaces | Write `{name: foo, gid: 42}`, not `{name: foo,  gid: 42}`. |
| `truthy` | `true`/`false` only | Use lowercase boolean values. |

---

### Molecule Unit Tests

Each role ships a [Molecule](https://molecule.readthedocs.io/) test scenario under
`roles/<role>/molecule/default/`. Tests run inside Docker containers using the same
`daos/rocky-el9:2.8` image used by DAOS CI.

#### Requirements

- Docker Engine (the local Docker socket at `/var/run/docker.sock` must be accessible)
- The `daos/rocky-el9:2.8` Docker image available locally
- All Python dependencies from `requirements.txt`:

  ```bash
  pip install --user --requirement requirements.txt
  ```

#### Running Tests

Use the `scripts/molecule-test.sh` wrapper to run Molecule tests:

```bash
# Test a single role (full lifecycle: destroy → converge → idempotence → verify → destroy)
scripts/molecule-test.sh daos_common

# Test all roles sequentially
scripts/molecule-test.sh

# Keep the container alive after the run for manual inspection
scripts/molecule-test.sh daos_server --destroy never
```

The script can also be invoked directly via Molecule from inside a role directory:

```bash
cd roles/daos_common
molecule test             # full lifecycle
molecule converge         # apply tasks only
molecule verify           # run assertions only
molecule idempotence      # re-run converge, assert 0 changes
```

#### Test Scenarios per Role

Each Molecule scenario runs the full Molecule lifecycle:
`destroy → syntax → create → converge → idempotence → verify → destroy`

| Role | Container name | What `converge.yml` tests | What `verify.yml` asserts |
|---|---|---|---|
| `daos_common` | `daos-rocky9-common` | `coredumps.yml` (file writes, sysctl config) + proxy sentinel tasks: verifies that `daos_proxy_env` forwards the proxy URL and `daos_noproxy_env` clears it, and that the `daos_proxy_nofwd` list drives the correct env dict for dnf tasks | `/etc/sysctl.d/daos_coredumps.conf` exists; core dump dir exists; `/etc/environment` does NOT contain `http_proxy`; `/etc/profile.d/proxy.sh` does NOT exist |
| `daos_server` | `daos-rocky9-server` | `users_groups.yml` + `limits.yml` + `hugepages.yml` (sysctl only; GRUB disabled via `daos_grub_update_enabled: false`) + Debian GRUB path exercised via `ansible_os_family: Debian` override | Groups/user exist; limits files exist; hugepages sysctl file exists; `/etc/default/grub` contains `hugepages=N`, `hugepagesz=2M`, `default_hugepagesz=2M`, each appearing exactly once (idempotency) |
| `daos_client` | `daos-rocky9-client` | `users_groups.yml` | `daos_agent` group and user exist |
| `daos_dev` | `daos-rocky9-dev` | `users_groups.yml` (with `daos_launch_username: root`) + `build_deps.yml` with a fake repo ID to verify the availability check warns instead of failing; `daos_proxy_nofwd: [dnf]` so the dnf block bypasses the fake test proxy (the gem task outside the block is unaffected — `fpm` is pre-installed in the image) | Task completes without error; `fake-repo-does-not-exist` is not present in `dnf repolist` output |
| `daos_post` | `daos-rocky9-post` | Full `main.yml` with a mock `requirements-ftest.txt` | `python3 -c "import distro"` succeeds |

> **Why only sub-tasks, not the full role?**
> Package installs (`dnf`, `pip`, `gem`, `go mod`) require network access and take several
> minutes. By using `include_role: tasks_from:` in `converge.yml`, the tests focus on the
> configuration logic (file writes, user/group creation, sysctl config) which runs quickly
> and deterministically in a container without network access.

#### Known Container Limitations

- **`daos_server` — PID 1 user conflict**: The `daos/rocky-el9:2.8` image runs as the
  `daos_server` user (PID 1). The `user` module cannot modify a user that owns a running
  process, so `usermod` fails with `rc=8`. The `converge.yml` wraps this task in a
  `block/rescue` to catch and log the error gracefully. This is a container-only limitation;
  on real nodes the user is created fresh and the task succeeds.

- **Hugepages sysctl**: The `daos_sysctl_reload` variable is set to `false` in all
  `converge.yml` files to prevent the `sysctl` module from applying kernel-level changes
  inside the container. The sysctl configuration *file* is still written and verified.

- **Systemd services**: All `systemd` module calls and handlers are guarded with
  `when: ansible_service_mgr == 'systemd'`. They are silently skipped inside Docker
  containers and executed normally on real hosts.

- **`ANSIBLE_REMOTE_TMP`**: Set to `/tmp` in every `molecule.yml` provisioner environment.
  This is required because `become: true` inside the container otherwise tries to create
  a temporary directory in `~/.ansible/tmp` which may not be writable for the container's
  internal user.

- **`daos_dev` — dnf repo isolation**: The `daos_dev` molecule scenario invokes
  `build_deps.yml` directly (without first running `daos_common`) so `skip_if_unavailable`
  and `timeout` are not pre-configured in `/etc/dnf/dnf.conf`. The `converge.yml` adds a
  setup task that applies these settings before running the role, making `update_cache: true`
  resilient to external repos (e.g. EPEL) that are unreachable in a network-restricted
  container environment.


  server,client,dev,post}`) so that multiple roles can be tested in parallel without
  Docker name conflicts.

#### Molecule Configuration Reference

All `molecule.yml` files follow the same structure:

```yaml
driver:
  name: docker
platforms:
  - name: daos-rocky9-<role>
    image: daos/rocky-el9:2.8
    pre_build_image: true
    privileged: true
provisioner:
  name: ansible
  env:
    ANSIBLE_ROLES_PATH: "${MOLECULE_PROJECT_DIRECTORY}/.."
    ANSIBLE_FORCE_COLOR: "true"
    ANSIBLE_REMOTE_TMP: "/tmp"
verifier:
  name: ansible
```

`ANSIBLE_ROLES_PATH` points one level above the role being tested so that
`include_role: name: daos_server` (for example) resolves correctly from within
the `daos_dev` converge playbook.

---

### Adding a New Role

1. Create the role skeleton under `roles/`:

   ```bash
   cd roles
   ansible-galaxy role init daos_<name>
   ```

2. Write tasks in `tasks/main.yml`, splitting complex work into sub-task files
   (e.g. `tasks/users_groups.yml`, `tasks/packages.yml`) so that Molecule can
   test them independently via `include_role: tasks_from:`.

3. Add a `handlers/main.yml` with the `when: ansible_service_mgr == 'systemd'`
   guard on any handler that calls `systemd`.

4. Add a `molecule/default/` scenario:
   - `molecule.yml` — use a unique container name `daos-rocky9-<name>` and set
     `ANSIBLE_REMOTE_TMP: "/tmp"`.
   - `converge.yml` — include only the sub-task files that are testable without
     network access or kernel privileges.
   - `verify.yml` — assert that the expected files, users, and groups exist.

   > :warning: Do **not** add `---` at the top of any YAML file: the project yamllint
   > configuration forbids the document-start marker (`document-start: present: false`).

5. Add the new role to the appropriate play(s) in `ftest.yml`.

6. Verify linting and the Molecule scenario both pass:

   ```bash
   scripts/lint.sh roles/daos_<name>
   scripts/molecule-test.sh daos_<name>
   ```
