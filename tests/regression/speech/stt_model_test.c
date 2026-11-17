#include "speech.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char** argv){
  if(argc!=2) return 99;
  double mel[8*TN_SPEECH_MELS];
  for(int t=0;t<8;t++) for(int m=0;m<TN_SPEECH_MELS;m++) mel[t*TN_SPEECH_MELS+m]=-10.0;
  for(int t=1;t<=3;t++) mel[t*TN_SPEECH_MELS]=20.0;
  for(int t=5;t<=7;t++) mel[t*TN_SPEECH_MELS+1]=20.0;
  int64_t labels[2]={3,4};
  long long handle=tn_speech_stt_create(16,0,42);
  if(!handle) return 1;
  double initial,final;
  if(tn_speech_stt_loss(handle,mel,8,labels,2,&initial)) return 2;
  char before[32];
  if(tn_speech_stt_predict(handle,mel,8,before,sizeof(before))!=-1) return 3;
  if(tn_speech_stt_save(handle,argv[1])!=-1) return 4;
  for(int i=0;i<300;i++) if(tn_speech_stt_train(handle,mel,8,labels,2,2000,&final)) return 5;
  if(tn_speech_stt_loss(handle,mel,8,labels,2,&final) || !isfinite(final) || final>=initial*.1) return 6;
  if(tn_speech_stt_predict(handle,mel,8,before,sizeof(before))<0 || strcmp(before,"ab")) return 7;
  int64_t aligned[8]; double loss;
  if(tn_speech_stt_align(handle,mel,8,labels,2,aligned,&loss)) return 8;
  if(aligned[0]!=0 || aligned[7]!=1) return 9;
  if(tn_speech_stt_save(handle,argv[1])) return 10;
  long long loaded=tn_speech_stt_load(argv[1]);
  char after[32];
  if(!loaded || tn_speech_stt_steps(loaded)!=300 || tn_speech_stt_predict(loaded,mel,8,after,sizeof(after))<0 || strcmp(before,after)) return 11;
  if(tn_speech_stt_free(handle) || tn_speech_stt_steps(handle)!=-1) return 12;
  int64_t invalid[1]={0};
  if(tn_speech_stt_train(loaded,mel,8,invalid,1,2000,&loss)!=-1 || tn_speech_stt_steps(loaded)!=300) return 13;
  FILE* file=fopen(argv[1],"r+b");
  if(!file || fseek(file,100,SEEK_SET)) return 14;
  int byte=fgetc(file);
  if(byte==EOF || fseek(file,100,SEEK_SET) || fputc(byte^1,file)==EOF || fclose(file)) return 15;
  if(tn_speech_stt_load(argv[1])!=0) return 16;
  tn_speech_stt_free(loaded);
  printf("{\"synthetic_ctc_initial\":%.9g,\"synthetic_ctc_final\":%.9g,\"prediction\":\"%s\",\"steps\":300,\"checkpoint_roundtrip\":true,\"corrupt_rejected\":true}\n",initial,final,before);
  return 0;
}
