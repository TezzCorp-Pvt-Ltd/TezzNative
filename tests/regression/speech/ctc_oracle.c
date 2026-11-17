#include "speech.h"
#include <math.h>
#include <stdio.h>

int main(void){
  float logits[12] = {0.2f,0.5f,-0.1f, -0.3f,0.7f,0.4f, 0.6f,-0.2f,0.9f, 0.1f,0.3f,0.8f};
  int64_t labels[2] = {1,2};
  float gradient[12]; int alignment[4]; double loss;
  if(speech_ctc(logits,4,3,labels,2,gradient,alignment,&loss)) return 1;
  printf("{\"loss\":%.17g,\"gradient\":[",loss);
  for(int i=0;i<12;i++) printf("%s%.9g",i?",":"",gradient[i]);
  printf("],\"alignment\":[%d,%d,%d,%d]}\n",alignment[0],alignment[1],alignment[2],alignment[3]);
  for(int i=0;i<12;i++){
    float original=logits[i]; double plus,minus;
    logits[i]=original+0.001f;
    if(speech_ctc(logits,4,3,labels,2,NULL,NULL,&plus)) return 2;
    logits[i]=original-0.001f;
    if(speech_ctc(logits,4,3,labels,2,NULL,NULL,&minus)) return 3;
    logits[i]=original;
    if(fabs((plus-minus)/0.002-gradient[i])>0.0001) return 4;
  }
  int64_t repeated[2]={1,1};
  if(speech_ctc(logits,2,3,repeated,2,NULL,NULL,&loss)==0) return 5;
  if(speech_ctc(logits,3,3,repeated,2,gradient,alignment,&loss)) return 6;
  if(speech_ctc(logits,4,3,NULL,0,gradient,NULL,&loss)) return 7;
  return 0;
}
