/* Included by security.c: RFC 4226 HMAC-SHA1, RFC 6238 and RFC 4648. */
#include <stdint.h>
#include <limits.h>
#if !defined(_WIN32) && defined(TN_TLS_OPENSSL)
#include <openssl/hmac.h>
#endif

static int otp_base32_digit(unsigned char value){
  if(value>='a' && value<='z') value=(unsigned char)(value-'a'+'A');
  if(value>='A' && value<='Z') return value-'A';
  if(value>='2' && value<='7') return value-'2'+26;
  return -1;
}

static int otp_decode(const unsigned char* secret, unsigned char key[320]){
  int length=bounded_length(secret,512),bits=0,used=0,padding=0,symbols=0;
  unsigned int buffer=0;
  if(length<=0) return -1;
  for(int i=0;i<length;i++){
    if(secret[i]=='='){ padding++; continue; }
    int value=otp_base32_digit(secret[i]);
    if(padding || value<0) return -1;
    symbols++;
    buffer=(buffer<<5)|(unsigned int)value;
    bits+=5;
    if(bits>=8){
      bits-=8;
      key[used++]=(unsigned char)(buffer>>bits);
      buffer&=(1u<<bits)-1;
    }
  }
  int residue=symbols%8;
  if(residue==1 || residue==3 || residue==6 || buffer!=0 || used<16) return -1;
  if(padding && (length%8!=0 || padding!=(8-residue)%8)) return -1;
  return used;
}

static int otp_hmac(const unsigned char* key, int length,
                    const unsigned char counter[8], unsigned char output[20]){
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm=NULL;
  BCRYPT_HASH_HANDLE hash=NULL;
  int ok=0;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA1_ALGORITHM,NULL,
                                 BCRYPT_ALG_HANDLE_HMAC_FLAG)!=0) return 0;
  if(BCryptCreateHash(algorithm,&hash,NULL,0,(PUCHAR)key,(ULONG)length,0)==0){
    ok=BCryptHashData(hash,(PUCHAR)counter,8,0)==0 &&
       BCryptFinishHash(hash,output,20,0)==0;
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(algorithm,0);
  return ok;
#elif defined(TN_TLS_OPENSSL)
  unsigned int written=0;
  return HMAC(EVP_sha1(),key,length,counter,8,output,&written)!=NULL && written==20;
#else
  (void)key; (void)length; (void)counter; (void)output;
  return 0;
#endif
}

static int otp_code(const unsigned char* key, int length, uint64_t counter,
                    int digits, unsigned char output[9]){
  unsigned char moving[8],digest[20];
  for(int i=7;i>=0;i--){ moving[i]=(unsigned char)counter; counter>>=8; }
  if(!otp_hmac(key,length,moving,digest)){ clear_secret(digest,sizeof(digest)); return 0; }
  unsigned int offset=digest[19]&15;
  uint32_t value=((uint32_t)(digest[offset]&127)<<24)|
                 ((uint32_t)digest[offset+1]<<16)|((uint32_t)digest[offset+2]<<8)|
                 digest[offset+3];
  uint32_t divisor=digits==6 ? 1000000u : digits==7 ? 10000000u : 100000000u;
  value%=divisor;
  for(int i=digits-1;i>=0;i--){ output[i]=(unsigned char)('0'+value%10); value/=10; }
  output[digits]=0;
  clear_secret(digest,sizeof(digest));
  return 1;
}

TN_NATIVE_API unsigned char* tn_security_otp_secret(void){
  static const unsigned char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  unsigned char bytes[20];
  if(!random_bytes(bytes,20)) return NULL;
  unsigned char* output=(unsigned char*)malloc(33);
  if(output){
    unsigned int buffer=0; int bits=0,used=0;
    for(int i=0;i<20;i++){
      buffer=(buffer<<8)|bytes[i]; bits+=8;
      while(bits>=5){ bits-=5; output[used++]=alphabet[(buffer>>bits)&31]; }
    }
    output[32]=0;
  }
  clear_secret(bytes,sizeof(bytes));
  return output;
}

TN_NATIVE_API long long tn_security_otp_random_u32(void){
  uint32_t result;
  if(!random_bytes((unsigned char*)&result,sizeof(result))) return -1;
  return (long long)result;
}

