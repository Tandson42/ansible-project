variable "aws_region" {
  type        = string
  description = "Região AWS usada pelo provider"
  default     = "sa-east-1"
}
variable "aws_endpoint" {
  type        = string
  description = "Endpoint do MiniStack/LocalStack"
  default     = "http://localhost:4566"
}
variable "environment" {
  type        = string
  description = ""
  default     = "dev"
}