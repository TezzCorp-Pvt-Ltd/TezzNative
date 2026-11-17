#include "speech.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Supervised character-context/duration baseline. Acoustic targets and
 * monotonic frame alignments come from real audio and the native CTC model. */
#define TEXT_INPUTS (5 * TN_SPEECH_CLASSES)
#define TTS_TAG (UINT64_C(1) << 40)
typedef struct { SpeechNet acoustic, duration; atomic_flag busy; } TtsModel;
typedef struct { TtsModel* model; uint32_t generation; } TtsSlot;
static TtsSlot tts_slots[16];
static atomic_flag tts_registry = ATOMIC_FLAG_INIT;

static TtsModel* acquire(long long handle){
  uint64_t id=(uint64_t)handle;
  if(handle<=0 || !(id&TTS_TAG) || atomic_flag_test_and_set(&tts_registry)) return NULL;
  unsigned index=(unsigned)(id&255u);
  TtsModel* model=NULL;
  if(index>=1 && index<=16 && ((id&~TTS_TAG)>>8)==tts_slots[index-1].generation){
    TtsModel* candidate=tts_slots[index-1].model;
    if(candidate && !atomic_flag_test_and_set(&candidate->busy)) model=candidate;
  }
  atomic_flag_clear(&tts_registry); return model;
}

static long long register_model(TtsModel* model){
  if(atomic_flag_test_and_set(&tts_registry)) return 0;
  long long handle=0;
  for(unsigned i=0;i<16;i++) if(!tts_slots[i].model){
    if(++tts_slots[i].generation==0) ++tts_slots[i].generation;
    tts_slots[i].model=model;
    handle=(long long)(TTS_TAG|((uint64_t)tts_slots[i].generation<<8)|(i+1)); break;
  }
  atomic_flag_clear(&tts_registry); return handle;
}

static void destroy(TtsModel* model){
  speech_net_free(&model->acoustic); speech_net_free(&model->duration); free(model);
}

static int validate(const int64_t* labels, long long count){
  if(!labels || count<1 || count>TN_SPEECH_MAX_LABELS) return -1;
  for(int i=0;i<count;i++) if(labels[i]<1 || labels[i]>=TN_SPEECH_CLASSES) return -1;
  return 0;
}

static void text_input(const int64_t* labels, int count, int index, float* input){
  memset(input,0,TEXT_INPUTS*sizeof(float));
  for(int context=-2;context<=2;context++){
    int source=index+context;
    int symbol=source>=0 && source<count ? (int)labels[source] : 0;
    input[(context+2)*TN_SPEECH_CLASSES+symbol]=1.0f;
  }
}

TN_NATIVE_API long long tn_speech_tts_create(long long hidden, long long seed){
  if(hidden<8 || hidden>256) return 0;
  TtsModel* model=(TtsModel*)calloc(1,sizeof(TtsModel));
  if(!model) return 0;
  atomic_flag_clear(&model->busy);
  if(speech_net_init(&model->acoustic,TEXT_INPUTS+2,(int)hidden,TN_SPEECH_MELS,(uint32_t)seed) ||
     speech_net_init(&model->duration,TEXT_INPUTS,32,1,(uint32_t)seed+1)){ destroy(model); return 0; }
  long long handle=register_model(model); if(!handle) destroy(model); return handle;
}

TN_NATIVE_API long long tn_speech_tts_free(long long handle){
  TtsModel* model=acquire(handle); if(!model) return -1;
  if(atomic_flag_test_and_set(&tts_registry)){ atomic_flag_clear(&model->busy); return -1; }
  tts_slots[((uint64_t)handle&255u)-1].model=NULL;
  atomic_flag_clear(&tts_registry); destroy(model); return 0;
}

