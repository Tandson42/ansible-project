#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VMS_DIR="${SCRIPT_DIR}/../vms"
SEED_PATH="${VMS_DIR}/seed.img"

if [ ! -f "${SEED_PATH}" ]; then
  echo "Erro: seed.img nao encontrado em ${SEED_PATH}"
  exit 1
fi

declare -a PIDS=()

cleanup() {
  echo -e "\nFinalizando as VMs em execucao..."
  for pid in "${PIDS[@]}"; do
    kill "${pid}" 2>/dev/null || true
  done
  wait 2>/dev/null || true
  echo "Todas as VMs foram encerradas."
}
trap cleanup SIGINT SIGTERM

start_lb() {
  local disk="${VMS_DIR}/balanceador.qcow2"
  local ram="${LB_RAM:-1G}"
  local smp="${LB_SMP:-2}"
  local mac="52:54:00:12:34:00"

  if [ ! -f "${disk}" ]; then
    echo "Erro: Disco nao encontrado: ${disk}"
    return 1
  fi

  echo "=========================================="
  echo "Iniciando BALANCEADOR..."
  echo "  Disco:       ${disk}"
  echo "  RAM / CPU:   ${ram} / ${smp} vCPUs"
  echo "  SSH:         ssh -p 2220 ubuntu@localhost"
  echo "  Web (VIP):   http://localhost:8080 (HAProxy -> porta 80)"
  echo "  API (VIP):   http://localhost:3001 (HAProxy -> porta 3001)"
  echo "  Dashboard:   http://localhost:8404 (Stats HAProxy)"
  echo "=========================================="

  qemu-system-x86_64 \
    -enable-kvm \
    -m "${ram}" \
    -smp "${smp}" \
    -cpu host \
    -drive file="${disk}",if=virtio,aio=threads \
    -drive file="${SEED_PATH}",media=cdrom,if=virtio,readonly=on \
    -netdev user,id=netlb,hostfwd=tcp::2220-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::3001-:3001,hostfwd=tcp::8404-:8404 \
    -device virtio-net-pci,netdev=netlb,mac="${mac}" &

  PIDS+=($!)
}

start_vm() {
  local num="$1"
  local disk="${VMS_DIR}/vm${num}.qcow2"
  local ssh_port=$((2220 + num))               # vm1: 2221, vm2: 2222, vm3: 2223
  local http_port=$((8080 + num))              # vm1: 8081, vm2: 8082, vm3: 8083
  local app_port=$((3010 + num))               # vm1: 3011, vm2: 3012, vm3: 3013
  local mac="52:54:00:12:34:0${num}"
  local ram="${VM_RAM:-2G}"
  local smp="${VM_SMP:-2}"

  if [ ! -f "${disk}" ]; then
    echo "Erro: Disco nao encontrado: ${disk}"
    return 1
  fi

  echo "=========================================="
  echo "Iniciando VM${num}..."
  echo "  Disco:       ${disk}"
  echo "  RAM / CPU:   ${ram} / ${smp} vCPUs"
  echo "  SSH:         ssh -p ${ssh_port} ubuntu@localhost"
  echo "  HTTP:        http://localhost:${http_port} (porta 8080)"
  echo "  Todo-API:    http://localhost:${app_port} (porta 3001)"
  echo "=========================================="

  qemu-system-x86_64 \
    -enable-kvm \
    -m "${ram}" \
    -smp "${smp}" \
    -cpu host \
    -drive file="${disk}",if=virtio,aio=threads \
    -drive file="${SEED_PATH}",media=cdrom,if=virtio,readonly=on \
    -netdev user,id="net${num}",hostfwd=tcp::"${ssh_port}"-:22,hostfwd=tcp::"${http_port}"-:8080,hostfwd=tcp::"${app_port}"-:3001 \
    -device virtio-net-pci,netdev="net${num}",mac="${mac}" &

  PIDS+=($!)
}

TARGET="${1:-all}"

case "${TARGET}" in
  all)
    start_lb
    start_vm 1
    start_vm 2
    start_vm 3
    ;;
  lb|balanceador)
    start_lb
    ;;
  vms)
    start_vm 1
    start_vm 2
    start_vm 3
    ;;
  1|2|3)
    start_vm "${TARGET}"
    ;;
  *)
    echo "Uso: $0 [all|lb|vms|1|2|3]"
    exit 1
    ;;
esac

echo -e "\nInstancias iniciadas em background. Pressione Ctrl+C para encerrar todas.\n"
wait
