#pragma once
#include <openssl/sha.h>
typedef SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context*c){(void)c;}
static inline void mbedtls_sha256_free(mbedtls_sha256_context*c){(void)c;}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context*c,int n){(void)n;return SHA256_Init(c)?0:-1;}
static inline int mbedtls_sha256_update(mbedtls_sha256_context*c,const void*b,unsigned long n){return SHA256_Update(c,b,n)?0:-1;}
static inline int mbedtls_sha256_finish(mbedtls_sha256_context*c,unsigned char*b){return SHA256_Final(b,c)?0:-1;}
