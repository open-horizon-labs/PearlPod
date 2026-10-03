#pragma once
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *c){(void)c;}
static inline void mbedtls_sha256_starts(mbedtls_sha256_context *c,int bits){(void)bits;CC_SHA256_Init(c);}
static inline void mbedtls_sha256_update(mbedtls_sha256_context *c,const void *p,size_t n){CC_SHA256_Update(c,p,(CC_LONG)n);}
static inline void mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *out){CC_SHA256_Final(out,c);}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
