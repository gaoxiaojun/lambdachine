/*
 * The reference interpreter using direct threading.
 *
 * This only works with GCC because it requires GNU's "labels as
 * values" extensions.
 *
 */

#include "Common.h"
#include "Bytecode.h"
#include "InfoTables.h"
#include "Thread.h"
#include "MiscClosures.h"
#include "PrintClosure.h"

#include <stdio.h>
#include <stdlib.h>

/*********************************************************************

Stack frame layout and invariants
---------------------------------

 - The stack grows upwards.


    +----------------+
    |   register N   |
    +----------------+ <--- base[N]
    :                :
    :                :
    +----------------+
    |   register 0   |
    +----------------+ <--- base
    |      Node      | .. points to the current closure (which in
    |----------------|    turn points to the info table)
    |  return addr.  | .. points to the byte code instruction to
    |----------------|    retur to.
    | previous base  | .. a pointer (or offset) to the previous base
    +----------------+




The entry frame
---------------

A newly created stack is populated with the entry frame.  This looks
as follows:



*********************************************************************/

int engine(Thread *T);

Closure *
startThread(Thread *T, Closure *cl)
{
  int ans;
  T->base[0] = (Word)cl;
  ans = engine(T);
  if (ans != 0) {
    fprintf(stderr, "ABORT: Interpreter exitited abnormally (%d)\n", ans);
    exit(1);
  }
  return (Closure*)T->stack[1];
}

#define STACK_FRAME_SIZEW   3
#define UPDATE_FRAME_SIZEW  (STACK_FRAME_SIZEW + 2)
#define MAX_CALLT_ARGS      12

typedef void* Inst;


