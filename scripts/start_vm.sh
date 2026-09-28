#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DISK_PATH="${SCRIPT_DIR}/../vms/meu_disco.qcow2"

if [ ! -f "${DISK_PATH}" ]; then
  echo "Erro: Disco nao encontrado em ${DISK_PATH}"
  exit 1
fi

echo "Iniciando VM QEMU com o disco ${DISK_PATH}..."
qemu-system-x86_64 \
  -enable-kvm \
  -m 4G \
  -smp 4 \
  -cpu host \
  -drive file="${DISK_PATH}",if=virtio,aio=threads \
  -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080 \
  -device virtio-net-pci,netdev=net0 \
