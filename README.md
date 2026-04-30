# ghostwire

> **Educational Linux LKM Rootkit PoC** — syscall hooking, process/file hiding, module concealment & privilege escalation primitives.

```
  ██████╗ ██╗  ██╗ ██████╗ ███████╗████████╗██╗    ██╗██╗██████╗ ███████╗
 ██╔════╝ ██║  ██║██╔═══██╗██╔════╝╚══██╔══╝██║    ██║██║██╔══██╗██╔════╝
 ██║  ███╗███████║██║   ██║███████╗   ██║   ██║ █╗ ██║██║██████╔╝█████╗
 ██║   ██║██╔══██║██║   ██║╚════██║   ██║   ██║███╗██║██║██╔══██╗██╔══╝
 ╚██████╔╝██║  ██║╚██████╔╝███████║   ██║   ╚███╔███╔╝██║██║  ██║███████╗
  ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝    ╚══╝╚══╝ ╚═╝╚═╝  ╚═╝╚══════╝
```

[![License](https://img.shields.io/badge/license-GPL--3.0-red?style=flat-square)](LICENSE)
[![Kernel](https://img.shields.io/badge/kernel-5.x%20%2F%206.x-informational?style=flat-square&logo=linux)](https://kernel.org)
[![Arch](https://img.shields.io/badge/arch-x86__64-lightgrey?style=flat-square)]()
[![Language](https://img.shields.io/badge/language-C%20%7C%20Python-00ff88?style=flat-square)]()
[![Purpose](https://img.shields.io/badge/purpose-educational%20PoC-orange?style=flat-square)]()

---

> ⚠️ **DISCLAIMER — READ BEFORE CLONING**
>
> ghostwire is a **purely educational proof-of-concept** demonstrating well-documented kernel rootkit techniques. It is intended for security researchers, students, and CTF players learning about Linux kernel internals and offensive tooling.
>
> **Run only in isolated, offline virtual machines that you own.**
> Deploying this on any system without explicit authorisation is illegal under the Computer Fraud and Abuse Act (CFAA), the UK Computer Misuse Act, and equivalent laws worldwide.
>
> The author accepts no liability for misuse.

---

## Techniques Demonstrated

| Primitive | Implementation |
|---|---|
| **Syscall Hooking** | Direct `sys_call_table` patching via CR0 WP-bit toggle |
| **Process Hiding** | `getdents64` hook + `kill` sig-0 spoofing → processes invisible to `ps`, `/proc` |
| **File/Dir Hiding** | `getdents64` entry filtering by configurable filename prefix |
| **Module Concealment** | `list_del` from kernel module linked list → invisible to `lsmod` |
| **Privilege Escalation** | `prepare_creds` / `commit_creds` → token theft, uid=0 |
| **Control Interface** | `/proc/ghostwire` pseudo-file → accepts plaintext commands from userland |

---

## Project Structure

```
ghostwire/
├── src/
│   └── ghostwire.c          # LKM — kernel module source
├── include/
│   └── ghostwire.h          # shared constants & definitions
├── cli/
│   └── ghostwire-ctl.py     # Python CLI controller
├── scripts/
│   └── setup-lab.sh         # QEMU lab VM bootstrap
├── docs/
│   └── internals.md         # deep-dive: how each technique works
├── Makefile
└── README.md
```

---

## Prerequisites

- Linux kernel **5.x** or **6.x**, x86_64
- `linux-headers-$(uname -r)`
- `build-essential`
- `python3` (for CLI controller)
- **Root privileges**
- An isolated VM (see [Lab Setup](#lab-setup))

---

## Build & Load

```bash
# Clone
git clone https://github.com/0xNullVector/ghostwire
cd ghostwire

# Build the kernel module
make

# Load
sudo insmod ghostwire.ko

# Verify (module exposes /proc/ghostwire)
cat /proc/ghostwire
```

---

## CLI Controller

```
sudo python3 cli/ghostwire-ctl.py [command]

Commands:
  status                  print current rootkit state
  hide-pid   <pid>        hide a process by PID
  show-pid   <pid>        unhide a process
  set-prefix <prefix>     set file hiding prefix (default: .gw_)
  hide-module             conceal LKM from lsmod
  show-module             re-expose LKM
  privesc                 escalate calling process to uid=0
  shell                   interactive command REPL
  demo                    live walkthrough of all features
```

### Example session

```bash
# Check state
sudo python3 cli/ghostwire-ctl.py status

# Hide a running process
sudo python3 cli/ghostwire-ctl.py hide-pid 1337

# Verify — process should vanish from ps
ps aux | grep 1337

# Hide all files prefixed with '.gw_'
sudo python3 cli/ghostwire-ctl.py set-prefix .gw_

# Conceal the module itself
sudo python3 cli/ghostwire-ctl.py hide-module
lsmod | grep ghostwire   # → nothing

# Full live demo
sudo python3 cli/ghostwire-ctl.py demo
```

---

## Lab Setup

**Never run this on a host machine.** Use an isolated VM:

```bash
bash scripts/setup-lab.sh
```

This prints a ready-to-use QEMU command that mounts the ghostwire source tree as a `virtfs` share inside the VM.

Alternatively, use any of:
- [QEMU/KVM](https://www.qemu.org/) (recommended)
- VMware Workstation
- VirtualBox (disable VT-x nested if issues arise)

Suggested base images: **Debian 12**, **Ubuntu 22.04 LTS**

---

## How It Works — Quick Overview

### Syscall Table Patching

The kernel's `sys_call_table` is a jump table of function pointers for every syscall. ghostwire locates it via `kallsyms_lookup_name`, disables the CR0 write-protect bit, swaps pointers with our hooks, then re-enables WP:

```c
wp_disable();
syscall_table[__NR_getdents64] = (unsigned long)hooked_getdents64;
wp_enable();
```

### Process Hiding

Two-pronged: the `getdents64` hook strips PID entries from `/proc` directory listings, and the `kill` hook returns `ESRCH` for signal 0 checks against hidden PIDs — making them appear non-existent to both procfs and existence probes.

### Module Concealment

Linux tracks loaded modules in a doubly-linked list. Calling `list_del(&THIS_MODULE->list)` unlinks the entry — `lsmod` and `/proc/modules` iterate this list and will never see the module again. The `prev` pointer is saved to re-link on unhide.

### Privilege Escalation

`prepare_creds()` duplicates the current task's credential struct. The copy has its uid/gid fields zeroed (root), then `commit_creds()` atomically replaces the task's creds — classic token-stealing without touching any exploit primitive.

See [`docs/internals.md`](docs/internals.md) for a full line-by-line walkthrough.

---

## Detection & Defences

ghostwire is intentionally detectable — part of understanding offense is understanding defense:

| Detection Vector | Tool |
|---|---|
| Syscall table integrity check | `ksyms` diff, custom kernel module |
| `/proc` vs `ps` discrepancy | `unhide`, `pspy` |
| Module list vs `/sys/module/` | volatility3 `linux.lsmod` |
| eBPF-based EDRs (Falco, Tetragon) | May catch `commit_creds` calls |
| Kernel lockdown mode | Blocks unsigned LKM loading entirely |

---

## References & Further Reading

- [Linux Kernel Module Programming Guide](https://sysprog21.github.io/lkmpg/)
- [Linux Device Drivers, 3rd Ed.](https://lwn.net/Kernel/LDD3/)
- [The Linux Kernel (kernel.org docs)](https://www.kernel.org/doc/html/latest/)
- [Heroin — Phrack rootkit article (classic)](http://phrack.org/issues/61/13.html)
- [Linux Rootkits — xcellerator's blog series](https://xcellerator.github.io/tags/rootkit/)
- [volatility3 — memory forensics](https://github.com/volatilityfoundation/volatility3)

---

## License

GPL-2.0 — see [LICENSE](LICENSE).

---

<div align="center">
<sub>built in the lab, not the wild — [0xNullVector](https://github.com/0xNullVector)</sub>
</div>
