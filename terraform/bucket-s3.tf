provider "aws" {
  access_key                  = "test"
  secret_key                  = "test"
  region                      = "sa-east-1"
  skip_credentials_validation = true
  skip_metadata_api_check     = true
  skip_requesting_account_id  = true

  endpoints {
    s3 = "http://localhost:4566"
  }
}