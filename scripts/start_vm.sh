#!/usr/bin/env bash
# ==============================================================================
# Script Gerenciador de VMs QEMU (4 VMs x 1GB RAM)
# DevOps Lab - Tandson
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VMS_DIR="$(cd "${SCRIPT_DIR}/../vms" && pwd)"
RUN_DIR="${VMS_DIR}/run"
LOGS_DIR="${VMS_DIR}/logs"
BASE_IMAGE="${VMS_DIR}/resolute-server-cloudimg-amd64.img"
SEED_IMAGE="${VMS_DIR}/seed.img"

# Configurações padrão
TOTAL_VMS=4
VM_RAM="${VM_RAM:-1G}"
VM_CPUS="${VM_CPUS:-1}"
VM_DISK_SIZE="${VM_DISK_SIZE:-20G}"

# Modo de rede: 'user' (SLIRP com port forwarding) ou 'bridge' (IP direto na rede)
# Para usar bridge: export NET_MODE=bridge e export BRIDGE_IF=virbr0 (ou br0)
NET_MODE="${NET_MODE:-user}"
BRIDGE_IF="${BRIDGE_IF:-virbr0}"

mkdir -p "${RUN_DIR}" "${LOGS_DIR}"

# ------------------------------------------------------------------------------
# Funções Utilitárias
# ------------------------------------------------------------------------------

get_vm_role() {
  local id="$1"
  case "${id}" in
    1) echo "lb01  (Nginx LB + Prom)" ;;
    2) echo "web01 (Apache HTTP 1)" ;;
    3) echo "web02 (Apache HTTP 2)" ;;
    4) echo "db01  (MySQL DB)" ;;
    *) echo "vm${id}" ;;
  esac
}

get_vm_pid() {
  local id="$1"
  local pid_file="${RUN_DIR}/vm${id}.pid"
  
  if [ -f "${pid_file}" ]; then
    local pid
    pid="$(cat "${pid_file}" 2>/dev/null || true)"
    if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
      echo "${pid}"
      return 0
    fi
  fi

  # Fallback caso tenha sido iniciada externamente
  local found_pid
  found_pid="$(pgrep -f "qemu-system.*vm${id}\.qcow2" | head -n 1 || true)"
  if [ -n "${found_pid}" ]; then
    echo "${found_pid}"
    return 0
  fi

  return 1
}

ensure_vm_disk() {
  local id="$1"
  local disk_path="${VMS_DIR}/vm${id}.qcow2"

  if [ ! -f "${disk_path}" ]; then
    echo "[INFO] Disco ${disk_path} não encontrado."
    if [ ! -f "${BASE_IMAGE}" ]; then
      echo "[ERRO] Imagem base não encontrada em ${BASE_IMAGE}" >&2
      return 1
    fi
    echo "[INFO] Criando disco ${disk_path} (${VM_DISK_SIZE}) a partir de $(basename "${BASE_IMAGE}")..."
    qemu-img create -f qcow2 -F qcow2 -b "${BASE_IMAGE}" "${disk_path}" "${VM_DISK_SIZE}" >/dev/null
    echo "[OK] Disco vm${id}.qcow2 criado com sucesso."
  fi
}

