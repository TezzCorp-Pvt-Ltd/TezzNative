#include "speech.h"
#include <math.h>
#include <stdio.h>

/* The training API accepts a 150-frame character; synthesis must not silently
   clamp its learned duration to the former hard-coded 100-frame limit. */
int main(void){
  double mel[150*80], prediction[400*80], loss;
  int64_t labels[1]={3}, alignment[150]={0};
  for(int i=0;i<150*80;i++) mel[i]=-10;
  long long handle=tn_speech_tts_create(16,77);
  if(!handle) return 1;
  for(int step=0;step<1000;step++){
    if(tn_speech_tts_train(handle,mel,150,labels,1,alignment,1000,&loss)) return 2;
  }
  long long frames=tn_speech_tts_mel(handle,labels,1,prediction,400,1000000);
  long long too_small=tn_speech_tts_mel(handle,labels,1,prediction,100,1000000);
  tn_speech_tts_free(handle);
  printf("DURATION_BOUNDS frames %lld expected150 too_small %lld\n",frames,too_small);
  return frames<145 || frames>155 || too_small!=-1;
}
