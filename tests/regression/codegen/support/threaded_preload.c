#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static void* worker(void* unused){
  (void)unused;
  for(;;) pause();
  return NULL;
}

__attribute__((constructor)) static void start_worker(void){
  pthread_t thread;
  if(pthread_create(&thread,NULL,worker,NULL)!=0) _Exit(90);
  if(pthread_detach(thread)!=0) _Exit(91);
  /* The test requires this marker to prove that a live worker was created. */
  if(write(STDOUT_FILENO,"WORKER_STARTED\n",15)!=15) _Exit(92);
}
