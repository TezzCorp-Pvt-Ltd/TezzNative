#pragma once

static int tn_tls_pin_equal(const unsigned char* left, const unsigned char* right){
  unsigned int difference=0;
  for(int i=0;i<32;i++) difference|=(unsigned int)(left[i]^right[i]);
  return difference==0;
}

#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>

static int tn_tls_verify_context(SSL_CTX* context){
  SSL_CTX_set_verify(context,SSL_VERIFY_PEER,NULL);
  return SSL_CTX_set_default_verify_paths(context)==1;
}

static int tn_tls_verify_hostname(SSL* connection, const char* host){
  if(!host || !host[0]) return 0;
  unsigned char address[16];
  X509_VERIFY_PARAM* parameters=SSL_get0_param(connection);
  SSL_set_hostflags(connection,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  if(inet_pton(AF_INET,host,address)==1 || inet_pton(AF_INET6,host,address)==1){
    return X509_VERIFY_PARAM_set1_ip_asc(parameters,host)==1;
  }
  return SSL_set1_host(connection,host)==1 && SSL_set_tlsext_host_name(connection,host)==1;
}
#endif
