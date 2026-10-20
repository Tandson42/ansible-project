# Pipeline de CD: Terraform (libvirt/KVM) cria as VMs e o Ansible provisiona.
#
# Fluxo completo:   make deploy
#   vm-apply -> inventory gerado -> wait -> provision (site.yml)

TF_DIR       := terraform/kvm
INVENTORY    := inventory/generated.ini
ANSIBLE_ARGS := -i $(INVENTORY)

.DEFAULT_GOAL := help
.PHONY: help init plan apply wait provision deploy destroy console fmt validate

help: ## Mostra esta ajuda
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

init: ## terraform init (baixa providers)
	terraform -chdir=$(TF_DIR) init -upgrade

plan: init ## Mostra o plano das VMs
	terraform -chdir=$(TF_DIR) plan

apply: init ## Cria/atualiza as VMs e gera o inventario Ansible
	terraform -chdir=$(TF_DIR) apply -auto-approve

wait: ## Espera as VMs aceitarem SSH
	ansible-playbook wait.yml $(ANSIBLE_ARGS)

provision: ## Provisiona as VMs com Ansible (site.yml)
	ansible-playbook site.yml $(ANSIBLE_ARGS)

deploy: apply wait provision ## CD completo: cria as VMs e provisiona

destroy: ## Destroi as VMs criadas pelo Terraform
	terraform -chdir=$(TF_DIR) destroy -auto-approve

console: ## Abre o console serial (TTY) da VM. Ex.: make console VM=lab-vm-1
	virsh -c qemu:///system console $(VM)

fmt: ## Formata os .tf
	terraform -chdir=$(TF_DIR) fmt -recursive

validate: ## Valida a configuracao Terraform
	terraform -chdir=$(TF_DIR) validate