static long long objective(long long handle, const double* mel, long long frames,
  const int64_t* labels, long long count, const int64_t* alignment, long long rate_ppm, double* loss){
  if(validate(labels,count) || !mel || !alignment || !loss || frames<count || frames>TN_SPEECH_MAX_FRAMES ||
     rate_ppm<0 || rate_ppm>100000) return -1;
  int duration[TN_SPEECH_MAX_LABELS]={0}, starts[TN_SPEECH_MAX_LABELS]={0};
  for(int t=0;t<frames;t++){
    if(alignment[t]<0 || alignment[t]>=count || (t && alignment[t]<alignment[t-1])) return -1;
    duration[alignment[t]]++;
    for(int m=0;m<TN_SPEECH_MELS;m++) if(!isfinite(mel[(size_t)t*TN_SPEECH_MELS+m]) || fabs(mel[(size_t)t*TN_SPEECH_MELS+m])>100) return -1;
  }
  for(int i=0;i<count;i++){ if(!duration[i]) return -1; if(i) starts[i]=starts[i-1]+duration[i-1]; }
  TtsModel* model=acquire(handle); if(!model) return -1;
  float *input=NULL,*hidden=NULL,*output=NULL,*gradient=NULL,*di=NULL,*dh=NULL,*dy=NULL,*dg=NULL;
  long long status=-1;
  input=(float*)calloc((size_t)frames*(TEXT_INPUTS+2),sizeof(float));
  hidden=(float*)malloc((size_t)frames*model->acoustic.hidden*sizeof(float));
  output=(float*)malloc((size_t)frames*TN_SPEECH_MELS*sizeof(float));
  gradient=(float*)malloc((size_t)frames*TN_SPEECH_MELS*sizeof(float));
  di=(float*)malloc((size_t)count*TEXT_INPUTS*sizeof(float));
  dh=(float*)malloc((size_t)count*32*sizeof(float)); dy=(float*)malloc((size_t)count*sizeof(float)); dg=(float*)malloc((size_t)count*sizeof(float));
  if(!input || !hidden || !output || !gradient || !di || !dh || !dy || !dg) goto done;
  for(int i=0;i<count;i++) text_input(labels,(int)count,i,di+(size_t)i*TEXT_INPUTS);
  for(int t=0;t<frames;t++){
    int index=(int)alignment[t]; float* row=input+(size_t)t*(TEXT_INPUTS+2);
    memcpy(row,di+(size_t)index*TEXT_INPUTS,TEXT_INPUTS*sizeof(float));
    row[TEXT_INPUTS]=(float)(t-starts[index]+0.5f)/duration[index];
    row[TEXT_INPUTS+1]=(float)(index+0.5f)/(float)count;
  }
  if(speech_net_forward(&model->acoustic,input,(int)frames,hidden,output) ||
     speech_net_forward(&model->duration,di,(int)count,dh,dy)) goto done;
  *loss=0;
  for(size_t i=0;i<(size_t)frames*TN_SPEECH_MELS;i++){
    double error=output[i]-(mel[i]+10.0)*0.1;
    *loss+=error*error/((double)frames*TN_SPEECH_MELS);
    gradient[i]=(float)(2.0*error/((double)frames*TN_SPEECH_MELS));
  }
  /* Log output guarantees positive durations, but squared log error learns a
     geometric mean and systematically shortens variable-duration speech.
     Minimize error in frames (scaled by 10), predicting E[duration|text]. */
  for(int i=0;i<count;i++){
    double exponent=4.0*dy[i];
    if(exponent>10.0) exponent=10.0;
    if(exponent< -10.0) exponent=-10.0;
    double predicted=exp(exponent);
    dg[i]=(float)(0.08*(predicted-duration[i])*predicted/(double)count);
  }
  if(rate_ppm && (speech_net_update(&model->acoustic,input,hidden,gradient,(int)frames,(float)rate_ppm*1e-6f) ||
     speech_net_update(&model->duration,di,dh,dg,(int)count,(float)rate_ppm*1e-6f))) goto done;
  status=0;
done:
  free(input); free(hidden); free(output); free(gradient); free(di); free(dh); free(dy); free(dg);
  atomic_flag_clear(&model->busy); return status;
}

TN_NATIVE_API long long tn_speech_tts_train(long long handle,const double* mel,long long frames,
  const int64_t* labels,long long count,const int64_t* alignment,long long rate_ppm,double* loss){
  if(rate_ppm<1) return -1;
  return objective(handle,mel,frames,labels,count,alignment,rate_ppm,loss);
}

TN_NATIVE_API long long tn_speech_tts_loss(long long handle,const double* mel,long long frames,
  const int64_t* labels,long long count,const int64_t* alignment,double* loss){
  return objective(handle,mel,frames,labels,count,alignment,0,loss);
}

TN_NATIVE_API long long tn_speech_tts_mel(long long handle, const int64_t* labels, long long count,
  double* mel, long long capacity, long long speed_ppm){
  if(validate(labels,count) || !mel || capacity<1 || capacity>TN_SPEECH_MAX_FRAMES || speed_ppm<100000 || speed_ppm>10000000) return -1;
  TtsModel* model=acquire(handle); if(!model) return -1;
  long long result=-1;
  float *di=NULL,*dh=NULL,*dy=NULL,*input=NULL,*hidden=NULL,*output=NULL;
  if(!model->acoustic.step || !model->duration.step) goto done;
  di=(float*)malloc((size_t)count*TEXT_INPUTS*sizeof(float)); dh=(float*)malloc((size_t)count*32*sizeof(float)); dy=(float*)malloc((size_t)count*sizeof(float));
  if(!di || !dh || !dy) goto done;
  for(int i=0;i<count;i++) text_input(labels,(int)count,i,di+(size_t)i*TEXT_INPUTS);
  if(speech_net_forward(&model->duration,di,(int)count,dh,dy)) goto done;
  int durations[TN_SPEECH_MAX_LABELS], frames=0;
  for(int i=0;i<count;i++){
    double predicted=4.0*dy[i]; if(predicted<0) predicted=0;
    /* Training can assign the entire accepted utterance to one character.
       Preserve that learned duration; the output-capacity check below must
       reject a short buffer instead of silently shortening the speech. */
    if(predicted>log((double)TN_SPEECH_MAX_FRAMES)) predicted=log((double)TN_SPEECH_MAX_FRAMES);
    double duration=exp(predicted)*1000000.0/(double)speed_ppm;
    durations[i]=(int)(duration+0.5); if(durations[i]<1) durations[i]=1;
    frames+=durations[i]; if(frames>capacity) goto done;
  }
  input=(float*)malloc((size_t)frames*(TEXT_INPUTS+2)*sizeof(float));
  hidden=(float*)malloc((size_t)frames*model->acoustic.hidden*sizeof(float)); output=(float*)malloc((size_t)frames*TN_SPEECH_MELS*sizeof(float));
  if(!input || !hidden || !output) goto done;
  int t=0;
  for(int i=0;i<count;i++) for(int f=0;f<durations[i];f++,t++){
    float* row=input+(size_t)t*(TEXT_INPUTS+2); memcpy(row,di+(size_t)i*TEXT_INPUTS,TEXT_INPUTS*sizeof(float));
    row[TEXT_INPUTS]=(float)(f+0.5f)/durations[i]; row[TEXT_INPUTS+1]=(float)(i+0.5f)/(float)count;
  }
  if(speech_net_forward(&model->acoustic,input,frames,hidden,output)) goto done;
  for(size_t i=0;i<(size_t)frames*TN_SPEECH_MELS;i++){
    double value=output[i]*10.0-10.0; if(value< -30) value=-30; if(value>20) value=20; mel[i]=value;
  }
  result=frames;
done:
  free(di); free(dh); free(dy); free(input); free(hidden); free(output);
  atomic_flag_clear(&model->busy); return result;
}

