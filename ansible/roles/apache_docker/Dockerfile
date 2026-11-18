# Imagem Apache + PHP com suporte a MySQL
FROM php:8.3-apache

RUN docker-php-ext-install pdo_mysql mysqli

# Copiando projeto para o DocumentRoot do Apache
COPY ./ /var/www/html/
