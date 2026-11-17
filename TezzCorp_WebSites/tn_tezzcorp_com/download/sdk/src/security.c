#include "native_abi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(TN_TLS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

/* Password records have one bounded, versioned format. Unsupported/legacy
 * hashes are rejected; there is no fast-hash authentication fallback. */
#define PASSWORD_PREFIX "pbkdf2-sha256$600000$"
#define PASSWORD_PREFIX_LEN (sizeof(PASSWORD_PREFIX)-1)
#define PASSWORD_RECORD_LEN (PASSWORD_PREFIX_LEN+32+1+64)
#define PASSWORD_MAX 1024

static void clear_secret(void* buffer, size_t length){
  volatile unsigned char* bytes=(volatile unsigned char*)buffer;
  while(length--) *bytes++=0;
}

static int bounded_length(const unsigned char* text, size_t maximum){
  if(!text) return -1;
  size_t length=0;
  while(length<=maximum && text[length]) length++;
  return length>maximum ? -1 : (int)length;
}

static int random_bytes(unsigned char* output, int length){
#ifdef _WIN32
  return BCryptGenRandom(NULL,output,(ULONG)length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
#elif defined(TN_TLS_OPENSSL)
  return RAND_bytes(output,length)==1;
#else
  (void)output; (void)length;
  return 0;
#endif
}

static int password_key(const unsigned char* password, int length,
                        const unsigned char salt[16], unsigned char output[32]){
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm=NULL;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,
                                 BCRYPT_ALG_HANDLE_HMAC_FLAG)!=0) return 0;
  NTSTATUS result=BCryptDeriveKeyPBKDF2(algorithm,(PUCHAR)password,(ULONG)length,
                                       (PUCHAR)salt,16,600000,output,32,0);
  BCryptCloseAlgorithmProvider(algorithm,0);
  return result==0;
#elif defined(TN_TLS_OPENSSL)
  return PKCS5_PBKDF2_HMAC((const char*)password,length,salt,16,600000,
                           EVP_sha256(),32,output)==1;
#else
  (void)password; (void)length; (void)salt; (void)output;
  return 0;
#endif
}

static int sha256(const unsigned char* input, int length, unsigned char output[32]){
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm=NULL;
  BCRYPT_HASH_HANDLE hash=NULL;
  int ok=0;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)!=0) return 0;
  if(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)==0){
    ok=BCryptHashData(hash,(PUCHAR)input,(ULONG)length,0)==0 &&
       BCryptFinishHash(hash,output,32,0)==0;
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(algorithm,0);
  return ok;
#elif defined(TN_TLS_OPENSSL)
  unsigned int written=0;
  return EVP_Digest(input,(size_t)length,output,&written,EVP_sha256(),NULL)==1 && written==32;
#else
  (void)input; (void)length; (void)output;
  return 0;
#endif
}

static void hex_encode(const unsigned char* bytes, size_t length, unsigned char* output){
  const char* digits="0123456789abcdef";
  for(size_t i=0;i<length;i++){
    output[2*i]=(unsigned char)digits[bytes[i]>>4];
    output[2*i+1]=(unsigned char)digits[bytes[i]&15];
  }
  output[2*length]=0;
}

static int hex_digit(unsigned char value){
  if(value>='0' && value<='9') return value-'0';
  if(value>='a' && value<='f') return value-'a'+10;
  return -1;
}

static int hex_decode(const unsigned char* text, size_t length, unsigned char* output){
  for(size_t i=0;i<length;i++){
    int high=hex_digit(text[2*i]),low=hex_digit(text[2*i+1]);
    if(high<0 || low<0) return 0;
    output[i]=(unsigned char)((high<<4)|low);
  }
  return 1;
}

TN_NATIVE_API unsigned char* tn_security_password_hash(const unsigned char* password){
  int length=bounded_length(password,PASSWORD_MAX);
  unsigned char salt[16],key[32];
  if(length<=0 || !random_bytes(salt,16)) return NULL;
  if(!password_key(password,length,salt,key)){ clear_secret(key,sizeof(key)); return NULL; }
  unsigned char* record=(unsigned char*)malloc(PASSWORD_RECORD_LEN+1);
  if(record){
    memcpy(record,PASSWORD_PREFIX,PASSWORD_PREFIX_LEN);
    hex_encode(salt,16,record+PASSWORD_PREFIX_LEN);
    record[PASSWORD_PREFIX_LEN+32]='$';
    hex_encode(key,32,record+PASSWORD_PREFIX_LEN+33);
  }
  clear_secret(key,sizeof(key));
  return record;
}