TN_NATIVE_API long long tn_speech_tts_pcm(long long handle, const int64_t* labels, long long count,
  int64_t* pcm, long long capacity, long long speed_ppm, long long pitch_ppm, long long energy_ppm){
  if(!pcm || capacity<400 || capacity>480000 || pitch_ppm<100000 || pitch_ppm>4000000 || energy_ppm<0 || energy_ppm>4000000) return -1;
  int max_frames=(int)((capacity-400)/160+1); if(max_frames>TN_SPEECH_MAX_FRAMES) max_frames=TN_SPEECH_MAX_FRAMES;
  double* mel=(double*)malloc((size_t)max_frames*TN_SPEECH_MELS*sizeof(double)); if(!mel) return -1;
  long long frames=tn_speech_tts_mel(handle,labels,count,mel,max_frames,speed_ppm);
  long long result=frames>0 ? speech_vocode(mel,(int)frames,pcm,(int)capacity,(double)pitch_ppm*1e-6,(double)energy_ppm*1e-6) : -1;
  free(mel); return result;
}

TN_NATIVE_API long long tn_speech_tts_steps(long long handle){
  TtsModel* model=acquire(handle); if(!model) return -1;
  long long result=(long long)model->acoustic.step; atomic_flag_clear(&model->busy); return result;
}

TN_NATIVE_API long long tn_speech_tts_save(long long handle,const char* path){
  if(!path || !*path || strlen(path)>4096) return -1;
  TtsModel* model=acquire(handle); if(!model) return -1;
  long long result=-1;
  if(model->acoustic.step && model->duration.step){
    FILE* file=fopen(path,"wb"); if(file){
      const char magic[8]={'T','N','T','T','S','0','1',0};
      uint32_t config[8]={1,0x01020304,16000,400,512,160,TN_SPEECH_MELS,(uint32_t)model->acoustic.hidden};
      if(fwrite(magic,8,1,file)==1 && fwrite(config,sizeof(config),1,file)==1 && !speech_net_write(file,&model->acoustic) && !speech_net_write(file,&model->duration)) result=0;
      if(fclose(file)) result=-1;
    }
  }
  atomic_flag_clear(&model->busy); return result;
}

TN_NATIVE_API long long tn_speech_tts_load(const char* path){
  if(!path || !*path || strlen(path)>4096) return 0;
  FILE* file=fopen(path,"rb"); if(!file) return 0;
  char magic[8]; uint32_t config[8]; TtsModel* model=NULL; long long handle=0;
  if(fread(magic,8,1,file)!=1 || memcmp(magic,"TNTTS01",8) || fread(config,sizeof(config),1,file)!=1 ||
     config[0]!=1 || config[1]!=0x01020304 || config[2]!=16000 || config[3]!=400 || config[4]!=512 || config[5]!=160 ||
     config[6]!=TN_SPEECH_MELS || config[7]<8 || config[7]>256) goto done;
  model=(TtsModel*)calloc(1,sizeof(TtsModel)); if(!model) goto done; atomic_flag_clear(&model->busy);
  if(speech_net_read(file,&model->acoustic,TEXT_INPUTS+2,(int)config[7],TN_SPEECH_MELS) ||
     speech_net_read(file,&model->duration,TEXT_INPUTS,32,1) || !model->acoustic.step || model->acoustic.step!=model->duration.step ||
     fgetc(file)!=EOF || ferror(file)) goto done;
  handle=register_model(model); if(handle) model=NULL;
done:
  if(model) destroy(model);
  fclose(file); return handle;
}