start_single_vm() {
  local id="$1"
  local role
  role="$(get_vm_role "${id}")"
  local disk_path="${VMS_DIR}/vm${id}.qcow2"
  local pid_file="${RUN_DIR}/vm${id}.pid"
  local log_file="${LOGS_DIR}/vm${id}.log"
  local mac_addr
  mac_addr="$(printf "52:54:00:12:34:%02d" "${id}")"

  if pid="$(get_vm_pid "${id}")"; then
    echo "[AVISO] VM ${id} [${role}] já está rodando (PID ${pid}). Ignorando..."
    return 0
  fi

  ensure_vm_disk "${id}"

  if [ ! -f "${SEED_IMAGE}" ]; then
    echo "[ERRO] Seed image não encontrada em ${SEED_IMAGE}" >&2
    return 1
  fi

  # Portas mapeadas no host para evitar qualquer conflito
  local ssh_port=$((2220 + id))      # VM1: 2221, VM2: 2222, VM3: 2223, VM4: 2224
  local http_port=$((8000 + id))     # VM1: 8001, VM2: 8002, VM3: 8003, VM4: 8004
  local app_port=$((8080 + id))      # VM1: 8081, VM2: 8082, VM3: 8083, VM4: 8084
  local mysql_port=$((3300 + id))    # VM1: 3301, VM2: 3302, VM3: 3303, VM4: 3304
  local metrics_port=$((9090 + id))  # VM1: 9091 (Prometheus), VM2: 9092, VM3: 9093, VM4: 9094
  local node_port=$((9100 + id))     # VM1: 9101 (Node Exp), VM2: 9102, VM3: 9103, VM4: 9104

  echo -n "[INICIANDO] VM ${id} [${role}] (RAM: ${VM_RAM}, CPU: ${VM_CPUS}, MAC: ${mac_addr})... "

  local net_args=()
  if [ "${NET_MODE}" = "bridge" ]; then
    net_args=(
      "-netdev" "bridge,id=net0,br=${BRIDGE_IF}"
      "-device" "virtio-net-pci,netdev=net0,mac=${mac_addr}"
    )
  else
    net_args=(
      "-netdev" "user,id=net0,hostfwd=tcp::${ssh_port}-:22,hostfwd=tcp::${http_port}-:80,hostfwd=tcp::${app_port}-:8080,hostfwd=tcp::${mysql_port}-:3306,hostfwd=tcp::${metrics_port}-:9090,hostfwd=tcp::${node_port}-:9100"
      "-device" "virtio-net-pci,netdev=net0,mac=${mac_addr}"
    )
  fi

  qemu-system-x86_64 \
    -enable-kvm \
    -m "${VM_RAM}" \
    -smp "${VM_CPUS}" \
    -cpu host \
    -drive file="${disk_path}",if=virtio,aio=threads \
    -drive file="${SEED_IMAGE}",format=raw,if=virtio,readonly=on \
    "${net_args[@]}" \
    -display none \
    -daemonize \
    -pidfile "${pid_file}" \
    -serial file:"${log_file}" 2>> "${log_file}"

  sleep 1

  if pid="$(get_vm_pid "${id}")"; then
    echo "[OK] Rodando com PID ${pid}."
    if [ "${NET_MODE}" = "user" ]; then
      echo "           └─ SSH: 127.0.0.1:${ssh_port} | HTTP: :${http_port} | App: :${app_port} | MySQL: :${mysql_port} | Prom: :${metrics_port} | Node: :${node_port}"
    else
      echo "           └─ Conectada à Bridge '${BRIDGE_IF}' (IP via DHCP/ARP local)"
    fi
  else
    echo "[FALHA] Não foi possível iniciar a VM ${id}. Verifique o log em ${log_file}" >&2
    return 1
  fi
}

stop_single_vm() {
  local id="$1"
  local role
  role="$(get_vm_role "${id}")"
  local pid_file="${RUN_DIR}/vm${id}.pid"
  local pid

  if ! pid="$(get_vm_pid "${id}")"; then
    echo "[INFO] VM ${id} [${role}] já está desligada."
    rm -f "${pid_file}"
    return 0
  fi

  echo -n "[PARANDO] VM ${id} [${role}] (PID ${pid})... "
  kill -TERM "${pid}" 2>/dev/null || true

  local count=0
  while kill -0 "${pid}" 2>/dev/null && [ "${count}" -lt 10 ]; do
    sleep 1
    count=$((count + 1))
  done

  if kill -0 "${pid}" 2>/dev/null; then
    echo -n "(forçando SIGKILL)... "
    kill -KILL "${pid}" 2>/dev/null || true
  fi

  rm -f "${pid_file}"
  echo "[OK]"
}

