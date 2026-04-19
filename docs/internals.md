# ghostwire — Internals Deep Dive

> Line-by-line explanation of every technique. Written for people learning kernel internals, not just running the code.

---

## 1. Resolving the Syscall Table

```c
unsigned long addr = kallsyms_lookup_name("sys_call_table");
```

`kallsyms_lookup_name` walks the kernel's symbol table (exported in `/proc/kallsyms`) to find the virtual address of `sys_call_table`. On kernels ≥ 5.7, this function is no longer exported — you must either use `kprobes` to resolve it or register a custom kprobe on a known symbol adjacent to the table. ghostwire uses the direct approach; see `resolve_syscall_table()` for the kprobe fallback path.

The table itself is just an array of `unsigned long` — one entry per syscall number (`__NR_*`), each holding a function pointer.

---

## 2. Disabling Write Protection (CR0 WP bit)

The `sys_call_table` resides in a read-only memory region. The CPU enforces this via the **WP (Write Protect) bit** in control register CR0 (bit 16). When set, even ring-0 code cannot write to read-only pages.

```c
static inline void wp_disable(void) {
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 & ~X86_CR0_WP);
}
```

We clear bit 16, patch the table, then re-enable it. On modern kernels with SMEP/SMAP this is still sufficient since we're operating entirely in kernel space.

> **Detection note:** Any integrity monitor that periodically checksums `sys_call_table` will catch this immediately.

---

## 3. The getdents64 Hook

`getdents64(fd, dirp, count)` fills a buffer with `linux_dirent64` structs — one per directory entry. It's what `ls`, `find`, and `/proc` reads all use internally.

**Hook strategy:**
1. Call the original `getdents64` to populate the buffer normally.
2. Copy the kernel-filled buffer to a kernel allocation (`kdirent`).
3. Walk every `linux_dirent64` entry. If `d_name` starts with `hidden_prefix`, shift all subsequent entries over it (`memmove`), reducing `ret` by `d_reclen`.
4. Copy the modified buffer back to userspace.

The result: the process's file descriptor sees every entry *except* the hidden ones. No file is deleted — it simply never appears in the listing.

**Process hiding via /proc:** `/proc/<pid>/` directories are exposed via `getdents64` on `/proc`. By stripping numeric entries matching hidden PIDs, tools like `ps` (which enumerate `/proc`) never see the process.

---

## 4. The kill Hook (Signal 0 Existence Probe)

`kill(pid, 0)` is a standard POSIX idiom to test if a PID exists — it sends no signal but returns 0 if the process exists or `ESRCH` if it doesn't.

```c
if (sig == 0 && pid_is_hidden(pid))
    return -ESRCH;
```

This makes the process appear non-existent to any code using `kill(pid, 0)` as an existence check — including monitoring tools, watchdog daemons, and systemd.

---

## 5. Module Self-Concealment

The kernel maintains a doubly-linked list of all loaded modules, iterated by `lsmod` and exposed at `/proc/modules` and `/sys/module/`.

```c
prev_module_list_entry = THIS_MODULE->list.prev;
list_del(&THIS_MODULE->list);
```

`list_del` is the standard kernel list removal macro — it patches the `next` and `prev` pointers of the neighbouring nodes to skip over `THIS_MODULE`. The module remains fully functional in memory; it simply isn't reachable by normal enumeration.

We save `prev` so we can `list_add` back on unhide or before `rmmod` (which would panic if it can't find the module entry).

---

## 6. Privilege Escalation via Credential Patching

Every Linux task has a `struct cred` that holds its uid, gid, capabilities, and SELinux context. The kernel provides a safe API to replace a task's credentials atomically:

```c
struct cred *new_creds = prepare_creds();   // deep-copy current creds
new_creds->uid  = GLOBAL_ROOT_UID;          // patch uid/gid to 0
new_creds->euid = GLOBAL_ROOT_UID;
// ...
commit_creds(new_creds);                    // atomically install
```

`prepare_creds` allocates and deep-copies the current credential struct. `commit_creds` uses RCU to atomically swap it into `current->cred`. After this call, the process is uid=0 with full capabilities.

This is the same primitive exploited in most Linux LPE CVEs — ghostwire reaches it directly since we're already in ring-0 as an LKM.

---

## 7. The /proc Control Interface

Rather than a network socket or signal-based covert channel, ghostwire uses a simple `/proc` pseudo-file for userland↔kernel communication. `proc_create` registers a file at `/proc/ghostwire` backed by our `proc_fops` struct.

- **Reads** (`cat /proc/ghostwire`) call `gw_proc_show` via `seq_file` — returns current state.
- **Writes** (`echo "HIDE_PID 1337" > /proc/ghostwire`) call `gw_proc_write` — parses and dispatches commands.

This is intentionally simple and visible (rootkits in the wild use far stealthier IPC). The educational point is the dispatch mechanism and the kernel↔userland boundary crossing via `copy_from_user`.

---

## Suggested Experiments

| Experiment | What you'll learn |
|---|---|
| Patch `getdents64` to hide by inode instead of name | How inodes map to dentries |
| Add a netlink socket instead of `/proc` | Kernel socket programming |
| Hook `readdir` on a specific filesystem only | VFS layer, `file_operations` |
| Detect ghostwire from another LKM | `sys_call_table` integrity checking |
| Port the `kill` hook to use `kprobes` instead | Non-destructive kernel tracing |
| Add capability-based access control to `/proc/ghostwire` | `capable(CAP_SYS_ADMIN)` checks |

---

## Recommended Follow-up Reading

- `linux/cred.h` — credential structure definition
- `linux/syscalls.h` — syscall wrapper macros
- `linux/ftrace.h` — alternative hook approach (no WP toggle needed)
- `fs/proc/base.c` — how `/proc/<pid>` entries are generated
- Phrack 68/14 — "Attacking the Core: Kernel Exploiting Notes"
