/* File: tac.cc
 * ------------
 * Implementation of Location class and Instruction class/subclasses.
 */
  
#include "tac.h"
#include "mips.h"
#include <string.h>
#include <deque>

Location::Location(Segment s, int o, const char *name) :
  variableName(strdup(name)), segment(s), offset(o),
  reference(NULL), reg(Mips::zero) 
  {
      edges = new List<Location*>();
  }

void Location::AddEdge(Location* edge, bool recall)
{
    if (!recall)
    {
        for (int i = 0; i < GetNumEdges(); i++)
        {
            if (edge == edges->Nth(i)) return;
        }

        edges->Append(edge);
        edge->AddEdge(this, true);
    }

    else edges->Append(edge);
}

int Location::GetNumEdges()
{
    return edges->NumElements();
}

Location* Location::GetEdge(int n)
{
    return edges->Nth(n);
}

void Location::RemoveAllEdges()
{
    edges->Clear();
}

 
void Instruction::Print() {
  printf("\t%s ;", printed);
  printf("\n");
}

void Instruction::Emit(Mips *mips) {
  if (*printed)
    mips->Emit("# %s", printed);   // emit TAC as comment into assembly
  EmitSpecific(mips);
}

LoadConstant::LoadConstant(Location *d, int v)
  : dst(d), val(v) {
  Assert(dst != NULL);
  sprintf(printed, "%s = %d", dst->GetName(), val);
}
void LoadConstant::EmitSpecific(Mips *mips) {
  mips->EmitLoadConstant(dst, val);
}
List<Location*> LoadConstant::MakeKillSet()
{
    List<Location*> set;
    set.Append(dst);
    return set;
}
bool LoadConstant::IsDead()
{
    for (int i = 0; i < out_set.NumElements(); i++)
        if (out_set.Nth(i) == dst) return false;

    return true;
}



LoadStringConstant::LoadStringConstant(Location *d, const char *s)
  : dst(d) {
  Assert(dst != NULL && s != NULL);
  const char *quote = (*s == '"') ? "" : "\"";
  str = new char[strlen(s) + 2*strlen(quote) + 1];
  sprintf(str, "%s%s%s", quote, s, quote);
  quote = (strlen(str) > 50) ? "...\"" : "";
  sprintf(printed, "%s = %.50s%s", dst->GetName(), str, quote);
}
void LoadStringConstant::EmitSpecific(Mips *mips) {
  mips->EmitLoadStringConstant(dst, str);
}
List<Location*> LoadStringConstant::MakeKillSet()
{
    List<Location*> set;
    set.Append(dst);
    return set;
}
bool LoadStringConstant::IsDead()
{
    for (int i = 0; i < out_set.NumElements(); i++)
        if (out_set.Nth(i) == dst) return false;

    return true;
}

     

LoadLabel::LoadLabel(Location *d, const char *l)
  : dst(d), label(strdup(l)) {
  Assert(dst != NULL && label != NULL);
  sprintf(printed, "%s = %s", dst->GetName(), label);
}
void LoadLabel::EmitSpecific(Mips *mips) {
  mips->EmitLoadLabel(dst, label);
}



Assign::Assign(Location *d, Location *s)
  : dst(d), src(s) {
  Assert(dst != NULL && src != NULL);
  sprintf(printed, "%s = %s", dst->GetName(), src->GetName());
}
void Assign::EmitSpecific(Mips *mips) {
  mips->EmitCopy(dst, src);
}
List<Location*> Assign::MakeKillSet()
{
    List<Location*> set;
    set.Append(dst);
    return set;
}
List<Location*> Assign::MakeGenSet()
{
    List<Location*> set;
    set.Append(src);
    return set;
}
bool Assign::IsDead()
{
    for (int i = 0; i < out_set.NumElements(); i++)
        if (out_set.Nth(i) == dst) return false;

    return true;
}



Load::Load(Location *d, Location *s, int off)
  : dst(d), src(s), offset(off) {
  Assert(dst != NULL && src != NULL);
  if (offset) 
    sprintf(printed, "%s = *(%s + %d)", dst->GetName(), src->GetName(), offset);
  else
    sprintf(printed, "%s = *(%s)", dst->GetName(), src->GetName());
}
void Load::EmitSpecific(Mips *mips) {
  mips->EmitLoad(dst, src, offset);
}

