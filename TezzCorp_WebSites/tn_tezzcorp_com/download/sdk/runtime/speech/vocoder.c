#include "speech.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Invert our own 80-band log-power frontend using nonnegative least squares,
 * then bounded fast Griffin-Lim phase reconstruction. No stored audio or OS TTS. */
static void fft(double* re,double* im,int inverse){
  for(int i=1,j=0;i<512;i++){
    int bit=256; for(;j&bit;bit>>=1) j^=bit; j^=bit;
    if(i<j){ double v=re[i]; re[i]=re[j]; re[j]=v; v=im[i]; im[i]=im[j]; im[j]=v; }
  }
  for(int length=2;length<=512;length*=2){
    double angle=(inverse ? 1.0 : -1.0)*6.2831853071795864769/length;
    double wr=cos(angle),wi=sin(angle);
    for(int start=0;start<512;start+=length){
      double ar=1,ai=0;
      for(int k=0;k<length/2;k++){
        int a=start+k,b=a+length/2;
        double vr=re[b]*ar-im[b]*ai,vi=re[b]*ai+im[b]*ar;
        re[b]=re[a]-vr; im[b]=im[a]-vi; re[a]+=vr; im[a]+=vi;
        double next=ar*wr-ai*wi; ai=ar*wi+ai*wr; ar=next;
      }
    }
  }
  if(inverse) for(int i=0;i<512;i++){ re[i]/=512; im[i]/=512; }
}

