#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VMS_DIR="${SCRIPT_DIR}/../vms"
BASE_IMAGE="${VMS_DIR}/resolute-server-cloudimg-amd64.img"
DISK_SIZE="20G"

if [ $# -ge 1 ]; then
  VM_NAME="$1"
else
  read -r -p "Digite o nome da VM (sem .qcow2): " VM_NAME
fi

# Remove a extensao .qcow2 caso tenha sido informada
VM_NAME="${VM_NAME%.qcow2}"

if [ -z "${VM_NAME}" ]; then
  echo "Erro: O nome da VM nao pode ser vazio."
  exit 1
fi

if [ ! -f "${BASE_IMAGE}" ]; then
  echo "Erro: Imagem base nao encontrada em ${BASE_IMAGE}"
  exit 1
fi

DISK_PATH="${VMS_DIR}/${VM_NAME}.qcow2"

if [ -f "${DISK_PATH}" ]; then
  echo "Removendo disco atual: ${DISK_PATH}..."
  rm -f "${DISK_PATH}"
fi

echo "Recriando disco ${DISK_PATH} (${DISK_SIZE}) a partir de ${BASE_IMAGE}..."
qemu-img create -f qcow2 -F qcow2 -b "${BASE_IMAGE}" "${DISK_PATH}" "${DISK_SIZE}"

echo "VM ${VM_NAME} recriada com sucesso em ${DISK_PATH}!"
