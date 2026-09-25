# Ansible Lab - Infraestrutura e Automação

Projeto Ansible organizado de acordo com as melhores práticas recomendadas pela Red Hat e comunidade Ansible, estruturado por **Roles**, **Inventory com Group Vars**, **Handlers** e **FQCN**.

## Estrutura do Projeto

```text
.
├── ansible.cfg                      # Configurações globais (inventory, roles_path)
├── .gitignore                       # Ignora discos pesados (*.qcow2), caches e logs
├── README.md                        # Documentação do projeto
├── site.yml                         # Playbook mestre (orquestrador)
│
├── inventory/
│   ├── hosts.ini                    # Inventário de hosts
│   └── group_vars/
│       └── vm_qemu.yml              # Variáveis de conexão da VM
│
├── roles/
│   ├── common/                      # Utilitários de SO, SSH e limpeza de repositórios
│   │   ├── tasks/main.yml
│   │   └── handlers/main.yml
│   ├── apache/                      # Servidor Apache2 e handlers de reinício
│   │   ├── tasks/main.yml
│   │   └── handlers/main.yml
│   └── docker/                      # Docker Engine oficial via deb822_repository
│       ├── tasks/main.yml
│       ├── handlers/main.yml
│       └── vars/main.yml
│
├── scripts/
│   └── start_vm.sh                  # Script de inicialização da VM QEMU
│
└── vms/
    └── meu_disco.qcow2              # Imagem do disco QEMU (isolado do Git)
```

## Como Usar

### 1. Iniciar a Máquina Virtual (se não estiver rodando)
```bash
./scripts/start_vm.sh
```

### 2. Testar Conectividade
```bash
ansible vm_qemu -m ping
```

### 3. Validar Sintaxe e Linting
```bash
ansible-playbook site.yml --syntax-check
ansible-lint site.yml roles/
```

### 4. Executar o Playbook Principal
```bash
ansible-playbook site.yml
```