TN_NATIVE_API unsigned char* tn_security_otp_numeric(long long length){
  if(length<1 || length>128) return NULL;
  unsigned char* output=(unsigned char*)malloc((size_t)length+1);
  if(!output) return NULL;
  unsigned char bytes[32];
  int used=0;
  while(used<length){
    if(!random_bytes(bytes,sizeof(bytes))){
      clear_secret(bytes,sizeof(bytes)); clear_secret(output,(size_t)length); free(output); return NULL;
    }
    for(size_t i=0;i<sizeof(bytes) && used<length;i++){
      if(bytes[i]<250) output[used++]=(unsigned char)('0'+bytes[i]%10);
    }
  }
  output[length]=0;
  clear_secret(bytes,sizeof(bytes));
  return output;
}

TN_NATIVE_API unsigned char* tn_security_otp_hotp(const unsigned char* secret,
                                                long long counter, long long digits){
  unsigned char key[320],code[9];
  int length=otp_decode(secret,key);
  if(length<0 || counter<0 || digits<6 || digits>8){ clear_secret(key,sizeof(key)); return NULL; }
  int ok=otp_code(key,length,(uint64_t)counter,(int)digits,code);
  clear_secret(key,sizeof(key));
  if(!ok) return NULL;
  unsigned char* output=(unsigned char*)malloc((size_t)digits+1);
  if(output) memcpy(output,code,(size_t)digits+1);
  clear_secret(code,sizeof(code));
  return output;
}

/* Returns the matched moving factor, or -1. Applications must atomically
 * compare/store it per identity to reject replay; this primitive is stateless. */
TN_NATIVE_API long long tn_security_otp_match(const unsigned char* secret,
                                             const unsigned char* supplied,
                                             long long counter, long long window,
                                             long long digits){
  unsigned char key[320],candidate[9];
  int length=otp_decode(secret,key);
  if(length<0 || counter<0 || window<0 || window>10 || digits<6 || digits>8 ||
     bounded_length(supplied,8)!=digits){ clear_secret(key,sizeof(key)); return -1; }
  unsigned int malformed=0;
  for(int i=0;i<digits;i++) malformed|=(unsigned int)(supplied[i]<'0' || supplied[i]>'9');
  long long matched=-1;
  for(long long delta=-window;delta<=window;delta++){
    if((delta<0 && counter<-delta) || (delta>0 && counter>LLONG_MAX-delta)) continue;
    long long moving=counter+delta;
    if(!otp_code(key,length,(uint64_t)moving,(int)digits,candidate)){
      clear_secret(key,sizeof(key)); clear_secret(candidate,sizeof(candidate)); return -1;
    }
    unsigned int difference=malformed;
    for(int i=0;i<digits;i++) difference|=(unsigned int)(candidate[i]^supplied[i]);
    if(difference==0) matched=moving;
  }
  clear_secret(key,sizeof(key)); clear_secret(candidate,sizeof(candidate));
  return matched;
}

static unsigned char* otp_uri_component(unsigned char* output, const unsigned char* input){
  const char* hex="0123456789ABCDEF";
  for(;*input;input++){
    unsigned char c=*input;
    if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
       c=='-' || c=='.' || c=='_' || c=='~') *output++=c;
    else { *output++='%'; *output++=(unsigned char)hex[c>>4]; *output++=(unsigned char)hex[c&15]; }
  }
  return output;
}

TN_NATIVE_API unsigned char* tn_security_otp_uri(const unsigned char* secret,
                                               const unsigned char* account,
                                               const unsigned char* issuer){
  unsigned char key[320];
  int key_length=otp_decode(secret,key);
  clear_secret(key,sizeof(key));
  int a=bounded_length(account,1024),b=bounded_length(issuer,1024);
  if(key_length<0 || a<=0 || b<=0) return NULL;
  int secret_length=bounded_length(secret,512);
  unsigned char* output=(unsigned char*)malloc(3*(size_t)(a+2*b+secret_length)+128);
  if(!output) return NULL;
  unsigned char* cursor=output;
#define OTP_URI_TEXT(text) do { memcpy(cursor,text,sizeof(text)-1); cursor+=sizeof(text)-1; } while(0)
  OTP_URI_TEXT("otpauth://totp/");
  cursor=otp_uri_component(cursor,issuer);
  *cursor++=':';
  cursor=otp_uri_component(cursor,account);
  OTP_URI_TEXT("?secret=");
  cursor=otp_uri_component(cursor,secret);
  OTP_URI_TEXT("&issuer=");
  cursor=otp_uri_component(cursor,issuer);
  OTP_URI_TEXT("&algorithm=SHA1&digits=6&period=30");
  *cursor=0;
#undef OTP_URI_TEXT
  return output;
}