TN_NATIVE_API long long tn_security_password_verify(const unsigned char* password,
                                                     const unsigned char* record){
  int length=bounded_length(password,PASSWORD_MAX);
  if(length<=0 || bounded_length(record,PASSWORD_RECORD_LEN)!=PASSWORD_RECORD_LEN ||
     memcmp(record,PASSWORD_PREFIX,PASSWORD_PREFIX_LEN)!=0 ||
     record[PASSWORD_PREFIX_LEN+32]!='$') return 0;
  unsigned char salt[16],expected[32],actual[32];
  if(!hex_decode(record+PASSWORD_PREFIX_LEN,16,salt) ||
     !hex_decode(record+PASSWORD_PREFIX_LEN+33,32,expected)) return 0;
  if(!password_key(password,length,salt,actual)){ clear_secret(actual,sizeof(actual)); return 0; }
  unsigned int difference=0;
  for(size_t i=0;i<32;i++) difference|=(unsigned int)(expected[i]^actual[i]);
  clear_secret(actual,sizeof(actual));
  return difference==0;
}

TN_NATIVE_API unsigned char* tn_security_random_token(void){
  unsigned char bytes[32];
  if(!random_bytes(bytes,32)) return NULL;
  unsigned char* token=(unsigned char*)malloc(65);
  if(token) hex_encode(bytes,32,token);
  clear_secret(bytes,sizeof(bytes));
  return token;
}

TN_NATIVE_API unsigned char* tn_security_token_hash(const unsigned char* token){
  unsigned char bytes[32],digest[32];
  if(bounded_length(token,64)!=64 || !hex_decode(token,32,bytes)) return NULL;
  clear_secret(bytes,sizeof(bytes));
  if(!sha256(token,64,digest)) return NULL;
  unsigned char* hash=(unsigned char*)malloc(65);
  if(hash) hex_encode(digest,32,hash);
  clear_secret(digest,sizeof(digest));
  return hash;
}

/* Resolve existing paths before serving them. Symbolic links and Windows
 * reparse points must stay inside the selected document root. */
static char* public_canonical_path(const char* path){
#ifdef _WIN32
  HANDLE file=CreateFileA(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                          NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
  if(file==INVALID_HANDLE_VALUE) return NULL;
  DWORD length=GetFinalPathNameByHandleA(file,NULL,0,FILE_NAME_NORMALIZED);
  char* output=length && length<32768 ? (char*)malloc((size_t)length+1) : NULL;
  if(output){
    DWORD written=GetFinalPathNameByHandleA(file,output,length+1,FILE_NAME_NORMALIZED);
    if(!written || written>length){ free(output); output=NULL; }
  }
  CloseHandle(file);
  return output;
#else
  return realpath(path,NULL);
#endif
}

TN_NATIVE_API unsigned char* tn_security_public_path(const unsigned char* root,
                                                      const unsigned char* relative){
  int root_length=bounded_length(root,4096),path_length=bounded_length(relative,4096);
  if(root_length<=0 || path_length<=0 || relative[0]=='/' || relative[0]=='\\') return NULL;
  char* base=public_canonical_path((const char*)root);
  if(!base) return NULL;
  if(strcmp((const char*)relative,".")==0) return (unsigned char*)base;
  size_t base_length=strlen(base);
  char* joined=(char*)malloc(base_length+(size_t)path_length+2);
  if(!joined){ free(base); return NULL; }
  snprintf(joined,base_length+(size_t)path_length+2,"%s/%s",base,relative);
#ifdef _WIN32
  /* Extended Windows paths do not normalize forward slashes. */
  for(char* cursor=joined;*cursor;cursor++) if(*cursor=='/') *cursor='\\';
#endif
  char* path=public_canonical_path(joined);
  free(joined);
  while(base_length>1 && (base[base_length-1]=='/' || base[base_length-1]=='\\')) base_length--;
  int valid=path && strlen(path)>=base_length;
#ifdef _WIN32
  if(valid) valid=_strnicmp(base,path,base_length)==0;
#else
  if(valid) valid=strncmp(base,path,base_length)==0;
#endif
  if(valid && path[base_length] && path[base_length]!='/' && path[base_length]!='\\' &&
     !(base_length==1 && base[0]=='/')) valid=0;
  /* Hidden files/directories remain private even through an in-root symlink. */
  if(valid){
    const char* part=path+base_length;
    while(*part){
      if(*part=='/' || *part=='\\') part++;
      if(*part=='.'){ valid=0; break; }
      while(*part && *part!='/' && *part!='\\') part++;
    }
  }
  free(base);
  if(!valid){ free(path); return NULL; }
  return (unsigned char*)path;
}

#include "security_otp.h"
