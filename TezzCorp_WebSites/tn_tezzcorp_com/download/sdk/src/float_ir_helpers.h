/* Portable storage-bit conversions lowered through the normal integer IR.
 * Keep rounding and special-value policies aligned with include/float_bits.h. */
static const char float_ir_source[] =
"fn round_shift(value:int, shift:int) -> int:\n"
"  if shift >= 32: ret 0\n"
"  if shift <= 0: ret value\n"
"  retained:int = value >> shift\n"
"  discarded:int = value & ((1 << shift) - 1)\n"
"  half:int = 1 << (shift - 1)\n"
"  if discarded > half: ret retained + 1\n"
"  if discarded == half && (retained & 1) != 0: ret retained + 1\n"
"  ret retained\n"
"\n"
"fn encode(bits:int, mode:int) -> int:\n"
"  bits = bits & 4294967295\n"
"  if mode == 0:\n"
"    if (bits & 2147483647) > 2139095040: ret (bits >> 16) | 64\n"
"    ret ((bits + 32767 + ((bits >> 16) & 1)) >> 16) & 65535\n"
"  mant_bits:int = 10\n"
"  bias:int = 15\n"
"  sign_shift:int = 16\n"
"  sign_mask:int = 32768\n"
"  max_bits:int = 31744\n"
"  if mode != 1:\n"
"    mant_bits = 3\n"
"    bias = 7\n"
"    sign_shift = 24\n"
"    sign_mask = 128\n"
"    max_bits = 126\n"
"    if mode == 3:\n"
"      mant_bits = 2\n"
"      bias = 15\n"
"      max_bits = 123\n"
"  sign:int = (bits >> sign_shift) & sign_mask\n"
"  exponent:int = (bits >> 23) & 255\n"
"  mant:int = bits & 8388607\n"
"  if exponent == 255:\n"
"    if mode == 1:\n"
"      if mant != 0: ret sign | 31744 | 512 | (mant >> 13)\n"
"      ret sign | 31744\n"
"    if mant != 0: ret sign | 127\n"
"    ret sign | max_bits\n"
"  exponent = exponent - 127 + bias\n"
"  mant = mant | 8388608\n"
"  rounded:int = 0\n"
"  if exponent <= 0:\n"
"    rounded = round_shift(mant, 1 - exponent + 23 - mant_bits)\n"
"  else:\n"
"    rounded = ((exponent - 1) << mant_bits) + round_shift(mant, 23 - mant_bits)\n"
"  if rounded > max_bits: rounded = max_bits\n"
"  ret sign | rounded\n"
"\n"
"fn decode(bits:int, mode:int) -> int:\n"
"  if mode == 0: ret (bits & 65535) << 16\n"
"  mant_bits:int = 10\n"
"  bias:int = 15\n"
"  sign_shift:int = 16\n"
"  sign_mask:int = 32768\n"
"  exp_mask:int = 31\n"
"  if mode != 1:\n"
"    mant_bits = 3\n"
"    bias = 7\n"
"    sign_shift = 24\n"
"    sign_mask = 128\n"
"    exp_mask = 15\n"
"    if mode == 3:\n"
"      mant_bits = 2\n"
"      bias = 15\n"
"      exp_mask = 31\n"
"  sign:int = (bits & sign_mask) << sign_shift\n"
"  exponent:int = (bits >> mant_bits) & exp_mask\n"
"  mant_mask:int = (1 << mant_bits) - 1\n"
"  mant:int = bits & mant_mask\n"
"  if exponent == exp_mask:\n"
"    if mode == 3 && mant != 0: ret sign | 2143289344\n"
"    if mode != 2: ret sign | 2139095040 | (mant << (23 - mant_bits))\n"
"    if mant == 7: ret sign | 2143289344\n"
"  if exponent == 0:\n"
"    if mant == 0: ret sign\n"
"    exponent = 1\n"
"    while (mant & (1 << mant_bits)) == 0:\n"
"      mant = mant << 1\n"
"      exponent = exponent - 1\n"
"    mant = mant & mant_mask\n"
"  ret sign | ((exponent + 127 - bias) << 23) | (mant << (23 - mant_bits))\n"
"\n"
"fn unsigned_to_f64(value:int) -> f64:\n"
"  if value >= 0: ret value as f64\n"
"  half:int = ((value >> 1) & 9223372036854775807) | (value & 1)\n"
"  ret (half as f64) * 2.0\n"
"\n"
"fn integer_to_f32(value:int, unsigned:int) -> int:\n"
"  if value == 0: ret 0\n"
"  sign:int = 0\n"
"  magnitude:int = value\n"
"  if unsigned == 0 && value < 0:\n"
"    sign = 2147483648\n"
"    magnitude = 0 - value\n"
"  highest:int = 63\n"
"  if magnitude >= 0:\n"
"    highest = 0\n"
"    remaining:int = magnitude\n"
"    while remaining > 1:\n"
"      remaining = remaining >> 1\n"
"      highest = highest + 1\n"
"  retained:int = 0\n"
"  if highest <= 23:\n"
"    retained = magnitude << (23 - highest)\n"
"  else:\n"
"    shift:int = highest - 23\n"
"    retained = (magnitude >> shift) & 16777215\n"
"    discarded:int = magnitude & ((1 << shift) - 1)\n"
"    half:int = 1 << (shift - 1)\n"
"    if discarded > half || (discarded == half && (retained & 1) != 0): retained = retained + 1\n"
"    if retained == 16777216:\n"
"      retained = retained >> 1\n"
"      highest = highest + 1\n"
"  ret sign | ((highest + 127) << 23) | (retained & 8388607)\n"
"\n"
"fn f64_to_unsigned(value:f64) -> int:\n"
"  if value != value || value < 0.0 || value >= 18446744073709551616.0: ret 9223372036854775808\n"
"  if value >= 9223372036854775808.0:\n"
"    ret ((value - 9223372036854775808.0) as int) | 9223372036854775808\n"
"  ret value as int\n"
"\n"
"fn f32_to_f64_bits(bits:int) -> float:\n"
"  sign:int = (bits & 2147483648) << 32\n"
"  exponent:int = (bits >> 23) & 255\n"
"  mantissa:int = bits & 8388607\n"
"  if exponent == 255:\n"
"    if mantissa != 0: ret bits_to_float(sign | 9221120237041090560 | (mantissa << 29))\n"
"    ret bits_to_float(sign | 9218868437227405312)\n"
"  if exponent == 0:\n"
"    if mantissa == 0: ret bits_to_float(sign)\n"
"    exponent = 1\n"
"    while (mantissa & 8388608) == 0:\n"
"      mantissa = mantissa << 1\n"
"      exponent = exponent - 1\n"
"    mantissa = mantissa & 8388607\n"
"  ret bits_to_float(sign | ((exponent + 896) << 52) | (mantissa << 29))\n"
"\n"
"fn f64_to_f32_bits(value:float) -> int:\n"
"  bits:int = float_to_bits(value)\n"
"  sign:int = (bits >> 32) & 2147483648\n"
"  exponent:int = (bits >> 52) & 2047\n"
"  mantissa:int = bits & 4503599627370495\n"
"  if exponent == 2047:\n"
"    if mantissa == 0: ret sign | 2139095040\n"
"    ret sign | 2143289344 | (mantissa >> 29)\n"
"  if exponent == 0: ret sign\n"
"  exponent = exponent - 896\n"
"  if exponent >= 255: ret sign | 2139095040\n"
"  mantissa = mantissa | 4503599627370496\n"
"  rounded:int = 0\n"
"  if exponent <= 0:\n"
"    rounded = round_wide(mantissa, 30 - exponent)\n"
"  else:\n"
"    rounded = ((exponent - 1) << 23) + round_wide(mantissa, 29)\n"
"  if rounded > 2139095040: rounded = 2139095040\n"
"  ret sign | rounded\n"
"\n"
"fn round_wide(value:int, shift:int) -> int:\n"
"  if shift >= 54: ret 0\n"
"  if shift <= 0: ret value\n"
"  retained:int = value >> shift\n"
"  discarded:int = value & ((1 << shift) - 1)\n"
"  half:int = 1 << (shift - 1)\n"
"  if discarded > half || (discarded == half && (retained & 1) != 0): ret retained + 1\n"
"  ret retained\n"
"\n"
"fn f64_to_signed(value:f64) -> int:\n"
"  if value != value || value < -9223372036854775808.0 || value >= 9223372036854775808.0: ret 9223372036854775808\n"
"  ret value as int\n"
"\n"
"fn bits_to_float(bits:int) -> float:\n"
"  unsafe:\n"
"    ret *((&bits) as *float)\n"
"\n"
"fn float_to_bits(value:float) -> int:\n"
"  unsafe:\n"
"    ret *((&value) as *int)\n";

