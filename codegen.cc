/* File: codegen.cc
 * ----------------
 * Implementation for the CodeGenerator class. The methods don't do anything
 * too fancy, mostly just create objects of the various Tac instruction
 * classes and append them to the list.
 */

#include "codegen.h"
#include <string.h>
#include "tac.h"
#include "mips.h"
#include "ast_decl.h"
#include "errors.h"
#include <stack>
#include "hashtable.h"

using namespace std;
  
CodeGenerator::CodeGenerator()
{
  code = new List<Instruction*>();
  labels = new unordered_map<string, Instruction*>;
  deleted_code = new vector<Instruction*>;
  interference_graph = new List<Location*>();
  curGlobalOffset = 0;
}

void CodeGenerator::CreateCFG(int begin)
{
    BeginFunc* bf = dynamic_cast<BeginFunc*>(code->Nth(begin));

    for (int i = begin; i < NumInstructions(); i++)
    {
        EndFunc* ef = dynamic_cast<EndFunc*>(code->Nth(i));
        if (ef) break;

        Goto* gt = dynamic_cast<Goto*>(code->Nth(i));
        if (gt)
        {
            string s(gt->GetLabel());
            gt->AddEdge((*labels)[s]);
            continue;
        }

        IfZ* ifz = dynamic_cast<IfZ*>(code->Nth(i));
        if (ifz)
        {
            string s(ifz->GetLabel());
            ifz->AddEdge((*labels)[s]);
            ifz->AddEdge(code->Nth(i + 1));
            continue;
        }

        Return* ret = dynamic_cast<Return*>(code->Nth(i));
        if (ret) continue;

        code->Nth(i)->AddEdge(code->Nth(i + 1));
    }

    do
    {
        LivenessAnalysis(begin);
    } while (DeadCodeAnalysis(begin));

    BuildInterferenceGraph(begin);
    ColorGraph();

    interference_graph->Clear();
    deleted_code->clear();
}

