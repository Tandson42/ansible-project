terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "6.36.0"
    }
  }
}
provider "aws" {
  region                      = var.aws_region
  access_key                  = "test"
  secret_key                  = "test"
  skip_credentials_validation = true
  skip_metadata_api_check     = true
  skip_requesting_account_id  = true
  endpoints {
    s3       = "http://localhost:4566"
    dynamodb = "http://localhost:4566"
    lambda   = "http://localhost:4566"
    ec2      = "http://localhost:4566"
  }
}
resource "aws_s3_bucket" "teste-de-bucket" {
  bucket = "tand-bk"

  tags = var.tags
}
resource "aws_ami" "local_ami" {
  name                = "minha-ami-terraform"
  root_device_name    = var.ebs_ami.device
  virtualization_type = "hvm"

  ebs_block_device {
    device_name = var.ebs_ami.device
    snapshot_id = var.ebs_ami.snaphsot_id # ID fictício
  }
}
resource "aws_instance" "web" {
  ami           = var.ami_id
  instance_type = var.instance_type
  tags          = var.instance_tags
}