int engine(Thread* T)
{
  static Inst disp1[] = {
#define BCIMPL(name,_) &&op_##name,
    BCDEF(BCIMPL)
#undef BCIMPL
    &&stop
  };
  Inst *disp = disp1;


  Word *base = T->base;
  // The program counter always points to the *next* instruction to be
  // decoded.
  u4 *pc = T->pc;
  u4 opA, opB, opC, opcode;
  Word last_result = 0;
  Word callt_temp[MAX_CALLT_ARGS];
  LcCode *code = NULL;

  /*
    At the beginning of an instruction the following holds:
    - pc points to the next instruction.
    - opcode contains the current opcode
    - opA has been decoded
    - opC is the D or SD operand
  */
# define DISPATCH_NEXT \
    opcode = bc_op(*pc); \
    opA = bc_a(*pc); \
    opC = bc_d(*pc); \
    ++pc; \
    goto *disp[opcode]

/* Decode the B and C operand from D. */
# define DECODE_BC \
    opB = bc_b_from_d(opC); \
    opC = bc_c_from_d(opC)
# define DECODE_AD \
    ;

  // Dispatch first instruction
  DISPATCH_NEXT;

 stop:
  T->pc = pc;
  T->base = base;
  return 0;

 op_ADDRR:
  DECODE_BC;
  base[opA] = base[opB] + base[opC];
  DISPATCH_NEXT;

 op_SUBRR:
  DECODE_BC;
  base[opA] = base[opB] - base[opC];
  DISPATCH_NEXT;

 op_MULRR:
  DECODE_BC;
  base[opA] = (WordInt)base[opB] * (WordInt)base[opC];
  DISPATCH_NEXT;

 op_DIVRR:
  DECODE_BC;
  if ((WordInt)base[opC] != 0)
    base[opA] = (WordInt)base[opB] / (WordInt)base[opC];
  else
    ; // TODO: Throw exception
  DISPATCH_NEXT;


 op_JMP:
  DECODE_AD;
  // add opC to the current pc (which points to the next instruction).
  // This means "JMP 0" is a No-op, "JMP -1" is an infinite loop.
  pc += bc_j_from_d(opC);
  DISPATCH_NEXT;

 op_MOV:
  DECODE_AD;
  base[opA] = base[opC];
  DISPATCH_NEXT;

 op_KINT:
  DECODE_AD;
  /* D = signed 16 bit integer constant */
  base[opA] = (WordInt)opC;
  DISPATCH_NEXT;

 op_NEW_INT:
  // A = result (IntClosure*)
  // C/D = value
  DECODE_AD;
  {
    WordInt val = base[opC];

    if (val >= -128 && val <= 127) {
      base[opA] = (Word)&smallInt(val);
    } else {
      IntClosure *cl = allocate(cap0, 2);
      base[opA] = (Word)cl;
      cl->info = &stg_Izh_con_info;
      cl->val = val;
    }
    DISPATCH_NEXT;
  }

 op_NOT:
  DECODE_AD;
  base[opA] = ~base[opC];
  DISPATCH_NEXT;

 op_NEG:
  DECODE_AD;
  base[opA] = -(WordInt)base[opC];
  DISPATCH_NEXT;

  /* Conditional branch instructions are followed by a JMP
     instruction, but we implement both together. */
 op_ISLT:
  DECODE_AD;
  ++pc;
  if ((WordInt)base[opA] < (WordInt)base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ISGE:
  DECODE_AD;
  ++pc;
  if ((WordInt)base[opA] >= (WordInt)base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ISLE:
  DECODE_AD;
  ++pc;
  if ((WordInt)base[opA] <= (WordInt)base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ISGT:
  DECODE_AD;
  ++pc;
  if ((WordInt)base[opA] > (WordInt)base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ISNE:
  DECODE_AD;
  ++pc;
  if (base[opA] != base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ISEQ:
  DECODE_AD;
  ++pc;
  if (base[opA] == base[opC])
    pc += bc_j(*(pc - 1));
  DISPATCH_NEXT;

 op_ALLOC1:
  // A = target
  // B = itbl
  // C = payload[0]
  {
    DECODE_BC;
    Closure *cl = malloc(sizeof(ClosureHeader) + sizeof(Word));
    setInfo(cl, (InfoTable*)base[opB]);
    cl->payload[0] = base[opC];
    base[opA] = (Word)cl;
    DISPATCH_NEXT;
  }

 op_ALLOC:
  // A = target
  // B = itbl
  // C = payload size
  // payload regs
  {
    DECODE_BC;
    u4 sz = base[opC];
    u4 i;
    u1 *arg = (u1 *)pc;
    Closure *cl = malloc(sizeof(ClosureHeader) + sz * sizeof(Word));
    setInfo(cl, (InfoTable*)base[opB]);
    for (i = 0; i < sz; i++)
      cl->payload[i] = base[*arg++];
    pc += (sz + 3) / sizeof(BCIns);
    DISPATCH_NEXT;
  }

 op_LOADF:
  // A = target
  // B = closure ptr.
  // C = closure offset
  {
    DECODE_BC;
    u4 offset = (u1)opC;
    Closure *cl = (Closure*)base[opB];
    base[opA] = cl->payload[offset];
    DISPATCH_NEXT;
  }

 op_LOADFV:
  // A = target
  // C/D = offset
  {
    u4 offset = (u2)opC;
    Closure *node = (Closure*)base[-1];
    base[opA] = node->payload[offset];
    DISPATCH_NEXT;
  }

 op_LOADBH:
  // A = target
  {
    base[opA] = (Word)&stg_BLACKHOLE_closure;
    DISPATCH_NEXT;
  }

 op_LOADSLF:
  // A = target
  {
    base[opA] = base[-1];
    DISPATCH_NEXT;
  }

 op_JFUNC:
 op_IFUNC:
 op_FUNC:
  // ignore
  DISPATCH_NEXT;

 op_CASE:
  // A case with compact targets.
  //
  //  +-----------+-----+-----+
  //  | num_cases |  A  | OPC |
  //  +-----------+-----+-----+
  //  | target_1  | target_0  |  target_i:
  //  +-----------+-----------+    goto this address if tag = i
  //  :                       :
  //  +-----------+-----------+  targetN may be 0 if num_cases is odd.
  //  | target_N  | target_N-1|
  //  +-----------+-----------+
  //  :  default case follows :
  //  +- - - - - - - - - - - -+
  //
  // Targets are non-negative numbers.  They are interpreted as
  // offsets relative to the _end_ of the instruction.  That is "0"
  // denotes the instruction directly following the CASE instruction.
  //
  // If num_cases is smaller than the tag, then we just fall through
  // to the default case.
  //
  // A = thing to dispatch on (must be a constructor node)
  // D = number of cases
  //
  {
    Closure *cl = (Closure *)base[opA];
    u2 num_cases = opC;
    BCIns *table = pc;
    pc += (num_cases + 1) >> 1;
    // assert cl->info.type == CONSTR

    u2 tag = getTag(cl);
    if (tag < num_cases) { // tags start at 0
      BCIns target = table[tag >> 1];
      u2 offs =
        tag & 1 ? bc_case_target(target) : bc_case_targetlo(target);
      pc += offs;
    }

    DISPATCH_NEXT;
  }

 op_CASE_S:
  // Sparse CASE.  A case with possibly missing tags.
  //
  //  +-----------+-----+-----+
  //  | num_cases |  A  | OPC |
  //  +-----------+-----+-----+
  //  | max_tag   |  min_tag  |
  //  +-----------+-----------+
  //  | target    |    tag    |  x num_cases
  //  +-----------+-----------+
  //  :  default case follows :
  //  +- - - - - - - - - - - -+
  //
  // The (tag, target) items must be in ascending order.  This allows us
  // to use binary search to find the matching case.
  //
  {
    Closure *cl = (Closure*)base[opA];
    u2 num_cases = opC;
    u2 min_tag = bc_case_mintag(*pc);
    u2 max_tag = bc_case_maxtag(*pc);
    BCIns *table = pc + 1;
    pc += 1 + num_cases;

    LC_ASSERT(cl != NULL && getInfo(cl)->type == CONSTR);
    u2 tag = getTag(cl);
    int istart = 0;
    int ilen = num_cases;
    int imid = 0;

    if (tag >= min_tag && tag <= max_tag) {
      // Use binary search if there's more than 4 entries
      while (ilen > 4) {
        int imid = (istart + istart + ilen) / 2;
        if (bc_case_tag(table[imid]) == tag)
          goto op_CASE_S_found;
        else if (bc_case_tag(table[imid]) < tag)
          ilen = imid - istart;
        else { // > tag
          ilen = istart + ilen + 1 - imid;
          istart = imid + 1;
        }
      }

      // The linear search for up to 4 entries
      for (imid = istart; ilen > 0; ilen--, imid++)
        if (bc_case_tag(table[imid]) == tag)
          goto op_CASE_S_found;

    }
    // nothing found
    DISPATCH_NEXT;

  op_CASE_S_found:
    LC_ASSERT(bc_case_tag(table[imid]) == tag);
    pc += bc_case_target(table[imid]);
    DISPATCH_NEXT;
  }

 op_EVAL:
  // Format of an EVAL instruction:
  //
  //  +-----------+-----+-----+
  //  |     -     |  A  | OPC |
  //  +-----------+-----+-----+
  //  |   live-outs bitmask   |
  //  +-----------+-----------+
  //
  {
    // opA = thing to evaluate
    Closure *tnode = (Closure *)base[opA];

    LC_ASSERT(tnode != NULL);

    if (closure_HNF(tnode)) {
      last_result = base[opA];
      pc += 1; // skip live-out info
      DISPATCH_NEXT;
    } else {
      //Closure *node = (Closure *)base[-1];
      Word *top = T->top; //base + node->info->code.framesize;
      FuncInfoTable *info = (FuncInfoTable*)getInfo(tnode);
      u4 framesize = info->code.framesize;

      if (stackOverflow(T, T->top, STACK_FRAME_SIZEW + UPDATE_FRAME_SIZEW +
                        framesize)) {
        printf("Stack overflow.  TODO: Automatically grow stack.\n");
        return -1;
      }

      BCIns *return_pc = pc + 1; // skip live-out info
      // push update frame and enter thunk
      top[0] = (Word)base;
      top[1] = (Word)return_pc;
      top[2] = (Word)&stg_UPD_closure;
      top[3] = (Word)tnode; // reg0
      top[4] = 0;           // reg1
      top[5] = (Word)&top[3];
      top[6] = (Word)stg_UPD_return_pc;
      top[7] = (Word)tnode;

      base = top + STACK_FRAME_SIZEW + UPDATE_FRAME_SIZEW;
      T->top = base + framesize;
      code = &info->code;
      pc = info->code.code;
      DISPATCH_NEXT;
    }

  }

 op_UPDATE:
  // opC/D = new value
  // opA = old value
  //
  // Make old_value point to new_value by overwriting the closure for
  // old_value with an indirection to new_value.  Then return new_value.
  {
    Closure *oldnode = (Closure *)base[opA];
    Closure *newnode = (Closure *)base[opC];
    setInfo(oldnode, &stg_IND_info);
    // TODO: Enforce invariant: *newcode is never an indirection.
    oldnode->payload[0] = (Word)newnode;
    last_result = (Word)newnode;
    goto do_return;
  }

 op_RET1:
  // opA = result
  //
  // The return address is on the stack. just jump to it.
  last_result = base[opA];
 do_return:
  T->top = base - 3;
  pc = (BCIns*)base[-2];
  base = (Word*)base[-3];
  { FuncInfoTable *info = getFInfo((Closure*)base[-1]);
    code = &info->code;
  }
  DISPATCH_NEXT;

 op_MOV_RES:
  // Copy last function call result into a register.
  //
  // opA = target register
  base[opA] = last_result;
  DISPATCH_NEXT;

 op_CALLT:
  {
    // opA = function
    // opB = no of args
    // opC = first argument
    DECODE_BC;
    u4 nargs = opB;
    Word arg0 = base[opC];
    Closure *fnode = (Closure *)base[opA];
    //Closure *node = (Closure *)base[-1];

    LC_ASSERT(fnode != NULL && getInfo(fnode)->type == FUN);
    FuncInfoTable *info = (FuncInfoTable*)getInfo(fnode);

    if (nargs != info->code.arity) {
      printf("TODO: Implement partial application / overapplication.\n");
      return -1;
    }

    u4 curframesize = T->top - base;
    u4 newframesize = info->code.framesize;

    if (newframesize > curframesize) {
      if (stackOverflow(T, base, newframesize)) {
        printf("Stack overflow.  TODO: Automatically grow stack.\n");
        return -1;
      } else {
        T->top = base + newframesize;
      }
    }

    if (nargs > MAX_CALLT_ARGS + 1) {
      printf("Too many arguments to CALLT.  (Error in code gen?)\n");
      return -1;
    }

    // Copy args into temporary area, then put them back onto the stack.
    // This avoids accidentally overwriting registers contents.
    u1 *arg = (u1 *)pc;
    int i;
    for (i = 0; i < nargs - 1; i++, arg++) {
      callt_temp[i] = base[*arg];
    }
    base[0] = arg0;
    for (i = 0; i < nargs - 1; i++) {
      base[i + 1] = callt_temp[i];
    }
    code = &info->code;
    pc = info->code.code;
    DISPATCH_NEXT;
  }

 op_CALL:
  {
    // opA = function
    // opB = no of args
    // opC = first argument reg
    // following bytes: argument regs, live regs
    DECODE_BC;
    u4 nargs = opC;
    Word arg0 = base[opB];
    Closure *fnode = (Closure *)base[opA];
    //Closure *node = (Closure *)base[-1];   // the current node
    Word *top = T->top; //&base[node->info->code.framesize];

    LC_ASSERT(fnode != NULL && getInfo(fnode)->type == FUN);
    FuncInfoTable *info = (FuncInfoTable*)getInfo(fnode);

    if (nargs != info->code.arity) {
      printf("TODO: Implement partial application / overapplication.\n");
      return -1;
    }

    u4 framesize = info->code.framesize;

    if (stackOverflow(T, top, STACK_FRAME_SIZEW + framesize)) {
      printf("Stack overflow.  TODO: Automatically grow stack.\n");
      return -1;
    }

    // each additional argument requires 1 byte,
    // we pad to multiples of an instruction
    // the liveness mask follows (one instruction)
    BCIns *return_pc = pc + BC_ROUND(nargs - 1) + 1;

    top[0] = (Word)base;
    top[1] = (Word)return_pc;
    top[2] = (Word)fnode;
    top[3] = arg0;

    // copy arguments
    u1 *arg = (u1*)pc;
    int i;
    for (i = 1; i < nargs; i++, arg++) {
      top[i + 3] = base[*arg];
    }
    // assert: arg <= next_pc - 1

    base = top + STACK_FRAME_SIZEW;
    T->top = base + framesize;
    code = &info->code;
    pc = info->code.code;
    DISPATCH_NEXT;
  }

 op_KLIT:
  {
    u2 lit_id = opC;
    base[opA] = code->lits[lit_id];
    DISPATCH_NEXT;
  }

 op_INITF:
 op_ALLOCAP:
  printf("Unimplemented bytecode\n.");
  return -1;
}

static BCIns test_code[] = {
  BCINS_ABC(BC_ADDRR, 1, 0, 1),
  BCINS_ABC(BC_ADDRR, 1, 0, 1),
  BCINS_AJ(BC_JMP, 0, +1), // skip next instr.
  BCINS_ABC(BC_ADDRR, 1, 0, 1),
  BCINS_AD(BC__MAX, 0, 0) };

// static BCIns

static BCIns silly1_code[] = {
  BCINS_AD(BC_KINT, 0, 42),   // r0 = 42
  BCINS_AD(BC_NEW_INT, 0, 0), // r0 = new(I#, r0)
  BCINS_AD(BC_RET1, 0, 0)     // return r0
};

static ThunkInfoTable silly1_info = {
  .i = DEF_INFO_TABLE(THUNK, 0, 0, 1),
  .name = "silly1",
  .code = {
    .lits = NULL, .sizelits = 0, 
    .littypes = NULL,
    .code = silly1_code, .sizecode = countof(silly1_code),
    .framesize = 1, .arity = 0
  }
};

static Closure silly1 = 
  DEF_CLOSURE(&silly1_info, { 0 });
/*
int main(int argc, char* argv[])
{
  initVM();
  Thread *T0 = createThread(cap0, 1024);

  T0->base[0] = (Word)&silly1; // smallInt(0);
  //printClosure((Closure*)T0->base[0]);

  engine(T0);

  printClosure((Closure*)T0->stack[1]);
  //printf("%0" FMT_WordLen FMT_WordX "\n", T0->stack[1]);
  return 0;
}
*/
int
stackOverflow(Thread* thread, Word* top, u4 increment)
{
  return 0;
}