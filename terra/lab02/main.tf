terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "6.36.0"
    }
    random = {
      source  = "hashicorp/random"
      version = "3.8.1"
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
    s3       = var.aws_endpoint
    dynamodb = var.aws_endpoint
    lambda   = var.aws_endpoint
    ec2      = var.aws_endpoint
  }
}
