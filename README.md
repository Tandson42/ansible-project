# Ansible Lab - Infraestrutura, Balanceamento e Automação

Projeto Ansible organizado de acordo com as melhores práticas recomendadas pela Red Hat e comunidade Ansible, estruturado com **Roles**, **Inventário Multi-Node**, **Handlers**, **Templates Jinja2** e **FQCN**.

A infraestrutura é composta por instâncias virtuais locais gerenciadas via QEMU/KVM:
- **1 Balanceador de Carga (Load Balancer)** rodando **Nginx** nativo em `balanceador.qcow2`.
- **3 Nós de Aplicação (VMs)** (`vm1`, `vm2` e `vm3`) configurados com Docker Engine, ambiente XFCE, Apache Web Server containerizado, Observabilidade (Promtail) e Backend Todo-API (Node.js).

---

## Arquitetura e Fluxo de Rede

O **Nginx** atua como ponto único de entrada (VIP), distribuindo as conexões de forma redundante (*Round-Robin*) com *health checks* automáticos entre as 3 instâncias de aplicação.

```text
                                  ┌────────────────┐
                                  │    Usuário     │
                                  └───────┬────────┘
                                          │
            ┌─────────────────────────────┼─────────────────────────────┐
            │ http://localhost:8080 (Web) │ http://localhost:3001 (API) │ http://localhost:8080/lb-status
            ▼                             ▼                             ▼
┌───────────────────────────────────────────────────────────────────────────────┐
│                           BALANCEADOR (Nginx)                                 │
│                         (SSH: 2220 ubuntu@localhost)                          │
└──────────────┬──────────────────────────┬──────────────────────────┬──────────┘
               │                          │                          │
               ▼                          ▼                          ▼
       ┌──────────────┐           ┌──────────────┐           ┌──────────────┐
       │     VM 1     │           │     VM 2     │           │     VM 3     │
       │  HTTP: 8081  │           │  HTTP: 8082  │           │  HTTP: 8083  │
       │   API: 3011  │           │   API: 3012  │           │   API: 3013  │
       │   SSH: 2221  │           │   SSH: 2222  │           │   SSH: 2223  │
       └──────────────┘           └──────────────┘           └──────────────┘
```

### Mapeamento de Portas (Host ➔ QEMU)

| Máquina | Disco | SSH (Host ➔ VM:22) | Web Interno (VM:8080) | API Interno (VM:3001) | Finalidade |
|---|---|---|---|---|---|
| **Balanceador** | `balanceador.qcow2` | **2220** | **8080** *(VIP Nginx)* | **3001** *(VIP Nginx)* | Ponto único de entrada / LB |
| **VM 1** | `vm1.qcow2` | **2221** | **8081** | **3011** | Nó de aplicação 1 |
| **VM 2** | `vm2.qcow2` | **2222** | **8082** | **3012** | Nó de aplicação 2 |
| **VM 3** | `vm3.qcow2` | **2223** | **8083** | **3013** | Nó de aplicação 3 |

---

## Pré-requisitos

| Componente | Observação |
|---|---|
| QEMU / KVM | Aceleração KVM ativa (`-enable-kvm`) em host Linux |
| Python 3.10+ | Interpretador do `ansible-core` |
| `cloud-image-utils` | Utilitário `cloud-localds` para geração do `seed.img` |
| `community.docker` | Coleção Ansible para orquestração de containers |

Instalar a coleção Docker no ambiente do Ansible:
```bash
ansible-galaxy collection install community.docker
```

---

## Estrutura do Projeto

```text
.
├── ansible.cfg                      # Configurações do Ansible
├── inventory/
│   └── hosts.ini                    # Inventário com grupos [load_balancer] e [vms]
├── playbook_lb.yml                  # Playbook específico para o balanceador
├── site.yml                         # Playbook mestre (Balanceador + Nós de Aplicação)
├── scripts/
│   └── start_vm.sh                  # Gerenciador de inicialização das VMs QEMU
├── roles/
│   ├── load_balancer/               # Nginx com upstream round-robin e failover
│   ├── common/                      # Mirrors apt, pacotes base e SSH
│   ├── docker/                      # Docker Engine oficial via deb822
│   ├── xfce/                        # Desktop XFCE e LightDM
│   ├── apache_docker/               # Apache HTTP Server containerizado
│   ├── observability/               # Promtail para envio de logs
│   └── todo_app/                    # Backend Todo API em Node.js com logs CRUD
└── vms/
    ├── balanceador.qcow2            # Disco do Load Balancer
    ├── vm1.qcow2, vm2.qcow2, ...    # Discos das VMs de aplicação
    ├── seed.img                     # ISO Cloud-Init com chave SSH e credenciais
    └── user-data.yaml               # Configuração do Cloud-Init
```

---

## Como Executar

### 1. Inicializar as Máquinas Virtuais

O script [scripts/start_vm.sh](file:///home/tandson/lab/scripts/start_vm.sh) permite iniciar todas as máquinas em conjunto ou de forma individual:

- **Iniciar tudo (Balanceador + 3 VMs em background)**:
  ```bash
  ./scripts/start_vm.sh all
  ```
- **Iniciar apenas o Balanceador**:
  ```bash
  ./scripts/start_vm.sh lb
  ```
- **Iniciar apenas as VMs de aplicação**:
  ```bash
  ./scripts/start_vm.sh vms
  ```
- **Iniciar uma VM específica (ex: VM 1)**:
  ```bash
  ./scripts/start_vm.sh 1
  ```
> **Nota:** Pressionar `Ctrl+C` no terminal do script encerra todas as instâncias em execução de maneira coordenada.

---

### 2. Testar Conectividade com o Ansible

Verifique a comunicação SSH com todo o cluster ou por grupo:

```bash
# Testar todos os nós (Balanceador + VMs)
ansible -i inventory/hosts.ini all -m ping

# Testar apenas o Balanceador
ansible -i inventory/hosts.ini load_balancer -m ping

# Testar apenas as VMs de aplicação
ansible -i inventory/hosts.ini vms -m ping
```

---

### 3. Provisionar a Infraestrutura

- **Provisionar apenas o Balanceador Nginx**:
  ```bash
  ansible-playbook -i inventory/hosts.ini playbook_lb.yml
  ```

- **Provisionar todo o ambiente (Balanceador + Aplicação)**:
  ```bash
  ansible-playbook -i inventory/hosts.ini site.yml
  ```

- **Provisionar apenas um nó ou grupo específico**:
  ```bash
  ansible-playbook -i inventory/hosts.ini site.yml --limit load_balancer
  ansible-playbook -i inventory/hosts.ini site.yml --limit vms
  ```

---

### 4. Validar o Acesso e o Balanceamento

Com as VMs em execução:

1. **Acessar a Aplicação Web pelo Balanceador**:
   Abra no navegador ou via curl:
   ```bash
   curl http://localhost:8080/
   ```

2. **Acompanhar o Status do Nginx**:
   Verifique o status das conexões ativas:
   ```bash
   curl http://localhost:8080/lb-status
   ```

3. **Acessar a API Todo via Balanceador**:
   ```bash
   curl http://localhost:3001/api/todos
   ```

4. **Conectar via SSH diretamente em cada nó**:
   ```bash
   ssh -p 2220 ubuntu@localhost   # Balanceador
   ssh -p 2221 ubuntu@localhost   # VM 1
   ssh -p 2222 ubuntu@localhost   # VM 2
   ssh -p 2223 ubuntu@localhost   # VM 3
   ```
