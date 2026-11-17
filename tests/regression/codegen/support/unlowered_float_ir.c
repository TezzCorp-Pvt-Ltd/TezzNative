#include "ir.h"
#include <string.h>
void ir_compile_to_c(IRModule*,const char*);
void ir_compile_to_cuda(IRModule*,const char*);

int main(int argc,char** argv){
  if(argc!=3 && argc!=4) return 90;
  IRIns instructions[2]={0};
  IRFunc function={0};
  IRModule module={0};
  instructions[0].op=argc==4 ? (IROp)-1 : I_F32_TO_BF16;
  instructions[0].a=1;
  instructions[0].b=0;
  instructions[1].op=I_RET;
  instructions[1].a=1;
  function.name="main";
  function.len=4;
  function.max_reg=1;
  function.ins=instructions;
  function.n=2;
  module.fns=&function;
  module.fn_n=1;
  if(!strcmp(argv[1],"c")) ir_compile_to_c(&module,argv[2]);
  else ir_compile_to_cuda(&module,argv[2]);
  return 0;
}