List<Location*> Load::MakeKillSet()
{
  List<Location*> kill_set;
  kill_set.Append(src);
  return kill_set;
}

List<Location*> Load::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(dst);
  return gen_set;
}

Store::Store(Location *d, Location *s, int off)
  : dst(d), src(s), offset(off) {
  Assert(dst != NULL && src != NULL);
  if (offset)
    sprintf(printed, "*(%s + %d) = %s", dst->GetName(), offset, src->GetName());
  else
    sprintf(printed, "*(%s) = %s", dst->GetName(), src->GetName());
}
void Store::EmitSpecific(Mips *mips) {
  mips->EmitStore(dst, src, offset);
}

List<Location*> Store::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(src);
  gen_set.Append(dst);
  return gen_set;
}
 
const char * const BinaryOp::opName[Mips::NumOps]  = {"+", "-", "*", "/", "%", "==", "<", "&&", "||"};;

Mips::OpCode BinaryOp::OpCodeForName(const char *name) {
  for (int i = 0; i < Mips::NumOps; i++) 
    if (opName[i] && !strcmp(opName[i], name))
	return (Mips::OpCode)i;
  Failure("Unrecognized Tac operator: '%s'\n", name);
  return Mips::Add; // can't get here, but compiler doesn't know that
}

BinaryOp::BinaryOp(Mips::OpCode c, Location *d, Location *o1, Location *o2)
  : code(c), dst(d), op1(o1), op2(o2) {
  Assert(dst != NULL && op1 != NULL && op2 != NULL);
  Assert(code >= 0 && code < Mips::NumOps);
  sprintf(printed, "%s = %s %s %s", dst->GetName(), op1->GetName(), opName[code], op2->GetName());
}
void BinaryOp::EmitSpecific(Mips *mips) {	  
  mips->EmitBinaryOp(code, dst, op1, op2);
}

List<Location*> BinaryOp::MakeKillSet()
{
  List<Location*> kill_set;
  kill_set.Append(dst);
  return kill_set;
}

List<Location*> BinaryOp::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(op1);
  gen_set.Append(op2);
  return gen_set;
}

bool BinaryOp::IsDead()
{
  for(int i = 0; i < out_set.NumElements(); i++)
  {
    if(out_set.Nth(i) == dst)
    {
      return false;
    }
  }
  return true;
}

Label::Label(const char *l) : label(strdup(l)) {
  Assert(label != NULL);
  *printed = '\0';
}
void Label::Print() {
  printf("%s:\n", label);
}
void Label::EmitSpecific(Mips *mips) {
  mips->EmitLabel(label);
}
 
Goto::Goto(const char *l) : label(strdup(l)) {
  Assert(label != NULL);
  sprintf(printed, "Goto %s", label);
}
void Goto::EmitSpecific(Mips *mips) {	  
  mips->EmitGoto(label);
}

IfZ::IfZ(Location *te, const char *l)
   : test(te), label(strdup(l)) {
  Assert(test != NULL && label != NULL);
  sprintf(printed, "IfZ %s Goto %s", test->GetName(), label);
}
void IfZ::EmitSpecific(Mips *mips) {	  
  mips->EmitIfZ(test, label);
}

List<Location*> IfZ::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(test);
  return gen_set;
}

BeginFunc::BeginFunc() {
  sprintf(printed,"BeginFunc (unassigned)");
  frameSize = -555; // used as sentinel to recognized unassigned value
}
void BeginFunc::SetFrameSize(int numBytesForAllLocalsAndTemps) {
  frameSize = numBytesForAllLocalsAndTemps; 
  sprintf(printed,"BeginFunc %d", frameSize);
}
void BeginFunc::EmitSpecific(Mips *mips) {
  mips->EmitBeginFunction(frameSize);

  //Now we need to do caller save stuff
  Location* fp = new Location(fpRelative, -800-frameSize, "framePointer");
  fp->SetRegister(Mips::Register(30));

  if(is_method)
  {
    for(int i = 0; i < in_set.NumElements(); i++)
    {
      if(!strcmp(in_set.Nth(i)->GetName(), "this"))
      {
        mips->EmitLoad(in_set.Nth(i), fp, 4);
      }
    }
  }

  for(int i = 0; i < parameters.NumElements(); i++)
  {
    mips->EmitLoad(parameters.Nth(i), fp, i * 4 + 4 + 4 * is_method);
  }
}