static void ir_lower_float_bits(IRModule* M, TypeEnv* tenv){
  int original_n = M->fn_n;
  int needed = 0;
  for(int fi=0;fi<original_n;fi++){
    for(int i=0;i<M->fns[fi].n;i++){
      IROp op = M->fns[fi].ins[i].op;
      if(op>=I_BF16_TO_F32 && op<=I_F32_TO_F8_E5M2) needed=1;
      if(op==I_F32_TO_F64 || op==I_F64_TO_F32 || op==I_F2I) needed=1;
      if(op==I_I2F && (M->fns[fi].ins[i].is_unsigned || M->fns[fi].ins[i].size==4)) needed=1;
    }
  }
  if(!needed) return;

  Module* helpers = parse_module(tenv, float_ir_source, "<float-bits>");
  ModuleGraph graph;
  memset(&graph,0,sizeof(graph));
  graph.mods = &helpers;
  graph.mod_n = 1;
  sema_check_module(&graph, tenv, helpers);
  for(int fi=0;fi<helpers->fn_n;fi++){
    ir_add_func(M, tenv, helpers, helpers, helpers->fns[fi]);
  }

  // Choose private symbols without colliding with functions in the program.
  const char** names=(const char**)xmalloc((size_t)helpers->fn_n*sizeof(*names));
  int* lengths=(int*)xmalloc((size_t)helpers->fn_n*sizeof(*lengths));
  for(int hi=0;hi<helpers->fn_n;hi++){
    IRFunc* fn = &M->fns[original_n + hi];
    int suffix = original_n;
    char symbol[96];
    for(;;){
      snprintf(symbol,sizeof(symbol),"__tn_float_%d_%.*s",suffix,fn->len,fn->name);
      int collision=0;
      for(int fi=0;fi<original_n;fi++){
        if((int)strlen(symbol)==M->fns[fi].len && !memcmp(symbol,M->fns[fi].name,strlen(symbol))) collision=1;
      }
      if(!collision) break;
      suffix++;
    }
    char* copy=(char*)xmalloc(strlen(symbol)+1);
    memcpy(copy,symbol,strlen(symbol)+1);
    names[hi] = copy;
    lengths[hi] = (int)strlen(symbol);
  }
  for(int hi=0;hi<helpers->fn_n;hi++){
    IRFunc* fn = &M->fns[original_n + hi];
    fn->name=names[hi]; fn->len=lengths[hi]; fn->name_owned=1;
    for(int i=0;i<fn->n;i++){
      IRIns* in=&fn->ins[i];
      if(in->op!=I_CALL) continue;
      for(int hj=0;hj<helpers->fn_n;hj++){
        FnDecl* target=helpers->fns[hj];
        if(in->nlen==target->len && !memcmp(in->name,target->name,(size_t)target->len)){
          in->name=names[hj]; in->nlen=lengths[hj];
          break;
        }
      }
    }
  }

  for(int fi=0;fi<original_n;fi++){
    IRFunc* fn=&M->fns[fi];
    int old_n=fn->n;
    IRIns* old=fn->ins;
    fn->ins=NULL; fn->n=0; fn->cap=0;
    for(int i=0;i<old_n;i++){
      IRIns in=old[i];
      if(in.op==I_F32_TO_F64 || in.op==I_F64_TO_F32 || in.op==I_F2I ||
         (in.op==I_I2F && (in.is_unsigned || in.size==4))){
        int value=in.b;
        if(in.op==I_F2I && in.size==4){
          IRIns promote; memset(&promote,0,sizeof(promote));
          promote.op=I_CALL; promote.a=new_reg(fn); promote.argc=1;
          promote.args=(int*)xmalloc(sizeof(int)); promote.args[0]=value;
          promote.name=names[6]; promote.nlen=lengths[6];
          ir_emit(fn,promote); value=promote.a;
        }
        int helper=in.op==I_F2I ? (in.is_unsigned ? 5 : 9) :
                   in.op==I_F32_TO_F64 ? 6 : in.op==I_F64_TO_F32 ? 7 : in.size==4 ? 4 : 3;
        IRIns call; memset(&call,0,sizeof(call));
        call.op=I_CALL; call.a=in.a; call.argc=1;
        if(helper==4){
          int mode=new_reg(fn);
          IRIns constant; memset(&constant,0,sizeof(constant));
          constant.op=I_ICONST; constant.a=mode; constant.imm=in.is_unsigned;
          ir_emit(fn,constant); call.argc=2;
          call.args=(int*)xmalloc(sizeof(int)*2); call.args[0]=value; call.args[1]=mode;
        } else {
          call.args=(int*)xmalloc(sizeof(int)); call.args[0]=value;
        }
        call.name=names[helper]; call.nlen=lengths[helper];
        ir_emit(fn,call);
      } else if(in.op>=I_BF16_TO_F32 && in.op<=I_F32_TO_F8_E5M2){
        int offset=(int)in.op-(int)I_BF16_TO_F32;
        int mode=new_reg(fn);
        IRIns constant; memset(&constant,0,sizeof(constant));
        constant.op=I_ICONST; constant.a=mode; constant.imm=offset/2;
        ir_emit(fn,constant);
        IRIns call; memset(&call,0,sizeof(call));
        call.op=I_CALL; call.a=in.a; call.argc=2;
        call.args=(int*)xmalloc(sizeof(int)*2);
        call.args[0]=in.b; call.args[1]=mode;
        int helper=(offset & 1) ? 1 : 2;
        call.name=names[helper]; call.nlen=lengths[helper];
        ir_emit(fn,call);
      } else {
        ir_emit(fn,in);
      }
    }
    free(old);
  }
  free(names); free(lengths);
}
