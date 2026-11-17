#include "speech.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if(!(value)){ fprintf(stderr,"line%d: %s\n",__LINE__,#value); return 1; } } while(0)
int main(void){
  double mel[4*80]; int64_t pcm[880], again[880];
  for(int i=0;i<4*80;i++) mel[i]=-10;
  for(int i=0;i<880;i++) pcm[i]=123456;
  CHECK(speech_vocode(NULL,4,pcm,880,1,1)==-1);
  CHECK(speech_vocode(mel,4,NULL,880,1,1)==-1);
  CHECK(speech_vocode(mel,0,pcm,880,1,1)==-1);
  CHECK(speech_vocode(mel,3001,pcm,880,1,1)==-1);
  CHECK(speech_vocode(mel,4,pcm,879,1,1)==-1);
  CHECK(speech_vocode(mel,4,pcm,880,NAN,1)==-1);
  CHECK(speech_vocode(mel,4,pcm,880,1,INFINITY)==-1);
  CHECK(speech_vocode(mel,4,pcm,880,0.09,1)==-1);
  CHECK(speech_vocode(mel,4,pcm,880,1,-1)==-1);
  mel[4*80-1]=NAN;
  CHECK(speech_vocode(mel,4,pcm,880,1,1)==-1);
  for(int i=0;i<880;i++) CHECK(pcm[i]==123456);
  mel[4*80-1]=-10;
  CHECK(speech_vocode(mel,4,pcm,880,1,1)==880);
  CHECK(speech_vocode(mel,4,again,880,1,1)==880);
  CHECK(memcmp(pcm,again,sizeof(pcm))==0);
  CHECK(speech_vocode(mel,4,pcm,880,1,0)==880);
  for(int i=0;i<880;i++) CHECK(pcm[i]==0);
  puts("VOCODER_BOUNDS deterministic, rejection atomicity, zero-energy pass");
  return 0;
}
