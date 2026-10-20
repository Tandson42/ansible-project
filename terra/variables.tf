variable "aws_region" {
  type        = string
  description = ""
  default     = "sa-east-1"
}

variable "instance_type" {
  type        = string
  description = ""
  #default     = "t3.micro"
}

variable "ami_id" {
  type        = string
  description = "AMI ID para instanciar no MiniStack"
  default     = "ami-00000001"
}
variable "snaphsot_id" {
  type    = string
  default = "snap-1234567890abcdef0"
}
variable "ebs_ami" {
  type = map(string)
  default = {
    snaphsot_id = "snap-1234567890abcdef0"
    device      = "/dev/xvda"
  }
}
variable "instance_tags" {
  type        = map(string)
  description = ""
  default = {
    Name    = "Ubuntu"
    Project = "Curso AWS com terraform"
  }
}

variable "tags" {
  type        = map(string)
  description = "tags usadas para o bucket"
  default = {
    Name        = "My bucket"
    Environment = "Dev"
    Managedby   = "Terraform"
    Owner       = "Tand"
  }
}
