/* Validate the owned ONNX test fixture with the vendor parser.
 * This helper does not invoke an NPU adapter or claim device execution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <onnxruntime_c_api.h>
static const OrtApi* api;
static int ok(OrtStatus* error){
  if(!error) return 1;
  fprintf(stderr, "%s\n", api->GetErrorMessage(error));
  api->ReleaseStatus(error);
  return 0;
}
int main(int argc, char** argv){
  if(argc != 2) return 1;
  FILE* file = fopen(argv[1], "rb");
  if(!file) return 1;
  unsigned char data[4096];
  size_t size = fread(data, 1, sizeof(data), file);
  int error = ferror(file) || !feof(file);
  fclose(file);
  if(error || !size) return 1;
  api = OrtGetApiBase()->GetApi(23);
  if(!api) return 1;
  OrtEnv* env = NULL;
  OrtSessionOptions* options = NULL;
  OrtSession* session = NULL;
  int result = 1;
  if(!ok(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "TezzNative-fixture-parser", &env)) ||
     !ok(api->DisableTelemetryEvents(env)) || !ok(api->CreateSessionOptions(&options)) ||
     !ok(api->CreateSessionFromArray(env, data, size, options, &session))) goto done;
  puts("PASS owned ONNX fixture parser (not an NPU execution test)");
  result = 0;
done:
  if(session) api->ReleaseSession(session);
  if(options) api->ReleaseSessionOptions(options);
  if(env) api->ReleaseEnv(env);
  return result;
}
