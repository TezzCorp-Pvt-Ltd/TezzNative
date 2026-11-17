#include "lexer.h"
#include <limits.h>
#include <stdio.h>

int main(int argc,char** argv){
  if(argc!=2) return 99;
  Lexer lexer;
  lex_init(&lexer,argv[1],"integer_oracle");
  if(lexer.cur.kind==TK_FLOAT){ printf("float %.17g\n",lexer.cur.f); return 0; }
  if(lexer.cur.kind!=TK_NUMBER) return 2;
  printf("integer %lld\n",lexer.cur.num);
  return 0;
}
