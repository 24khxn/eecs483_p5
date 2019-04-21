/* File: codegen.cc
 * ----------------
 * Implementation for the CodeGenerator class. The methods don't do anything
 * too fancy, mostly just create objects of the various Tac instruction
 * classes and append them to the list.
 */

#include "codegen.h"
#include "hashtable.h"
#include <string.h>
#include "tac.h"
#include <stack>
#include "mips.h"

CodeGenerator::CodeGenerator()
{
    code = new List<Instruction *>();
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

void CodeGenerator::GenStore(Location *dst, Location *src, int offset)
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
    code->Append(new Label(label));
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

BeginFunc *CodeGenerator::GenBeginFunc()
{
    BeginFunc *result = new BeginFunc;
    code->Append(result);
    return result;
}

void CodeGenerator::GenEndFunc()
{
    code->Append(new EndFunc());
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

Location *CodeGenerator::GenACall(Location *fnAddr, bool fnHasReturnValue)
{
    Location *result = fnHasReturnValue ? GenTempVariable() : NULL;
    code->Append(new ACall(fnAddr, result));
    return result;
}

static struct _builtin
{
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

Location *CodeGenerator::GenBuiltInCall(BuiltIn bn, Location *arg1, Location *arg2)
{
    Assert(bn >= 0 && bn < NumBuiltIns);
    struct _builtin *b = &builtins[bn];
    Location *result = NULL;

    if (b->hasReturn)
        result = GenTempVariable();
    // verify appropriate number of non-NULL arguments given
    Assert((b->numArgs == 0 && !arg1 && !arg2) || (b->numArgs == 1 && arg1 && !arg2) || (b->numArgs == 2 && arg1 && arg2));
    if (arg2)
        code->Append(new PushParam(arg2));
    if (arg1)
        code->Append(new PushParam(arg1));
    code->Append(new LCall(b->label, result));
    GenPopParams(VarSize * b->numArgs);
    return result;
}

void CodeGenerator::GenVTable(const char *className, List<const char *> *methodLabels)
{
    code->Append(new VTable(className, methodLabels));
}

void CodeGenerator::DoFinalCodeGen()
{
    BuildCFG();
    LiveVariableAnalysis();
    BuildInterferenceGraph();
    ColorGraph();

    if (IsDebugOn("tac"))
    { // if debug don't translate to mips, just print Tac
        for (int i = 0; i < code->NumElements(); i++)
            code->Nth(i)->Print();
    }
    else
    {
        Mips mips;
        mips.EmitPreamble();
        for (int i = 0; i < code->NumElements(); i++)
            code->Nth(i)->Emit(&mips);
    }
}

void CodeGenerator::BuildCFG()
{
    Hashtable<Instruction*> label_to_TAC;

    for (int i = 0; i < code->NumElements() - 1; i++)
    {
        if (auto lt = dynamic_cast<Label*>(code->Nth(i)))
        {
            label_to_TAC.Enter(lt->GetLabel(), code->Nth(i+1));
        }
    }

    for (int i = 0; i < code->NumElements() - 1; i++)
    {
        auto tac = code->Nth(i);

        if (dynamic_cast<EndFunc*>(code->Nth(i)))
            continue;
        
        else if (auto ifz_tac = dynamic_cast<IfZ*>(tac))
        {
            auto jump_tac = label_to_TAC.Lookup(ifz_tac->GetLabel());
            ifz_tac->next.Append(jump_tac);
            jump_tac->prev.Append(ifz_tac);
        }
        else 
        {
            tac->next.Append(code->Nth(i+1));
            code->Nth(i+1)->prev.Append(tac);
        }
    }
}

void CodeGenerator::LiveVariableAnalysis()
{
    // pulled straight from the pseudocode in spec

    bool changed = true;
    while (changed)
    {
        changed = false;
        // looping backwards for faster convergence
        for (int i = code->NumElements() - 1; i >= 0; i--)
        {
            auto tac = code->Nth(i);

            LiveVars_t* out_set = new LiveVars_t;
            for (int j = 0; j < tac->next.NumElements(); j++)
            {
                auto next_lives = tac->next.Nth(j)->live_vars_in;
                out_set->insert(next_lives->begin(), next_lives->end());
            }

            if (*out_set != *(tac->live_vars_out))
                changed = true;
            
            tac->live_vars_out = out_set;
            *(tac->live_vars_out) = *out_set;

            auto gen_set = tac->GetGenVars();
            auto kill_set = tac->GetKillVars();

            for (auto kloc : *(kill_set))
                tac->live_vars_in->erase(kloc);
            
            tac->live_vars_in->insert(gen_set->begin(), gen_set->end());
        }
    }
}

void CodeGenerator::BuildInterferenceGraph()
{
    InterferenceGraph_t* current = NULL;

    for (int i = 0; i < code->NumElements(); i++)
    {
        auto tac = code->Nth(i);
        if (auto beginfunc_tac = dynamic_cast<BeginFunc*> (tac))
            current = &(beginfunc_tac->interference_graph);

        if (current)
        {
            for (auto from_tac : *(tac->live_vars_in))
            {
                if (current->find(from_tac) == current->end())
                    (*current)[from_tac] = {};
                
                for (auto to_tac : *(tac->live_vars_in))
                {
                    if (from_tac != to_tac)
                        (*current)[from_tac].insert(to_tac);
                }
            }

            for (auto kill_tac : *(tac->GetKillVars()))
            {
                if (current->find(kill_tac) == current->end())
                    (*current)[kill_tac] = {};
                
                for (auto out_tac : *(tac->live_vars_out))
                {
                    if (kill_tac != out_tac)
                    {
                        (*current)[kill_tac].insert(out_tac);
                        (*current)[out_tac].insert(kill_tac);
                    }
                }
            }
        }
    }
}

void CodeGenerator::ColorGraph()
{
    InterferenceGraph_t* current = nullptr;

    for (int i = 0; i < code->NumElements(); i++)
    {
        auto tac = code->Nth(i);

        if (auto beginfunc_tac = dynamic_cast<BeginFunc*> (tac))
        {
            current = &(beginfunc_tac->interference_graph);
            std::stack<Location*> nodes_remove;
            InterferenceGraph_t edges_remove;

            while (!current->empty())
            {
                Location* max = nullptr;
                int max_size = -1;

                for (auto& kv: *current)
                {
                    if ( ((int) kv.second.size()) > max_size)
                    {
                        max = kv.first;
                        max_size = (int) kv.second.size();
                    }
                }

                nodes_remove.push(max);
                edges_remove[max] = (*current)[max];
                current->erase(max);

                for (auto& kv: *current)
                    kv.second.erase(max);

            }

            while (!nodes_remove.empty())
            {
                auto node = nodes_remove.top();
                nodes_remove.pop();
                std::set<Mips::Register> gen_purp_regs = {
                    Mips::t0, Mips::t1, Mips::t2, Mips::t3, Mips::t4, Mips::t5,
                    Mips::t6, Mips::t7, Mips::t8, Mips::t9, Mips::s0, Mips::s1,
                    Mips::s2, Mips::s3, Mips::s4, Mips::s5, Mips::s6, Mips::s7
                };

                for (auto to_node: edges_remove[node])
                    gen_purp_regs.erase(to_node->GetRegister());
                
                node->SetRegister(*(gen_purp_regs.begin()));
                edges_remove.erase(node);
            }
        }
    }
}