int speech_vocode(const double* mel,int frames,int64_t* pcm,int capacity,double pitch,double energy){
  if(!mel || !pcm || frames<1 || frames>TN_SPEECH_MAX_FRAMES || !isfinite(pitch) || pitch<0.1 || pitch>4 ||
     !isfinite(energy) || energy<0 || energy>4) return -1;
  int samples=(frames-1)*160+400;
  if(samples>capacity) return -1;
  size_t bins=(size_t)frames*257;
  double *magnitude=(double*)malloc(bins*sizeof(double)),*phase_re=(double*)malloc(bins*sizeof(double)),
         *phase_im=(double*)malloc(bins*sizeof(double)),*previous_re=(double*)calloc(bins,sizeof(double)),
         *previous_im=(double*)calloc(bins,sizeof(double)),*wave=(double*)calloc((size_t)samples,sizeof(double)),
         *normal=(double*)calloc((size_t)samples,sizeof(double));
  if(!magnitude || !phase_re || !phase_im || !previous_re || !previous_im || !wave || !normal){
    free(magnitude); free(phase_re); free(phase_im); free(previous_re); free(previous_im); free(wave); free(normal); return -1;
  }
  int status=-1, indices[257][2]; double weights[257][2],points[82],sums[80]={0},maximum=0;
  double low=2595.0*log10(1.0+20.0/700.0),high=2595.0*log10(1.0+8000.0/700.0);
  for(int i=0;i<82;i++) points[i]=700.0*(pow(10.0,(low+(high-low)*i/81.0)/2595.0)-1.0);
  for(int k=0;k<257;k++){
    double frequency=16000.0*k/512.0, column=0; int used=0;
    indices[k][0]=indices[k][1]=-1; weights[k][0]=weights[k][1]=0;
    for(int m=0;m<80;m++){
      double weight=0;
      if(frequency>=points[m] && frequency<=points[m+1]) weight=(frequency-points[m])/(points[m+1]-points[m]);
      else if(frequency>points[m+1] && frequency<=points[m+2]) weight=(points[m+2]-frequency)/(points[m+2]-points[m+1]);
      if(weight>0 && used<2){ indices[k][used]=m; weights[k][used++]=weight; sums[m]+=weight; column+=weight; }
    }
    if(column>maximum) maximum=column;
  }
  double maximum_row=0; for(int m=0;m<80;m++) if(sums[m]>maximum_row) maximum_row=sums[m];
  double learning_rate=1.0/(maximum*maximum_row+1e-12);
  uint32_t random=17;
  for(int t=0;t<frames;t++){
    double target[80],power[257]={0};
    for(int m=0;m<80;m++){
      double value=mel[(size_t)t*80+m]; if(!isfinite(value) || value>100 || value< -100) goto done;
      if(value>20) value=20;
      if(value< -30) value=-30;
      target[m]=exp(value);
    }
    for(int iteration=0;iteration<60;iteration++){
      double error[80]={0};
      for(int k=0;k<257;k++) for(int j=0;j<2;j++) if(indices[k][j]>=0) error[indices[k][j]]+=power[k]*weights[k][j];
      for(int m=0;m<80;m++) error[m]-=target[m];
      for(int k=0;k<257;k++){
        double gradient=0; for(int j=0;j<2;j++) if(indices[k][j]>=0) gradient+=error[indices[k][j]]*weights[k][j];
        power[k]-=learning_rate*gradient; if(power[k]<0) power[k]=0;
      }
    }
    for(int k=0;k<257;k++){
      double source=k/pitch,value=0;
      if(source<256){ int bin=(int)source; double fraction=source-bin; value=power[bin]*(1-fraction)+power[bin+1]*fraction; }
      else if(source==256) value=power[256];
      size_t index=(size_t)t*257+k; magnitude[index]=sqrt(value);
      random=random*1664525u+1013904223u; double phase=(random>>8)*(6.2831853071795864769/16777216.0);
      phase_re[index]=cos(phase); phase_im[index]=sin(phase);
    }
  }
  double window[400];
  for(int i=0;i<400;i++) window[i]=0.5*(1.0-cos(6.2831853071795864769*i/399.0));
  for(int t=0;t<frames;t++) for(int i=0;i<400;i++) normal[t*160+i]+=window[i]*window[i];
  for(int iteration=0;iteration<64;iteration++){
    memset(wave,0,(size_t)samples*sizeof(double));
    for(int t=0;t<frames;t++){
      double re[512]={0},im[512]={0};
      for(int k=0;k<257;k++){
        size_t index=(size_t)t*257+k;
        re[k]=magnitude[index]*phase_re[index]; im[k]=(k==0 || k==256) ? 0 : magnitude[index]*phase_im[index];
        if(k>0 && k<256){ re[512-k]=re[k]; im[512-k]=-im[k]; }
      }
      fft(re,im,1);
      for(int i=0;i<400;i++) wave[t*160+i]+=re[i]*window[i];
    }
    for(int i=0;i<samples;i++) wave[i]=normal[i]>0.001 ? wave[i]/normal[i] : 0;
    if(iteration==63) break;
    for(int t=0;t<frames;t++){
      double re[512]={0},im[512]={0};
      for(int i=0;i<400;i++) re[i]=wave[t*160+i]*window[i];
      fft(re,im,0);
      for(int k=0;k<257;k++){
        size_t index=(size_t)t*257+k;
        /* FGLA: c_n = t_n + alpha*(t_n-t_(n-1)); project its phase
           onto the requested magnitude on the next synthesis pass. */
        double accelerated_re=re[k]+0.9*(re[k]-previous_re[index]);
        double accelerated_im=im[k]+0.9*(im[k]-previous_im[index]);
        previous_re[index]=re[k]; previous_im[index]=im[k];
        double norm=hypot(accelerated_re,accelerated_im);
        if(norm>1e-15){ phase_re[index]=accelerated_re/norm; phase_im[index]=accelerated_im/norm; }
      }
    }
  }
  double peak=0;
  for(int i=0;i<samples;i++){ if(!isfinite(wave[i])) goto done; if(fabs(wave[i])>peak) peak=fabs(wave[i]); }
  double gain=peak>0.9 ? 0.9/peak : 1.0;
  for(int i=0;i<samples;i++){
    double sample=wave[i]*gain*energy*32767.0; if(sample>32767) sample=32767; if(sample< -32768) sample=-32768;
    pcm[i]=(int64_t)llround(sample);
  }
  status=samples;
done:
  free(magnitude); free(phase_re); free(phase_im); free(previous_re); free(previous_im); free(wave); free(normal); return status;
}
