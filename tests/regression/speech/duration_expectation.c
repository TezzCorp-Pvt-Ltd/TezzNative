#include "speech.h"
#include <stdio.h>
int main(void){
  double mel[20*80],prediction[100*80],loss;
  int64_t labels[2]={3,4},alignment[20];
  for(int i=0;i<20*80;i++) mel[i]=-10;
  long long handle=tn_speech_tts_create(16,77);
  if(!handle) return 1;
  for(int step=0;step<1200;step++){
    int boundary=step%2 ? 2 : 18;
    for(int t=0;t<20;t++) alignment[t]=t<boundary ? 0 : 1;
    if(tn_speech_tts_train(handle,mel,20,labels,2,alignment,1000,&loss)) return 2;
  }
  long long frames=tn_speech_tts_mel(handle,labels,2,prediction,100,1000000);
  tn_speech_tts_free(handle);
  printf("DURATION_EXPECTATION frames %lld expected 20\n",frames);
  return frames<17 || frames>23;
}