void CodeGenerator::LivenessAnalysis(int begin)
{
    Instruction* instr;
    Instruction* edge;

    BeginFunc* bf = dynamic_cast<BeginFunc*>(code->Nth(begin));

    bool changed = true;
    List<Location*> empty;
    for (int i = begin; i < NumInstructions(); i++)
        code->Nth(i)->in_set = empty;

    while (changed)
    {
        changed = false;

        for (int i = begin; i < NumInstructions(); i++)
        {
            List<Location*> out_set;
            instr = code->Nth(i);

            for (int j = 0; j < instr->GetNumEdges(); j++)
            {
                edge = instr->GetEdge(j);
                bool deleted = false;

                for (int k = 0; k < deleted_code->size(); k++)
                {
                    if (edge == (*deleted_code)[k])
                    {
                        deleted = true;
                        break;    
                    }
                }

                if (deleted) continue;

                for (int k = 0; k < edge->in_set.NumElements(); k++)
                {
                    Location* el = edge->in_set.Nth(k);
                    bool found = false;

                    for (int a = 0; a < out_set.NumElements(); a++)
                    {
                        if (out_set.Nth(a) == el)
                        {
                            found = true;
                            break;
                        }
                    }

                    if (!found) 
                        out_set.Append(el);
                }
            }

            instr->out_set = out_set;
            List<Location*> in_set_prime = out_set;
            List<Location*> kill_set = instr->MakeKillSet();
            List<Location*> gen_set = instr->MakeGenSet();

            for (int j = 0; j < in_set_prime.NumElements(); j++)
            {
                for (int k = 0; k < kill_set.NumElements(); k++)
                {
                    if (in_set_prime.Nth(j) == kill_set.Nth(k))
                    {
                        in_set_prime.RemoveAt(j);
                        j--;
                        break;
                    }
                }
            }

            for (int j = 0; j < gen_set.NumElements(); j++)
            {
                bool found = false;
                for (int k = 0; k < in_set_prime.NumElements(); k++)
                {
                    if (in_set_prime.Nth(k) == gen_set.Nth(j))
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    in_set_prime.Append(gen_set.Nth(j));
            }

            List<Location*> temp_prime_set = in_set_prime;

            bool present_out_set = false;
            bool sets_different = false;

            for (int j = 0; j < instr->in_set.NumElements(); j++)
            {
                for (int k = 0; k < temp_prime_set.NumElements(); k++)
                {
                    if (instr->in_set.Nth(j) == temp_prime_set.Nth(k))
                    {
                        temp_prime_set.RemoveAt(k);
                        present_out_set = true;
                        break;
                    }
                }

                if (present_out_set)
                {
                    present_out_set = false;
                    continue;
                }

                else 
                {
                    sets_different = true;
                    break;
                }
            }

            if (sets_different || temp_prime_set.NumElements() != 0)
            {
                instr->in_set = in_set_prime;
                changed = true;
            }

        }
    }
}

bool CodeGenerator::DeadCodeAnalysis(int begin)
{
    Instruction* instr;
    bool changed = false;

    for (int i = begin; i < NumInstructions(); i++)
    {
        instr = code->Nth(i);
        if (instr->IsDead())
        {
            code->RemoveAt(i);
            i--;
            changed = true;
            deleted_code->push_back(instr);
        }
    }

    return changed;
}

void CodeGenerator::BuildInterferenceGraph(int begin)
{
    List<Location*> kill_set, in_set, out_set;

    in_set = code->Nth(begin)->in_set;
    interference_graph->AppendAll(in_set);
    interference_graph->Unique();

    for (int i = 0; i < in_set.NumElements(); i++)
    {
        for (int j = i + 1; j < in_set.NumElements(); j++)
            in_set.Nth(i)->AddEdge(in_set.Nth(j));
    }

    for (int i = begin + 1; i < NumInstructions(); i++)
    {
        out_set = code->Nth(i)->out_set;
        kill_set = code->Nth(i)->MakeKillSet();
        interference_graph->AppendAll(out_set);
        interference_graph->Unique();

        for (int j = 0; j < kill_set.NumElements(); j++)
        {
            for (int k = 0; k < out_set.NumElements(); k++)
            {
                if (out_set.Nth(k) != kill_set.Nth(j))
                    out_set.Nth(k)->AddEdge(kill_set.Nth(j));
            }
        }
    }
}

void CodeGenerator::ColorGraph()
{
    bool can_still_color = false;
    stack<Location*> degree;
    List<Location*> removed;

    if (interference_graph->NumElements() == 0) return;

    while (!can_still_color)
    {
        int temp;

        while ((temp = FindNode(removed)) != -1)
        {
            Location* sat = interference_graph->Nth(temp);
            removed.Append(sat);
            degree.push(sat);
        }

        if (removed.NumElements() == interference_graph->NumElements())
        {
            Location* node = degree.top();
            degree.pop();
            node->SetRegister(Mips::Register(8));
            if (!strcmp(node->GetName(), "this"))
                node->SetRegister(Mips::Register(3));
            bool same_reg = false;
            while (!degree.empty())
            {
                node = degree.top();
                degree.pop();

                if (!strcmp(node->GetName(), "this"))
                {
                    node->SetRegister(Mips::Register(3));
                    continue;
                }

                for (int i = 8; i <= 25; i++)
                {
                    for (int j = 0; j < node->GetNumEdges(); j++)
                    {
                        if (node->GetEdge(j)->GetRegister() == Mips::Register(i))
                        {
                            same_reg = true;
                            break;
                        }
                    }

                    if (same_reg)
                    {
                        same_reg = false;
                        continue;
                    }

                    else 
                    {
                        node->SetRegister(Mips::Register(i));
                        break;
                    }
                }
            }

            can_still_color = true;
        }

        else 
        {
            int ind = FindMaxK(removed);
            if (ind != -1)
                removed.Append(interference_graph->Nth(ind));
        }
    }
}

int CodeGenerator::FindNode(List<Location*> removed)
{
    int count = 0;
    for (int i = 0; i < interference_graph->NumElements(); i++)
    {
        int num_edges = interference_graph->Nth(i)->GetNumEdges();

        if (Removed(interference_graph->Nth(i), removed)) continue;

        if (num_edges < Mips::NumGeneralPurposeRegs) return i;

        else 
        {
            for (int j = 0; j < num_edges; j++)
            {
                if (!Removed(interference_graph->Nth(i)->GetEdge(j), removed))
                    count++;
            }

            if (count < Mips::NumGeneralPurposeRegs) return i;
        }
    }

    return -1;
}

int CodeGenerator::FindMaxK(List<Location*> removed)
{
    int max = -1;
    int count = 0;
    int ind = -1;
    for (int i = 0; i < interference_graph->NumElements(); i++)
    {
        if (Removed(interference_graph->Nth(i), removed))
            continue;
        
        count = 0;

        for (int j = 0; j < interference_graph->Nth(i)->GetNumEdges(); j++)
        {
            if (Removed(interference_graph->Nth(i)->GetEdge(j), removed))
                continue;
            
            else
                count++;
        }

        if (count > max)
        {
            max = count;
            ind = i;
        }
    }

    return ind;
}

bool CodeGenerator::Removed(Location* check, List<Location*> removed)
{
    for (int i = 0; i < removed.NumElements(); i++)
    {
        if (check == removed.Nth(i))
            return true;
    }

    return false;
}

char *CodeGenerator::NewLabel()
{
  static int nextLabelNum = 0;
  char temp[10];
  sprintf(temp, "_L%d", nextLabelNum++);
  return strdup(temp);
}


Location *CodeGenerator::GenTempVariable()
{
  static int nextTempNum;
  char temp[10];
  Location *result = NULL;
  sprintf(temp, "_tmp%d", nextTempNum++);
  return GenLocalVariable(temp);
}

  
Location *CodeGenerator::GenLocalVariable(const char *varName)
{            
    curStackOffset -= VarSize;
    Location *loc = new Location(fpRelative, curStackOffset+4, varName);
    return loc;
}

Location *CodeGenerator::GenGlobalVariable(const char *varName)
{
    curGlobalOffset += VarSize;
    Location *loc = new Location(gpRelative, curGlobalOffset-4, varName);
    return loc;
}

Location *CodeGenerator::GenParameter(int index, const char *varName)
{
    Location *loc = new Location(fpRelative, OffsetToFirstParam+index*VarSize, varName);
    return loc;
}

Location *CodeGenerator::GenIndirect(Location* base, int offset)
{
    Location *loc = new Location(base, offset);
    return loc;
}


Location *CodeGenerator::GenLoadConstant(int value)
{
  Location *result = GenTempVariable();
  code->Append(new LoadConstant(result, value));
  return result;
}

Location *CodeGenerator::GenLoadConstant(const char *s)
{
  Location *result = GenTempVariable();
  code->Append(new LoadStringConstant(result, s));
  return result;
} 

Location *CodeGenerator::GenLoadLabel(const char *label)
{
  Location *result = GenTempVariable();
  code->Append(new LoadLabel(result, label));
  return result;
} 


void CodeGenerator::GenAssign(Location *dst, Location *src)
{
  code->Append(new Assign(dst, src));
}


Location *CodeGenerator::GenLoad(Location *ref, int offset)
{
  Location *result = GenTempVariable();
  code->Append(new Load(result, ref, offset));
  return result;
}

void CodeGenerator::GenStore(Location *dst,Location *src, int offset)
{
  code->Append(new Store(dst, src, offset));
}


Location *CodeGenerator::GenBinaryOp(const char *opName, Location *op1,
						     Location *op2)
{
  Location *result = GenTempVariable();
  code->Append(new BinaryOp(BinaryOp::OpCodeForName(opName), result, op1, op2));
  return result;
}


void CodeGenerator::GenLabel(const char *label)
{
    Instruction *instr = new Label(label);
    code->Append(instr);
    string s = label;
    (*labels)[s] = instr;
}

void CodeGenerator::GenIfZ(Location *test, const char *label)
{
  code->Append(new IfZ(test, label));
}

void CodeGenerator::GenGoto(const char *label)
{
  code->Append(new Goto(label));
}

void CodeGenerator::GenReturn(Location *val)
{
  code->Append(new Return(val));
}


BeginFunc *CodeGenerator::GenBeginFunc(FnDecl* fn)
{
    BeginFunc *result = new BeginFunc;
    code->Append(inside_fn = result);
    List<VarDecl*>*formals = fn->GetFormals();
    int start = OffsetToFirstParam;
    
    if (fn->IsMethodDecl()) start += VarSize;
    
    for (int i = 0; i < formals->NumElements(); i++)
    {
        Location* param = new Location(fpRelative, i*VarSize + start, formals->Nth(i)->GetName());
        formals->Nth(i)->rtLoc = param;
        result->AddParameter(param);
    }

    curStackOffset = OffsetToFirstLocal;
    result->CheckMethod(fn);

    return result;
}

void CodeGenerator::GenEndFunc()
{
    code->Append(new EndFunc());
    inside_fn->SetFrameSize(OffsetToFirstLocal - curStackOffset);
    inside_fn = NULL;
}

void CodeGenerator::GenPushParam(Location *param)
{
  code->Append(new PushParam(param));
}

void CodeGenerator::GenPopParams(int numBytesOfParams)
{
  Assert(numBytesOfParams >= 0 && numBytesOfParams % VarSize == 0); // sanity check
  if (numBytesOfParams > 0)
    code->Append(new PopParams(numBytesOfParams));
}

Location *CodeGenerator::GenLCall(const char *label, bool fnHasReturnValue)
{
  Location *result = fnHasReturnValue ? GenTempVariable() : NULL;
  code->Append(new LCall(label, result));
  return result;
}
  
Location *CodeGenerator::GenFunctionCall(const char *fnLabel, List<Location*> *args, bool hasReturnValue)
{
  for (int i = args->NumElements()-1; i >= 0; i--) // push params right to left
    GenPushParam(args->Nth(i));
  Location *result = GenLCall(fnLabel, hasReturnValue);
  GenPopParams(args->NumElements()*VarSize);
  return result;
}

Location *CodeGenerator::GenACall(Location *fnAddr, bool fnHasReturnValue)
{
  Location *result = fnHasReturnValue ? GenTempVariable() : NULL;
  code->Append(new ACall(fnAddr, result));
  return result;
}
  
Location *CodeGenerator::GenMethodCall(Location *rcvr,
			     Location *meth, List<Location*> *args, bool fnHasReturnValue)
{
  for (int i = args->NumElements()-1; i >= 0; i--)
    GenPushParam(args->Nth(i));
  GenPushParam(rcvr);	// hidden "this" parameter
  Location *result= GenACall(meth, fnHasReturnValue);
  GenPopParams((args->NumElements()+1)*VarSize);
  return result;
}
 
 
static struct _builtin {
  const char *label;
  int numArgs;
  bool hasReturn;
} builtins[] =
 {{"_Alloc", 1, true},
  {"_ReadLine", 0, true},
  {"_ReadInteger", 0, true},
  {"_StringEqual", 2, true},
  {"_PrintInt", 1, false},
  {"_PrintString", 1, false},
  {"_PrintBool", 1, false},
  {"_Halt", 0, false}};

Location *CodeGenerator::GenBuiltInCall(BuiltIn bn,Location *arg1, Location *arg2)
{
  Assert(bn >= 0 && bn < NumBuiltIns);
  struct _builtin *b = &builtins[bn];
  Location *result = NULL;

  if (b->hasReturn) result = GenTempVariable();
                // verify appropriate number of non-NULL arguments given
  Assert((b->numArgs == 0 && !arg1 && !arg2)
	|| (b->numArgs == 1 && arg1 && !arg2)
	|| (b->numArgs == 2 && arg1 && arg2));
  if (arg2) code->Append(new PushParam(arg2));
  if (arg1) code->Append(new PushParam(arg1));
  code->Append(new LCall(b->label, result));
  GenPopParams(VarSize*b->numArgs);
  return result;
}


void CodeGenerator::GenVTable(const char *className, List<const char *> *methodLabels)
{
  code->Append(new VTable(className, methodLabels));
}


void CodeGenerator::DoFinalCodeGen()
{

  // BuildCFG();
  // LiveVariableAnalysis();
  // BuildInterferenceGraph();
  // ColorGraph();

  if (IsDebugOn("tac")) { // if debug don't translate to mips, just print Tac
    for (int i = 0; i < code->NumElements(); i++)
      code->Nth(i)->Print();
  } else {
    Mips mips;
    mips.EmitPreamble();
    for (int i = 0; i < code->NumElements(); i++)
      code->Nth(i)->Emit(&mips);
  }
}



Location *CodeGenerator::GenArrayLen(Location *array)
{
  return GenLoad(array, -4);
}

Location *CodeGenerator::GenNew(const char *vTableLabel, int instanceSize)
{
  Location *size = GenLoadConstant(instanceSize);
  Location *result = GenBuiltInCall(Alloc, size);
  Location *vt = GenLoadLabel(vTableLabel);
  GenStore(result, vt);
  return result;
}


Location *CodeGenerator::GenDynamicDispatch(Location *rcvr, int vtableOffset, List<Location*> *args, bool hasReturnValue)
{
  Location *vptr = GenLoad(rcvr); // load vptr
  Assert(vtableOffset >= 0);
  Location *m = GenLoad(vptr, vtableOffset*4);
  return GenMethodCall(rcvr, m, args, hasReturnValue);
}

// all variables (ints, bools, ptrs, arrays) are 4 bytes in for code generation
// so this simplifies the math for offsets
Location *CodeGenerator::GenSubscript(Location *array, Location *index)
{
  Location *zero = GenLoadConstant(0);
  Location *isNegative = GenBinaryOp("<", index, zero);
  Location *count = GenLoad(array, -4);
  Location *isWithinRange = GenBinaryOp("<", index, count);
  Location *pastEnd = GenBinaryOp("==", isWithinRange, zero);
  Location *outOfRange = GenBinaryOp("||", isNegative, pastEnd);
  const char *pastError = NewLabel();
  GenIfZ(outOfRange, pastError);
  GenHaltWithMessage(err_arr_out_of_bounds);
  GenLabel(pastError);
  Location *four = GenLoadConstant(VarSize);
  Location *offset = GenBinaryOp("*", four, index);
  Location *elem = GenBinaryOp("+", array, offset);
  return GenIndirect(elem, 0); 
}



Location *CodeGenerator::GenNewArray(Location *numElems)
{
  Location *one = GenLoadConstant(1);
  Location *isNonpositive = GenBinaryOp("<", numElems, one);
  const char *pastError = NewLabel();
  GenIfZ(isNonpositive, pastError);
  GenHaltWithMessage(err_arr_bad_size);
  GenLabel(pastError);

  // need (numElems+1)*VarSize total bytes (extra 1 is for length)
  Location *arraySize = GenLoadConstant(1);
  Location *num = GenBinaryOp("+", arraySize, numElems);
  Location *four = GenLoadConstant(VarSize);
  Location *bytes = GenBinaryOp("*", num, four);
  Location *result = GenBuiltInCall(Alloc, bytes);
  GenStore(result, numElems);
  return GenBinaryOp("+", result, four);
}

void CodeGenerator::GenHaltWithMessage(const char *message)
{
   Location *msg = GenLoadConstant(message);
   GenBuiltInCall(PrintString, msg);
   GenBuiltInCall(Halt, NULL);
}

