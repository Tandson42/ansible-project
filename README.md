# Ansible Lab - Infraestrutura e Automação

[![Test Ansible](https://github.com/Tandson42/ansible-project/actions/workflows/test.yml/badge.svg)](https://github.com/Tandson42/ansible-project/actions/workflows/test.yml)

Projeto de automação de uma VM QEMU local com **Ansible**, além de laboratórios de **Terraform** usando o MiniStack/LocalStack. O playbook provisiona utilitários de SO, Docker Engine, ambiente gráfico XFCE e aplicações containerizadas (Apache, Nginx e MySQL).

A VM alvo é um QEMU local com rede user-mode (SLIRP); o acesso do host é feito por port forwarding configurado em `scripts/start_vm.sh`.

## Pré-requisitos

| Componente | Observação |
|---|---|
| QEMU | Precisa de KVM habilitado (`-enable-kvm`); exige virtualização aninhada ou host Linux |
| Python 3.14+ | Interpretador do `ansible-core` |
| `ansible-core` | Desenvolvido e testado com 2.21.4 |
| `ansible-lint` | Lint e formatação; testado com 26.10.1 |
| `community.docker` | Coleção usada pelas roles `docker`, `apache_docker`, `nginx`, `mysql` e `healthcheck` |
| Terraform | Opcional; usado apenas nos laboratórios em `terra/` |
| Docker | Opcional; necessário para executar o MiniStack/LocalStack |

Instalar as ferramentas e a coleção no mesmo ambiente:

```bash
python -m pip install ansible-core==2.21.4 ansible-lint==26.10.1
ansible-galaxy collection install community.docker
```

> Se o `ansible-lint` estiver em um ambiente virtual separado e acusar `couldn't resolve module/action 'community.docker...'`, instale a coleção usando o `ansible-galaxy` **do mesmo ambiente** do lint. Para o lint, a coleção precisa estar visível no `ANSIBLE_COLLECTIONS_PATH` (por padrão, `~/.ansible/collections`). O playbook em si funciona normalmente.

## Estrutura do Projeto

```text
.
├── ansible.cfg                      # Configurações globais (inventory, roles_path)
├── .ansible-lint                    # Perfil do ansible-lint (production)
├── .gitignore                       # Ignora discos pesados (*.qcow2), caches, logs e estados
├── README.md                        # Documentação do projeto
├── site.yml                         # Playbook mestre (orquestrador)
├── destroyer.yml                    # Remove containers, volumes e pacotes provisionados
│
├── .github/
│   └── workflows/
│       ├── test.yml                 # CI: syntax-check + ansible-lint + formatação
│       └── publish-apache-image.yml # CI: build e push da imagem Apache no GHCR
│
├── inventory/
│   ├── hosts.ini                    # Inventário de hosts
│   └── group_vars/
│       └── vm_qemu.yml              # Variáveis de conexão e become da VM
│
├── roles/
│   ├── common/                      # Mirror do apt, pacotes essenciais e serviço SSH
│   ├── apache/                      # Garante o Apache2 nativo ausente (desativada no site.yml)
│   ├── docker/                      # Docker Engine oficial via deb822_repository
│   ├── xfce/                        # Ambiente gráfico XFCE com LightDM
│   ├── apache_docker/               # Imagem Apache customizada + site estático
│   │   ├── Dockerfile
│   │   ├── app/                     # index.html, style.css e script.js
│   │   ├── docker-compose.yml       # Compose para execução manual
│   │   ├── handlers/main.yml
│   │   └── tasks/main.yml
│   ├── nginx/                       # Container Nginx na porta 8081
│   ├── mysql/                       # MySQL containerizado via docker compose
│   └── healthcheck/                 # Health check das portas 8080 e 8081
│
├── scripts/
│   ├── start_vm.sh                  # Inicialização da VM QEMU com port forwarding
│   └── recreate.sh                  # Recria o disco qcow2 a partir da imagem base
│
├── terra/                           # Laboratórios de Terraform (AWS via MiniStack)
│   ├── lab01/                       # Bucket S3, AMI e instância EC2
│   │   ├── main.tf
│   │   ├── variables.tf
│   │   └── terraform.tfvars         # Fora do Git (*.tfvars)
│   └── lab02/                       # Bucket S3 com nome gerado por random_pet
│       ├── main.tf
│       ├── random.tf
│       ├── s3.tf
│       ├── variables.tf
│       └── terraform.tfvars         # Fora do Git (*.tfvars)
│
├── teste_inicial/                   # Scratchbook de estudo, fora do fluxo do site.yml
│   ├── ansible.cfg
│   ├── ansible_lab/
│   ├── playbook.yml
│   └── readme.md
│
└── vms/                             # (fora do Git) imagens e discos da VM
    ├── resolute-server-cloudimg-amd64.img
    ├── seed.img
    ├── user-data.yaml
    ├── vm1.qcow2
    └── vm2.qcow2
```

## Arquitetura

### Apache nativo x Apache containerizado

A role `apache` mantém o Apache nativo **ausente** na VM:

```yaml
- name: Instalar apache2
  ansible.builtin.apt:
    name: apache2
    state: absent
```

Ela está comentada no `site.yml`, mas continua disponível para garantir que uma imagem nova não traga o Apache2 instalado. O serviço web usado pelo projeto é o container criado pela role `apache_docker`.

Se você rodar na VM e ver isso, é o comportamento esperado:

```console
# systemctl status apache2
Unit apache2.service could not be found.
```

### Containers provisionados

| Container | Imagem | Porta na VM | Origem |
|---|---|---|---|
| `web-server` | `ghcr.io/tandson42/ansible-project/apache:latest` | `8080` | Role `apache_docker` |
| `web-server-nginx` | `nginx:stable-alpine3.24-slim` | `8081` | Role `nginx` |
| `mysql` | `mysql:latest` | `3306` | Role `mysql` (docker compose em `/opt/mysql`) |

A imagem Apache é construída a partir de `roles/apache_docker/Dockerfile` e publicada no GitHub Container Registry pelo workflow `publish-apache-image.yml`. A role baixa a imagem e o handler recria o container `web-server`.

## Fluxo de rede

A VM usa rede user-mode (SLIRP), que não é roteável. Para o host alcançar os serviços, o `scripts/start_vm.sh` encaminha as portas:

```bash
-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080,hostfwd=tcp::8081-:8081,hostfwd=tcp::3306-:3306 \
```

| Porta (host) | Destino | Finalidade |
|---|---|---|
| 2222 | VM `:22` | SSH, usado pelo Ansible |
| 8080 | VM `:8080` | Apache containerizado |
| 8081 | VM `:8081` | Nginx containerizado |
| 3306 | VM `:3306` | MySQL containerizado |

O `-netdev` só é lido no boot do QEMU. Depois de alterar o `start_vm.sh`, é preciso reiniciar a VM.

Alternativa sem reiniciar a VM:

```bash
ssh -p 2222 ubuntu@127.0.0.1 -L 8080:localhost:8080 -L 8081:localhost:8081 -L 3306:localhost:3306
```

Para deploys, prefira `127.0.0.1` a `localhost` no `curl`: o túnel SSH escuta em `[::1]`, o QEMU em `0.0.0.0`, e `localhost` pode resolver para o endereço errado, mascarando o teste do encaminhamento.

## Como Usar

### 1. Iniciar a Máquina Virtual (se não estiver rodando)

```bash
./scripts/start_vm.sh
```

O script usa `vms/vm2.qcow2` e mantém o QEMU em primeiro plano. Para recriar um disco a partir da imagem base:

```bash
./scripts/recreate.sh vm3
```

> O `recreate.sh` recria o disco de destino com 20 GB a partir de `vms/resolute-server-cloudimg-amd64.img` e **apaga** o disco existente com o mesmo nome.

Os containers usam `restart: unless-stopped`/`restart_policy: unless-stopped`, então voltam sozinhos quando o Docker inicia com a VM. Se o `start_vm.sh` foi alterado, reinicie a VM para o `-netdev` valer.

### 2. Testar Conectividade

```bash
ansible vm_qemu -m ping
```

### 3. Validar Sintaxe, Linting e Formatação

```bash
ansible-playbook site.yml --syntax-check
ansible-lint site.yml roles/          # não altera arquivos
ansible-lint --fix site.yml roles/    # aplica o padrão
```

> `ansible-lint --fix` é idempotente: rodar duas vezes seguidas não produz alterações.

### 4. Executar o Playbook Principal

```bash
ansible-playbook site.yml
```

O playbook foi escrito para ser idempotente. Como as roles `apache_docker` e `nginx` usam `force_source: true` para baixar a imagem mais recente, o pull pode aparecer como `changed` em algumas execuções; o restante das tarefas deve permanecer estável.

### 5. Acessar os Serviços

Apache:

```bash
curl http://127.0.0.1:8080/
```

Ou abra `http://127.0.0.1:8080` no navegador. O conteúdo vem de `roles/apache_docker/app/` e é copiado para `/usr/local/apache2/htdocs/` durante o build da imagem.

Nginx:

```bash
curl http://127.0.0.1:8081/
```

MySQL:

```bash
mysql -h 127.0.0.1 -P 3306 -u tand -p
```

O banco padrão é `banco_do_tand` e a senha de exemplo é `123` (definida em `roles/mysql/files/docker-compose.yml`).

### 6. Desfazer a Infraestrutura

```bash
ansible-playbook destroyer.yml
```

O `destroyer.yml` remove os containers/volumes do MySQL, o container e a imagem Apache, os pacotes do XFCE, os componentes do Docker e os pacotes essenciais instalados pelo projeto.

## Formatação padrão

O padrão de formatação é o do próprio `ansible-lint`, definido em [`.ansible-lint`](.ansible-lint) com o profile mais estrito:

```yaml
profile: production
```

Aplicar o padrão em todo o projeto:

```bash
ansible-lint --fix site.yml roles/
```

Conferir sem alterar arquivos (é o que o CI faz):

```bash
ansible-lint site.yml roles/
```

O `--fix` normaliza, entre outras coisas:

| Item | Exemplo |
|---|---|
| Newline no fim do arquivo | garante exatamente um `\n` |
| Linhas em branco no fim | remove as que sobram |
| Documento YAML | adiciona `---` no topo quando ausente |
| Aspas | `mode: '0755'` → `mode: "0755"` |

As mudanças são apenas de formatação — não alteram a semântica do playbook. Rode o `--fix` antes de abrir o PR: o CI falha se houver arquivo fora do padrão.

## Integração Contínua (CI)

O workflow [`.github/workflows/test.yml`](.github/workflows/test.yml) roda em todo `push` e `pull_request`:

| Passo | Comando |
|---|---|
| Sintaxe | `ansible-playbook site.yml --syntax-check` |
| Lint | `ansible-lint site.yml roles/` |
| Formatação | `ansible-lint --fix site.yml roles/` + `git diff --exit-code` |

O passo de formatação aplica o auto-fix e falha se o `git diff` acusar qualquer alteração — ou seja, o repositório precisa estar formatado antes do merge. Para corrigir localmente:

```bash
ansible-lint --fix site.yml roles/
git add -A
git commit -m "style: aplica formatacao padrao do Ansible"
```

As versões são fixadas no workflow para o resultado ser determinístico: `ansible-core==2.21.4` e `ansible-lint==26.10.1`. Fixar o `ansible-lint` é importante porque versões diferentes podem reformatar os arquivos de forma diferente.

O workflow [`.github/workflows/publish-apache-image.yml`](.github/workflows/publish-apache-image.yml) roda em `push` na `main` quando algo em `roles/apache_docker/**` muda (ou via `workflow_dispatch`). Ele constrói `roles/apache_docker/Dockerfile` e publica a imagem `ghcr.io/tandson42/ansible-project/apache` com as tags `latest` e `sha-<commit>`.

## Terraform (MiniStack/LocalStack)

Os laboratórios em `terra/` usam o MiniStack como emulador local da AWS. O provider está configurado com credenciais fictícias (`test`/`test`) e as flags necessárias para dispensar validação real:

```hcl
skip_credentials_validation = true
skip_metadata_api_check     = true
skip_requesting_account_id  = true
```

O endpoint é configurável:

- **lab01**: endpoints fixos em `http://localhost:4566`
- **lab02**: usa `var.aws_endpoint` (default `http://localhost:4566`); pode ser sobrescrito no `terraform.tfvars`, que é ignorado pelo Git

Recursos por laboratório:

| Lab | Recursos |
|---|---|
| `lab01` | `aws_s3_bucket.teste-de-bucket`, `aws_ami.local_ami` e `aws_instance.web` |
| `lab02` | `random_pet.bucket` e `aws_s3_bucket.this` (nome `"${random_pet.bucket.id}-${var.environment}"`) |

Executar um laboratório:

```bash
cd terra/lab02
terraform init
terraform plan
terraform apply
```

Para subir o MiniStack com a porta publicada no host:

```bash
docker run -d --name ministack -p 4566:4566 ministackorg/ministack
```

Se o container estiver acessível apenas pelo IP da rede Docker (por exemplo, `172.17.0.2`), ajuste o endpoint:

```hcl
# terra/lab02/terraform.tfvars (ignorado pelo Git)
aws_endpoint = "http://172.17.0.2:4566"
```

> O estado do Terraform (`terraform.tfstate`) e os arquivos `*.tfvars` são ignorados pelo Git. O `.terraform.lock.hcl` é versionado para manter as versões dos providers determinísticas.

## Troubleshooting

**As portas 8080/8081/3306 não respondem**

Confira se a VM está rodando com o port forwarding correto e se os containers subiram:

```bash
docker ps --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
```

Se o `start_vm.sh` foi alterado, reinicie a VM para aplicar o novo `-netdev`. O playbook `site.yml` também valida as portas 8080 e 8081 na role `healthcheck`.

**Container com `Exited (255)` logo após subir a VM**

Falta a política de restart no container. O esperado é `unless-stopped`:

```bash
docker inspect -f "{{.HostConfig.RestartPolicy.Name}}" web-server
docker inspect -f "{{.HostConfig.RestartPolicy.Name}}" web-server-nginx
```

Alternativa imediata, sem editar nada: `ansible-playbook site.yml`.

**Site mostra "Index of /" em vez do conteúdo**

A imagem Apache pode ter sido publicada sem o conteúdo de `roles/apache_docker/app/`. Confira dentro do container:

```bash
docker exec web-server ls -Al /usr/local/apache2/htdocs/
```

Se a pasta estiver vazia, reconstrua/rebuilde a imagem (ou rode novamente o workflow `publish-apache-image.yml`) e execute o playbook para baixar a versão atualizada.

**`Bind for 0.0.0.0:8080 failed: port is already allocated`**

Já existe outro processo/container usando a porta no host. Identifique e pare o conflito:

```bash
ss -ltnp | grep -E '8080|8081|3306'
```

**MiniStack não responde em `localhost:4566`**

O container precisa publicar a porta (`-p 4566:4566`). Se ele só estiver acessível pelo IP da rede Docker, use esse IP no endpoint do Terraform:

```bash
docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' ministack
```
