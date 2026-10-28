#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DISK_PATH="${SCRIPT_DIR}/../vms/vm1.qcow2"
SEED_IMAGE="${SCRIPT_DIR}/../vms/seed.img" #imagem de user/pass

if [ ! -f "${DISK_PATH}" ]; then
  echo "Erro: Disco nao encontrado em ${DISK_PATH}"
  exit 1
fi

if [ ! -f "${SEED_IMAGE}" ]; then
  echo "Erro: Seed image nao encontrada em ${SEED_IMAGE}"
  exit 1
fi

echo "Iniciando VM QEMU com o disco ${DISK_PATH} e seed ${SEED_IMAGE}..."
qemu-system-x86_64 \
  -enable-kvm \
  -m 4G \
  -smp 4 \
  -cpu host \
  -drive file="${DISK_PATH}",if=virtio,aio=threads \
  -drive file="${SEED_IMAGE}",format=raw,if=virtio,readonly=on \
  -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080,hostfwd=tcp::8081-:8081,hostfwd=tcp::3306-:3306 \
  -device virtio-net-pci,netdev=net0 \