status_all() {
  echo "=========================================================================================================================="
  echo "                                PAINEL DE STATUS DAS VMS QEMU (MODO DE REDE: ${NET_MODE^^})"
  echo "=========================================================================================================================="
  printf "%-5s | %-24s | %-9s | %-7s | %-5s | %-5s | %-19s | %-32s\n" \
    "VM" "ROLE (INVENTORY)" "STATUS" "PID" "RAM" "CPUS" "MAC ADDRESS" "PORTAS HOST"
  echo "--------------------------------------------------------------------------------------------------------------------------"

  for i in $(seq 1 "${TOTAL_VMS}"); do
    local pid
    local role
    role="$(get_vm_role "${i}")"
    local status="PARADA"
    local pid_str="-"
    local mac_addr
    mac_addr="$(printf "52:54:00:12:34:%02d" "${i}")"
    local ssh_p=$((2220 + i))
    local http_p=$((8000 + i))
    local app_p=$((8080 + i))
    local mysql_p=$((3300 + i))
    local metrics_p=$((9090 + i))
    local node_p=$((9100 + i))

    if pid="$(get_vm_pid "${i}")"; then
      status="RODANDO"
      pid_str="${pid}"
    fi

    local ports_str
    if [ "${NET_MODE}" = "user" ]; then
      ports_str="SSH:${ssh_p} H:${http_p} A:${app_p} M:${mysql_p} P:${metrics_p} N:${node_p}"
    else
      ports_str="Bridge: ${BRIDGE_IF}"
    fi

    printf "VM %-2d | %-24s | %-9s | %-7s | %-5s | %-5s | %-19s | %-32s\n" \
      "${i}" "${role}" "${status}" "${pid_str}" "${VM_RAM}" "${VM_CPUS}" "${mac_addr}" "${ports_str}"
  done
  echo "=========================================================================================================================="
  echo "Exemplos de Acesso aos Serviços:"
  echo "  SSH Ansible:       ssh -p 2221 ubuntu@127.0.0.1  (lb01)"
  echo "  Nginx Load Bal.:   http://localhost:8001"
  echo "  Prometheus UI:     http://localhost:9091         (Centralizado no lb01)"
  echo "  Webserver 01:      http://localhost:8082"
  echo "  Webserver 02:      http://localhost:8083"
  echo "  MySQL Database:    localhost:3304"
  echo "=========================================================================================================================="
}

# ------------------------------------------------------------------------------
# Controle de Comandos
# ------------------------------------------------------------------------------

ACTION="${1:-start}"
TARGET="${2:-all}"

# Se o usuário passar apenas o número da VM (ex: ./start_vm.sh 2)
if [[ "${ACTION}" =~ ^[1-4]$ ]]; then
  TARGET="${ACTION}"
  ACTION="start"
fi

case "${ACTION}" in
  start)
    if [ "${TARGET}" = "all" ]; then
      echo "=== Iniciando todas as ${TOTAL_VMS} VMs (RAM: ${VM_RAM} cada) ==="
      for i in $(seq 1 "${TOTAL_VMS}"); do
        start_single_vm "${i}"
      done
      echo ""
      status_all
    elif [[ "${TARGET}" =~ ^[1-4]$ ]]; then
      start_single_vm "${TARGET}"
    else
      echo "Alvo inválido: ${TARGET}. Use 1, 2, 3, 4 ou all."
      exit 1
    fi
    ;;

  stop)
    if [ "${TARGET}" = "all" ]; then
      echo "=== Parando todas as VMs ==="
      for i in $(seq 1 "${TOTAL_VMS}"); do
        stop_single_vm "${i}"
      done
    elif [[ "${TARGET}" =~ ^[1-4]$ ]]; then
      stop_single_vm "${TARGET}"
    else
      echo "Alvo inválido: ${TARGET}. Use 1, 2, 3, 4 ou all."
      exit 1
    fi
    ;;

  restart)
    if [ "${TARGET}" = "all" ]; then
      "${BASH_SOURCE[0]}" stop all
      sleep 2
      "${BASH_SOURCE[0]}" start all
    elif [[ "${TARGET}" =~ ^[1-4]$ ]]; then
      "${BASH_SOURCE[0]}" stop "${TARGET}"
      sleep 2
      "${BASH_SOURCE[0]}" start "${TARGET}"
    fi
    ;;

  status)
    status_all
    ;;

  help|--help|-h)
    echo "Uso: $0 [start|stop|restart|status] [1|2|3|4|all]"
    echo ""
    echo "Exemplos:"
    echo "  $0                  # Inicia todas as 4 VMs"
    echo "  $0 start all        # Inicia todas as 4 VMs"
    echo "  $0 start 1          # Inicia apenas a VM 1 (lb01)"
    echo "  $0 stop all         # Para todas as 4 VMs"
    echo "  $0 stop 2           # Para apenas a VM 2 (web01)"
    echo "  $0 restart 3        # Reinicia a VM 3 (web02)"
    echo "  $0 status           # Exibe o painel de status e portas"
    echo ""
    echo "Variáveis de ambiente opcionais:"
    echo "  VM_RAM=1G VM_CPUS=1 $0 start all"
    echo "  NET_MODE=bridge BRIDGE_IF=virbr0 $0 start all"
    ;;

  *)
    echo "Comando desconhecido: ${ACTION}"
    echo "Use: $0 [start|stop|restart|status] [1|2|3|4|all]"
    exit 1
    ;;
esac