void BeginFunc::AddParameter(Location* param)
{
  parameters.Append(param);
}

void BeginFunc::CheckMethod(FnDecl* fn)
{
  is_method = fn->IsMethodDecl();
}

EndFunc::EndFunc() : Instruction() {
  sprintf(printed, "EndFunc");
}
void EndFunc::EmitSpecific(Mips *mips) {
  mips->EmitEndFunction();
}


 
Return::Return(Location *v) : val(v) {
  sprintf(printed, "Return %s", val? val->GetName() : "");
}
void Return::EmitSpecific(Mips *mips) {	  
  mips->EmitReturn(val);
}

List<Location*> Return::MakeGenSet()
{
  List<Location*> gen_set;
  if(val)
  {
    gen_set.Append(val);
  }
  return gen_set;
}

PushParam::PushParam(Location *p)
  :  param(p) {
  Assert(param != NULL);
  sprintf(printed, "PushParam %s", param->GetName());
}
void PushParam::EmitSpecific(Mips *mips) {
  mips->EmitParam(param);
} 

List<Location*> PushParam::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(param);
  return gen_set;
}

PopParams::PopParams(int nb)
  :  numBytes(nb) {
  sprintf(printed, "PopParams %d", numBytes);
}
void PopParams::EmitSpecific(Mips *mips) {
  mips->EmitPopParams(numBytes);
} 

LCall::LCall(const char *l, Location *d)
  :  label(strdup(l)), dst(d) {
  sprintf(printed, "%s%sLCall %s", dst? dst->GetName(): "", dst?" = ":"", label);
}
void LCall::EmitSpecific(Mips *mips) {
//   mips->EmitLCall(dst, label);

  //Now we need to do caller save stuff
  for(int i = 0; i < in_set.NumElements(); i++)
  {
    mips->SaveCaller(in_set.Nth(i));
  }
  mips->EmitLCall(dst, label);
  for(int i = 0; i < in_set.NumElements(); i++)
  {
    mips->RestoreCaller(in_set.Nth(i));
  }
}

List<Location*> LCall::MakeKillSet()
{
  List<Location*> kill_set;
  if(dst)
  {
    kill_set.Append(dst);
  }
  return kill_set;
}

ACall::ACall(Location *ma, Location *d)
  : dst(d), methodAddr(ma) {
  Assert(methodAddr != NULL);
  sprintf(printed, "%s%sACall %s", dst? dst->GetName(): "", dst?" = ":"",
	    methodAddr->GetName());
}
void ACall::EmitSpecific(Mips *mips) {
  mips->EmitACall(dst, methodAddr);

  //Now we need to do caller save stuff
  for(int i = 0; i < out_set.NumElements(); i++)
  {
    if(out_set.Nth(i) != dst)
    {
      mips->SaveCaller(out_set.Nth(i));
    }
  }
  mips->EmitACall(dst, methodAddr);
  for(int i = 0; i < out_set.NumElements(); i++)
  {
    if(out_set.Nth(i) != dst)
    {
      mips->RestoreCaller(out_set.Nth(i));
    }
  }
} 

List<Location*> ACall::MakeGenSet()
{
  List<Location*> gen_set;
  gen_set.Append(methodAddr);
  return gen_set;
}

List<Location*> ACall::MakeKillSet()
{
  List<Location*> kill_set;
  if(dst)
  {
    kill_set.Append(dst);
  }
  return kill_set;
}

VTable::VTable(const char *l, List<const char *> *m)
  : methodLabels(m), label(strdup(l)) {
  Assert(methodLabels != NULL && label != NULL);
  sprintf(printed, "VTable for class %s", l);
}

void VTable::Print() {
  printf("VTable %s =\n", label);
  for (int i = 0; i < methodLabels->NumElements(); i++) 
    printf("\t%s,\n", methodLabels->Nth(i));
  printf("; \n"); 
}
void VTable::EmitSpecific(Mips *mips) {
  mips->EmitVTable(label, methodLabels);
}
