#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(test) do { if(!(test)){ fprintf(stderr,"line %d: %s\n",__LINE__,#test); return 1; } } while(0)
int main(int argc,char** argv){
  REQUIRE(argc==2);
  double mel[32*80],loss=0,initial=0,prediction[100*80];
  int64_t labels[2]={3,4},alignment[32],pcm[20000];
  for(int t=0;t<32;t++){
    alignment[t]=t<16 ? 0 : 1;
    for(int m=0;m<80;m++) mel[t*80+m]=-10.0+4.0*sin(m*0.1)+(t<16 ? 1.0 : -1.0);
  }
  long long handle=tn_speech_tts_create(16,42);
  REQUIRE(handle>0);
  REQUIRE(tn_speech_tts_mel(handle,labels,2,prediction,100,1000000)==-1);
  REQUIRE(tn_speech_tts_save(handle,argv[1])==-1);
  for(int i=0;i<400;i++){
    REQUIRE(tn_speech_tts_train(handle,mel,32,labels,2,alignment,3000,&loss)==0);
    if(!i) initial=loss;
  }
  REQUIRE(loss<initial*0.01 && isfinite(loss));
  REQUIRE(tn_speech_tts_steps(handle)==400);
  long long frames=tn_speech_tts_mel(handle,labels,2,prediction,100,1000000);
  REQUIRE(frames>=24 && frames<=40);
  REQUIRE(tn_speech_tts_save(handle,argv[1])==0);
  long long loaded=tn_speech_tts_load(argv[1]);
  REQUIRE(loaded>0 && loaded!=handle && tn_speech_tts_steps(loaded)==400);
  double copy[100*80];
  REQUIRE(tn_speech_tts_mel(loaded,labels,2,copy,100,1000000)==frames);
  REQUIRE(memcmp(copy,prediction,(size_t)frames*80*sizeof(double))==0);
  long long samples=tn_speech_tts_pcm(loaded,labels,2,pcm,20000,1000000,1000000,1000000);
  REQUIRE(samples==(frames-1)*160+400);
  long long peak=0;
  for(int i=0;i<samples;i++){ REQUIRE(pcm[i]>=-32768 && pcm[i]<=32767); if(llabs(pcm[i])>peak) peak=llabs(pcm[i]); }
  REQUIRE(peak>0);
  REQUIRE(tn_speech_tts_pcm(loaded,labels,2,pcm,20000,1000000,0,1000000)==-1);
  alignment[31]=0;
  REQUIRE(tn_speech_tts_train(handle,mel,32,labels,2,alignment,3000,&loss)==-1);
  REQUIRE(tn_speech_tts_steps(handle)==400);
  REQUIRE(tn_speech_tts_free(handle)==0 && tn_speech_tts_free(handle)==-1);
  REQUIRE(tn_speech_tts_free(loaded)==0);
  FILE* file=fopen(argv[1],"r+b"); REQUIRE(file);
  REQUIRE(fseek(file,100,SEEK_SET)==0); int value=fgetc(file); REQUIRE(value!=EOF);
  REQUIRE(fseek(file,100,SEEK_SET)==0 && fputc(value^1,file)!=EOF && fclose(file)==0);
  REQUIRE(tn_speech_tts_load(argv[1])==0);
  printf("tts_synthetic_initial %.9g final %.9g frames %lld samples %lld peak %lld checkpoint_pass\n",initial,loss,frames,samples,peak);
  return 0;
}
