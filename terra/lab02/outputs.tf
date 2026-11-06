output "bucket_name" {
  value = aws_s3_bucket.this.bucket
}

output "bucket_arn" {
  value = aws_s3_bucket.this.bucket
}

output "ips_file_path" {
  value = "${aws_s3_object.this.bucket}/${aws_s3_object.this.key}"
}