/* File: tac.h
 * -----------
 * This module contains the Instruction class (and its various
 * subclasses) that represent Tac instructions and the Location
 * class used for operands to those instructions.
 *
 * Each instruction is mostly just a little struct with a
 * few fields, but each responds polymorphically to the methods
 * Print and Emit, the first is used to print out the TAC form of
 * the instruction (helpful when debugging) and the second to
 * convert to the appropriate MIPS assembly.
 *
 * The operands to each instruction are of Location class.
 * A Location object is a simple representation of where a variable
 * exists at runtime, i.e. whether it is on the stack or global
 * segment and at what offset relative to the current fp or gp.
 *
 * You may need to make changes/extensions to these classes
 * if you are working on IR optimization.

 */

#ifndef _H_tac
#define _H_tac

#include "list.h" // for VTable
#include "mips.h"
#include <set>
#include <map>

// A Location object is used to identify the operands to the
// various TAC instructions. A Location is either fp or gp
// relative (depending on whether in stack or global segemnt)
// and has an offset relative to the base of that segment.
// For example, a declaration for integer num as the first local
// variable in a function would be assigned a Location object
// with name "num", segment fpRelative, and offset -8.

typedef enum
{
    fpRelative,
    gpRelative
} Segment;

class Location
{
  protected:
    const char *variableName;
    Segment segment;
    int offset;

    // The register allocated to this location.
    // A "zero" indicates that no register has been allocated.
    Mips::Register reg;

  public:
    Location(Segment seg, int offset, const char *name);

    const char *GetName() { return variableName; }
    Segment GetSegment() { return segment; }
    int GetOffset() { return offset; }
    void SetRegister(Mips::Register r) { reg = r; }
    Mips::Register GetRegister() { return reg; }
};

struct LocationComparator
{
    bool operator()(Location *lhs, Location *rhs) const
    {
        if (strcmp(lhs->GetName(), rhs->GetName()) != 0)
            return strcmp(lhs->GetName(), rhs->GetName()) < 0;
        
        else if (lhs->GetSegment() != rhs->GetSegment())
            return lhs->GetSegment() < rhs->GetSegment();

        else 
            return lhs->GetOffset() < rhs->GetOffset();
    }
};

using LiveVars_t = std::set<Location*, LocationComparator>;
using InterferenceGraph_t = std::map<Location*, std::set<Location*, LocationComparator>, LocationComparator>; 

// base class from which all Tac instructions derived
// has the interface for the 2 polymorphic messages: Print & Emit

class Instruction
{
  protected:
    char printed[128];

  public:
    virtual void Print();
    virtual void EmitSpecific(Mips *mips) = 0;
    virtual void Emit(Mips *mips);

    virtual LiveVars_t *GetGenVars() { return new LiveVars_t; }
    virtual LiveVars_t* GetKillVars() { return new LiveVars_t; }
    LiveVars_t* FilterGlobalVars(LiveVars_t*);

    List<Instruction*> prev; 
    List<Instruction*> next;
    LiveVars_t* live_vars_in; 
    LiveVars_t* live_vars_out;
};

// for convenience, the instruction classes are listed here.
// the interfaces for the classes follows below

class LoadConstant;
class LoadStringConstant;
class LoadLabel;
class Assign;
class Load;
class Store;
class BinaryOp;
class Label;
class Goto;
class IfZ;
class BeginFunc;
class EndFunc;
class Return;
class PushParam;
class RemoveParams;
class LCall;
class ACall;
class VTable;

class LoadConstant : public Instruction
{
    Location *dst;
    int val;

  public:
    LoadConstant(Location *dst, int val);
    void EmitSpecific(Mips *mips);
};

class LoadStringConstant : public Instruction
{
    Location *dst;
    char *str;

  public:
    LoadStringConstant(Location *dst, const char *s);
    void EmitSpecific(Mips *mips);
};

class LoadLabel : public Instruction
{
    Location *dst;
    const char *label;

  public:
    LoadLabel(Location *dst, const char *label);
    void EmitSpecific(Mips *mips);
};

class Assign : public Instruction
{
    Location *dst, *src;

  public:
    Assign(Location *dst, Location *src);
    void EmitSpecific(Mips *mips);
};

class Load : public Instruction
{
    Location *dst, *src;
    int offset;

  public:
    Load(Location *dst, Location *src, int offset = 0);
    void EmitSpecific(Mips *mips);
};

class Store : public Instruction
{
    Location *dst, *src;
    int offset;

  public:
    Store(Location *d, Location *s, int offset = 0);
    void EmitSpecific(Mips *mips);
};

class BinaryOp : public Instruction
{

  public:
    static const char *const opName[Mips::NumOps];
    static Mips::OpCode OpCodeForName(const char *name);

  protected:
    Mips::OpCode code;
    Location *dst, *op1, *op2;

  public:
    BinaryOp(Mips::OpCode c, Location *dst, Location *op1, Location *op2);
    void EmitSpecific(Mips *mips);
};

class Label : public Instruction
{
    const char *label;

  public:
    Label(const char *label);
    void Print();
    void EmitSpecific(Mips *mips);
    const char *GetLabel() { return label; }
};

class Goto : public Instruction
{
    const char *label;

  public:
    Goto(const char *label);
    void EmitSpecific(Mips *mips);
    const char *GetLabel() { return label; }
};

class IfZ : public Instruction
{
    Location *test;
    const char *label;

  public:
    IfZ(Location *test, const char *label);
    void EmitSpecific(Mips *mips);
    const char *GetLabel() { return label; }
};

class BeginFunc : public Instruction
{
    int frameSize;

  public:
    BeginFunc();
    // used to backpatch the instruction with frame size once known
    void SetFrameSize(int numBytesForAllLocalsAndTemps);
    void EmitSpecific(Mips *mips);

    InterferenceGraph_t interference_graph;
};

class EndFunc : public Instruction
{
  public:
    EndFunc();
    void EmitSpecific(Mips *mips);
};

class Return : public Instruction
{
    Location *val;

  public:
    Return(Location *val);
    void EmitSpecific(Mips *mips);
};

class PushParam : public Instruction
{
    Location *param;

  public:
    PushParam(Location *param);
    void EmitSpecific(Mips *mips);
};

class PopParams : public Instruction
{
    int numBytes;

  public:
    PopParams(int numBytesOfParamsToRemove);
    void EmitSpecific(Mips *mips);
};

class LCall : public Instruction
{
    const char *label;
    Location *dst;

  public:
    LCall(const char *labe, Location *result);
    void EmitSpecific(Mips *mips);
};

class ACall : public Instruction
{
    Location *dst, *methodAddr;

  public:
    ACall(Location *meth, Location *result);
    void EmitSpecific(Mips *mips);
};

class VTable : public Instruction
{
    List<const char *> *methodLabels;
    const char *label;

  public:
    VTable(const char *labelForTable, List<const char *> *methodLabels);
    void Print();
    void EmitSpecific(Mips *mips);
};

#endif
