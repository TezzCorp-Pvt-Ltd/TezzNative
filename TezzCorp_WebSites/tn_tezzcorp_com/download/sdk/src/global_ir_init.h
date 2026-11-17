/* Lower non-literal global initializers through the ordinary typed IR. This
 * preserves casts, strings, aggregates and calls instead of silently emitting
 * zero. Literal data remains available before executable initialization. */
static int ir_global_is_static(struct GlobalVar* g){
  double value;
  return !g->init || g->is_extern || g->init->k==EX_NUM ||
    (type_is_float(g->ty) && ir_float_initializer(g->init, &value));
}

static char* ir_initializer_symbol(IRModule* M, const char* prefix){
  for(int suffix=0;;suffix++){
    char name[96];
    snprintf(name,sizeof(name),"%s_%d",prefix,suffix);
    int len=(int)strlen(name), used=0;
    for(int i=0;i<M->fn_n;i++)
      if(M->fns[i].len==len && !strncmp(M->fns[i].name,name,(size_t)len)) used=1;
    for(int i=0;i<M->global_n;i++)
      if(M->globals[i].len==len && !strncmp(M->globals[i].name,name,(size_t)len)) used=1;
    if(!used){
      char* result=(char*)xmalloc((size_t)len+1);
      memcpy(result,name,(size_t)len+1);
      return result;
    }
  }
}

static void ir_global_init_visit(IRModule* M, TypeEnv* tenv, IRFunc* F,
                                 Module* root, Module* mod, Module** mods,
                                 int mod_n, unsigned char* visited){
  int mi=0;
  for(;mi<mod_n;mi++) if(mods[mi]==mod) break;
  if(mi==mod_n || visited[mi]) return;
  visited[mi]=1;
  if(mod->import_modules){
    for(int i=0;i<mod->import_n;i++)
      ir_global_init_visit(M,tenv,F,root,mod->import_modules[i],mods,mod_n,visited);
  }
  IRGenCtx ctx;
  ctx.cur=mod; ctx.root=root; ctx.return_addr=-1;
  for(int i=0;i<mod->global_n;i++){
    struct GlobalVar* g=&mod->globals[i];
    if(ir_global_is_static(g)) continue;
    Expr lhs; memset(&lhs,0,sizeof(lhs));
    lhs.k=EX_NAME; lhs.loc=g->init->loc; lhs.ty=g->ty;
    lhs.name.name=g->name; lhs.name.len=g->len;
    Stmt assign; memset(&assign,0,sizeof(assign));
    assign.k=ST_ASSIGN; assign.loc=g->init->loc;
    assign.asg.op=TK_ASSIGN; assign.asg.lhs=&lhs; assign.asg.rhs=g->init;
    gen_stmt(M,tenv,F,&ctx,&assign);
  }
}

static void ir_add_global_initializers(IRModule* M, TypeEnv* tenv,
                                       Module* root, Module** mods, int mod_n){
  int main_index=-1, needed=0;
  for(int i=0;i<M->fn_n;i++)
    if(M->fns[i].len==4 && !strncmp(M->fns[i].name,"main",4)) main_index=i;
  for(int mi=0;mi<mod_n;mi++)
    for(int i=0;i<mods[mi]->global_n;i++)
      if(!ir_global_is_static(&mods[mi]->globals[i])) needed=1;
  if(main_index<0 || !needed) return;

  char* init_name=ir_initializer_symbol(M,"__tn_globals_init");
  char* guard_name=ir_initializer_symbol(M,"__tn_globals_started");
  IRGlobal guard; memset(&guard,0,sizeof(guard));
  guard.name=guard_name; guard.len=(int)strlen(guard_name); guard.name_owned=1;
  guard.ty=tenv->b.ty_i64; guard.size=8; guard.align=8; guard.is_static=1;
  M->globals=(IRGlobal*)xrealloc(M->globals,sizeof(IRGlobal)*(size_t)(M->global_n+1));
  M->globals[M->global_n++]=guard;

  if(M->fn_n==M->fn_cap){
    M->fn_cap=M->fn_cap?M->fn_cap*2:64;
    M->fns=(IRFunc*)xrealloc(M->fns,sizeof(IRFunc)*(size_t)M->fn_cap);
  }
  IRFunc* F=&M->fns[M->fn_n++];
  memset(F,0,sizeof(*F));
  F->name=init_name; F->len=(int)strlen(init_name); F->name_owned=1;
  F->max_reg=-1; F->ret_ty=tenv->b.ty_i64;
  int addr=new_reg(F), zero=new_reg(F), label=new_label(F);
  IRIns in; memset(&in,0,sizeof(in));
  in.op=I_ADDRSYM; in.a=addr; in.name=guard_name; in.nlen=guard.len;
  ir_emit(F,in);
  int started=ir_load(F,addr,guard.ty);
  memset(&in,0,sizeof(in)); in.op=I_ICONST; in.a=zero; ir_emit(F,in);
  memset(&in,0,sizeof(in)); in.op=I_JZ; in.a=started; in.b=label; ir_emit(F,in);
  memset(&in,0,sizeof(in)); in.op=I_RET; in.a=zero; ir_emit(F,in);
  memset(&in,0,sizeof(in)); in.op=I_LABEL; in.a=label; ir_emit(F,in);
  int one=new_reg(F);
  memset(&in,0,sizeof(in)); in.op=I_ICONST; in.a=one; in.imm=1; ir_emit(F,in);
  ir_store(F,addr,one,8);

  unsigned char* visited=(unsigned char*)xmalloc((size_t)mod_n);
  memset(visited,0,(size_t)mod_n);
  for(int i=0;i<mod_n;i++)
    if(mods[i]!=root) ir_global_init_visit(M,tenv,F,root,mods[i],mods,mod_n,visited);
  ir_global_init_visit(M,tenv,F,root,root,mods,mod_n,visited);
  free(visited);
  memset(&in,0,sizeof(in)); in.op=I_RET; in.a=zero; ir_emit(F,in);

  IRFunc* entry=&M->fns[main_index];
  IRIns call; memset(&call,0,sizeof(call));
  call.op=I_CALL; call.a=new_reg(entry); call.name=init_name; call.nlen=F->len;
  int old_n=entry->n;
  ir_emit(entry,call);
  memmove(entry->ins+1,entry->ins,sizeof(IRIns)*(size_t)old_n);
  entry->ins[0]=call;
}
