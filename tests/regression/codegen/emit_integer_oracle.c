#include "ir.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ir_compile_to_c(IRModule* module,const char* path);
void ir_compile_to_cuda(IRModule* module,const char* path);

int main(int argc,char** argv){
  if(argc!=3) return 99;
  IRModule module={0}; IRFunc functions[12]={0}; IRIns instructions[12][2]={0};
  const char* names[]={"add","sub","mul","divide","modulo","band","bor","bxor","shl","shr","offset","minimum"};
  module.fns=functions; module.fn_n=12;
  for(int i=0;i<12;i++){
    functions[i].name=names[i]; functions[i].len=(int)strlen(names[i]);
    functions[i].ins=instructions[i]; functions[i].n=2; functions[i].max_reg=4;
    functions[i].param_count=i<10 ? 2 : i==10 ? 1 : 0;
    instructions[i][0].op=i<10 ? I_BIN : i==10 ? I_GEP : I_ICONST;
    instructions[i][0].a=4; instructions[i][0].b=0; instructions[i][0].c=2;
    instructions[i][0].binop=(IRBin)i; instructions[i][0].imm=i==10 ? LLONG_MAX : LLONG_MIN;
    instructions[i][1].op=I_RET; instructions[i][1].a=4;
  }
  if(!strcmp(argv[1],"c")) ir_compile_to_c(&module,argv[2]);
  else ir_compile_to_cuda(&module,argv[2]);
  FILE* file=fopen(argv[2],"ab"); if(!file) return 98;
  fputs("\n#include <stdlib.h>\n#include <stdio.h>\nint main(int argc,char** argv){\n  if(argc!=4)return 2;\n  int op=atoi(argv[1]); int64_t a=strtoll(argv[2],0,10),b=strtoll(argv[3],0,10),value;\n  switch(op){\n",file);
  for(int i=0;i<12;i++) fprintf(file,"  case %d:value=%s(%s);break;\n",i,names[i],i<10 ? "a,b" : i==10 ? "a" : "");
  fputs("  default:return 3;\n  }\n  printf(\"%lld\\n\",(long long)value);return 0;\n}\n",file);
  return fclose(file)!=0;
}
