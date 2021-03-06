#include "functioncompilationctx.h"
#include "bytecodecompilationctx.h"
#include "class.h"

void FunctionCompilationContext::init() {
	Class *FunctionCompilationContextClass =
	    GcObject::FunctionCompilationContextClass;

	FunctionCompilationContextClass->init("function_compilation_context",
	                                      Class::ClassType::BUILTIN);
}

FunctionCompilationContext *FunctionCompilationContext::create(String2 name,
                                                               int     arity,
                                                               bool    isStatic,
                                                               bool    isva) {
	FunctionCompilationContext2 fcc =
	    GcObject::allocFunctionCompilationContext();
	fcc->slotCount = 0;
	fcc->bcc       = NULL;
	fcc->f         = NULL;
	fcc->slotmap   = NULL;
	// initialize the members
	fcc->slotmap = (SlotMap *)GcObject::malloc(sizeof(SlotMap));
	::new(fcc->slotmap) SlotMap();
	fcc->f       = Function::create(name, arity, isva, isStatic);
	fcc->bcc     = BytecodeCompilationContext::create();
	fcc->f->code = fcc->bcc->code;
	return fcc;
}

BytecodeCompilationContext *FunctionCompilationContext::get_codectx() {
	return bcc;
}

Function *FunctionCompilationContext::get_fn() {
	return f;
}

int FunctionCompilationContext::create_slot(String *s, int scopeID) {
	if(slotmap->contains(s)) {
		// reassign the variable to the new scope
		slotmap[0][s].scopeID = scopeID;
		return (*slotmap)[s].slot;
	}
	slotmap[0][s] = (VarState){slotCount++, scopeID};
	bcc->code->insertSlot();
	return slotCount - 1;
}

bool FunctionCompilationContext::has_slot(String *s, int scopeID) {
	return slotmap->contains(s) && slotmap[0][s].scopeID <= scopeID;
}

int FunctionCompilationContext::get_slot(String *s) {
	return slotmap[0][s].slot;
}

#ifdef DEBUG
void FunctionCompilationContext::disassemble(std::ostream &o) {
	o << "Slots: ";
	for(auto &a : slotmap[0]) {
		o << a.first->str() << "(" << a.second.slot << "), ";
	}
	o << "\n";
	f->disassemble(o);
}
#endif

#ifdef DEBUG_GC
const char *FunctionCompilationContext::gc_repr() {
	return f->name->str();
}
#endif
