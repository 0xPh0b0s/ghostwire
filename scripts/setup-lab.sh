#!/usr/bin/env bash
# scripts/setup-lab.sh — spin up a QEMU test VM for ghostwire
# Author: 0xNullVector
#
# Creates a minimal Debian VM with kernel headers pre-installed.
# Mounts the ghostwire source directory as a 9p share.
#
# Dependencies: qemu-system-x86_64, debootstrap (or pre-built image)
# Usage: bash scripts/setup-lab.sh

set -euo pipefail

GW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VM_IMG="${GW_DIR}/lab/ghostwire-lab.qcow2"
VM_MEM="2048"
VM_SMP="2"
KERNEL_VERSION="$(uname -r)"
SSH_PORT="2222"

RED='\033[91m'; GRN='\033[92m'; YLW='\033[93m'; CYN='\033[96m'; NC='\033[0m'

info()  { echo -e "  ${CYN}[*]${NC} $*"; }
ok()    { echo -e "  ${GRN}[+]${NC} $*"; }
warn()  { echo -e "  ${YLW}[!]${NC} $*"; }
die()   { echo -e "  ${RED}[✗]${NC} $*" >&2; exit 1; }

check_deps() {
    info "Checking dependencies..."
    for cmd in qemu-system-x86_64 qemu-img; do
        command -v "$cmd" &>/dev/null || die "Missing dependency: $cmd"
    done
    ok "Dependencies satisfied"
}

create_vm_image() {
    mkdir -p "${GW_DIR}/lab"
    if [[ -f "$VM_IMG" ]]; then
        warn "VM image already exists: $VM_IMG"
        return
    fi
    info "Creating VM disk image (8G)..."
    qemu-img create -f qcow2 "$VM_IMG" 8G
    ok "Image created: $VM_IMG"
}

print_boot_cmd() {
    cat <<EOF

  ┌─────────────────────────────────────────────────────────────┐
  │              ghostwire lab VM — boot command                 │
  └─────────────────────────────────────────────────────────────┘

  ${CYN}qemu-system-x86_64 \\
    -m ${VM_MEM} \\
    -smp ${VM_SMP} \\
    -hda ${VM_IMG} \\
    -net user,hostfwd=tcp::${SSH_PORT}-:22 \\
    -net nic \\
    -virtfs local,path=${GW_DIR},mount_tag=ghostwire,security_model=mapped \\
    -enable-kvm \\
    -nographic${NC}

  Inside the VM, mount the share with:
    ${YLW}mount -t 9p -o trans=virtio ghostwire /mnt/ghostwire${NC}

  Then build and load:
    ${YLW}cd /mnt/ghostwire && make && sudo insmod ghostwire.ko${NC}

EOF
}

print_install_hints() {
    cat <<EOF
  ┌─────────────────────────────────────────────────────────────┐
  │            inside the VM — first-time setup                  │
  └─────────────────────────────────────────────────────────────┘

  Install kernel headers (Debian/Ubuntu):
    ${YLW}sudo apt update && sudo apt install -y \\
        linux-headers-\$(uname -r) build-essential python3${NC}

  Build the module:
    ${YLW}cd /mnt/ghostwire && make${NC}

  Load:
    ${YLW}sudo insmod ghostwire.ko${NC}

  Control:
    ${YLW}sudo python3 cli/ghostwire-ctl.py demo${NC}

EOF
}

main() {
    echo ""
    echo -e "  ${RED}██████╗ ██╗    ██╗    ██╗      █████╗ ██████╗${NC}"
    echo -e "  ${RED}██╔════╝██║    ██║    ██║     ██╔══██╗██╔══██╗${NC}"
    echo -e "  ${RED}██║  ███╗██║ █╗ ██║    ██║     ███████║██████╔╝${NC}"
    echo -e "  ${RED}██║   ██║██║███╗██║    ██║     ██╔══██║██╔══██╗${NC}"
    echo -e "  ${RED}╚██████╔╝╚███╔███╔╝    ███████╗██║  ██║██████╔╝${NC}"
    echo -e "  ${RED} ╚═════╝  ╚══╝╚══╝     ╚══════╝╚═╝  ╚═╝╚═════╝${NC}"
    echo -e "  ${CYN}  ghostwire lab setup — 0xNullVector${NC}"
    echo ""

    check_deps
    create_vm_image
    print_boot_cmd
    print_install_hints

    ok "Lab setup complete."
}

main "$@"
