# Ansible Lab - Infraestrutura e Automação

Projeto Ansible organizado de acordo com as melhores práticas recomendadas pela Red Hat e comunidade Ansible, estruturado com **Roles**, **Inventory com Group Vars**, **Handlers** e **FQCN**.

A VM alvo é um QEMU local, provisionada com utilitários de SO, Docker Engine, ambiente gráfico XFCE e um Apache containerizado com um site estático de exemplo.

## Pré-requisitos

| Componente | Observação |
|---|---|
| QEMU | Precisa de KVM habilitado (`-enable-kvm`); exige virtualização aninhada ou host Linux |
| Python 3.14+ | Interpretador do `ansible-core` |
| `ansible-core` | Desenvolvido e testado com 2.21.4 |
| `community.docker` | Coleção usada pela role `docker` e `apache_docker` |

Instalar a coleção no mesmo ambiente do Ansible:

```bash
ansible-galaxy collection install community.docker
```

> Se `ansible-lint` instalado em um ambiente virtual separado acusar `couldn't resolve module/action 'community.docker...'`, é porque a coleção não está disponível naquele interpretador. O playbook em si funciona normalmente.

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
│       └── vm_qemu.yml              # Variáveis de conexão e become da VM
│
├── roles/
│   ├── common/                      # Mirror do apt, pacotes essenciais e serviço SSH
│   │   ├── tasks/main.yml
│   │   ├── handlers/main.yml
│   │   └── vars/main.yml
│   ├── apache/                      # Remove o Apache2 nativo do host (state: absent)
│   │   ├── tasks/main.yml
│   │   └── handlers/main.yml
│   ├── docker/                      # Docker Engine oficial via deb822_repository
│   │   ├── tasks/main.yml
│   │   ├── handlers/main.yml
│   │   └── vars/main.yml
│   ├── xfce/                        # Ambiente gráfico XFCE com LightDM
│   │   ├── tasks/main.yml
│   │   ├── handlers/main.yml
│   │   └── vars/main.yml
│   └── apache_docker/               # Apache containerizado + site estático
│       ├── tasks/main.yml
│       ├── docker-compose.yml       # Compose: bind mount do site + restart policy
│       └── app/
│           └── index.html           # Conteúdo servido pelo container
│
├── scripts/
│   └── start_vm.sh                  # Inicialização da VM QEMU com port forwarding
│
├── teste_inicial/                   # Scratchbook de estudo, fora do fluxo do site.yml
│   ├── ansible.cfg
│   ├── ansible_lab/
│   ├── playbook.yml
│   └── readme.md
│
└── vms/
    └── meu_disco.qcow2              # Imagem do disco QEMU (isolado do Git)
```

## Arquitetura: por que o Apache é removido do host

A role `apache` usa `state: absent` para garantir que o Apache nativo **nunca** seja instalado na VM:

```yaml
- name: Instalar apache2
  ansible.builtin.apt:
    name: apache2
    state: absent
```

Isso não é um resquício do lab — é o que elimina a dependência do host. O serviço web passa a existir apenas dentro do container, e a role continua sendo **idempotente**: mesmo numa VM recém-criada, o pacote não é instalado. Apagar a role seria pior, porque uma imagem nova poderia vir com o Apache2 presente.

Se você rodar na VM e ver isso, é o comportamento esperado:

```console
# systemctl status apache2
Unit apache2.service could not be found.
```

## Fluxo de rede

O Apache roda dentro do container, na porta `80`. A porta `8080` do container é publicada **na VM**, e a VM é uma QEMU com rede user-mode (SLIRP), que não é roteável. Para o host alcançar o site, o `scripts/start_vm.sh` encaminha a porta:

```bash
-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080 \
```

| Porta (host) | Destino | Finalidade |
|---|---|---|
| 2222 | VM `:22` | SSH, usado pelo Ansible |
| 8080 | VM `:8080` | Acesso ao site servido pelo container |

O `-netdev` só é lido no boot do QEMU. Depois de alterar o `start_vm.sh`, é preciso reiniciar a VM.

Alternativa sem reiniciar a VM:

```bash
ssh -p 2222 teste@127.0.0.1 -L 8080:localhost:8080
```

Para deploys, prefira `127.0.0.1` a `localhost` no `curl`: o túnel SSH escuta em `[::1]`, o QEMU em `0.0.0.0`, e `localhost` pode resolver para o endereço errado, mascarando o teste do encaminhamento.

## Como Usar

### 1. Iniciar a Máquina Virtual (se não estiver rodando)

```bash
./scripts/start_vm.sh
```

O compose usa `restart: unless-stopped`, então o container volta sozinho assim que o Docker sobe com a VM. Se o `start_vm.sh` foi alterado, o `-netdev` só é lido no boot — reinicie a VM para a mudança valer.

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

O playbook é idempotente: rodá-lo novamente deve resultar em `changed=0`.

### 5. Acessar o Site

```bash
curl http://127.0.0.1:8080/
```

Ou abra `http://127.0.0.1:8080` no navegador. O conteúdo vem de `roles/apache_docker/app/index.html`, copiado para `/opt/app/app/` na VM e montado em `/usr/local/apache2/htdocs/` pelo bind mount do compose.

Para alterar o site, edite `roles/apache_docker/app/index.html` e rode o playbook de novo. O bind mount faz o container enxergar a mudança sem recriar nada.

## Troubleshooting

**Site mostra "Index of /" em vez do conteúdo**

O bind mount do compose aponta para `./app` relativo ao `docker-compose.yml`, que precisa estar em `/opt/app`. Se o compose for copiado para outro lugar, ou se o diretório do site não existir, o Docker cria a pasta vazia e o httpd serve a listagem de diretório.

```bash
# conferir se o conteúdo chegou na VM
ls -Al /opt/app/app/
```

**Container com `Exited (255)` logo após subir a VM**

Falta a política de restart no compose. Sem ela o Docker até inicia, mas o container não sobe sozinho:

```bash
# conferir a política
docker inspect -f "{{.HostConfig.RestartPolicy.Name}}" meu-apache-compose
```

O esperado é `unless-stopped`. Alternativa imediata, sem editar nada: `ansible-playbook site.yml`.

**Container com `Exited (127)` e erro `not a directory` no mount**

Um arquivo está ocupando o lugar do diretório do site. A role remove esse resquício automaticamente, mas para inspecionar:

```bash
file /opt/app/app
```

**O módulo `copy` falha com `Could not find or access 'app/*'`**

O `copy` não expande glob. A resolução de caminho em `ansible/parsing/dataloader.py` usa `os.path.exists()`, e o `*` é tratado como caractere de nome, não como curinga. Use o caminho do diretório com barra final — `src: app/` — para que o conteúdo seja copiado para dentro do `dest`.
