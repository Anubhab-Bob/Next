#include "engine.h"
#include "display.h"
#include "primitive.h"

using namespace std;

ExecutionEngine::ExecutionEngine() {}

FrameInstance *ExecutionEngine::newinstance(Frame *f) {
	return new FrameInstance(f);
}

std::unordered_map<NextString, Module *> ExecutionEngine::loadedModules = {};

void ExecutionEngine::registerModule(Module *m) {
	if(loadedModules.find(m->name) != loadedModules.end()) {
		warn("Module already loaded : '%s'!", StringLibrary::get_raw(m->name));
	}
	loadedModules[m->name] = m;
}

// using BytecodeHolder::Opcodes;
void ExecutionEngine::printStackTrace(FrameInstance *root) {
	FrameInstance *f = root;
	while(f != NULL) {
		Token t;
		if((t = f->frame->findLineInfo(f->code - (f == root ? 0 : 1))).type !=
		   TOKEN_ERROR)
			t.highlight(true, "At ");
		else
			err("<source not found>");
		f = f->enclosingFrame;
	}
}

void ExecutionEngine::execute(Module *m, Frame *f) {
	FrameInstance *presentFrame;
	// Check if it is the root frame of the module,
	// and if so, restore the instance from the
	// module if there is one
	if(m->frame.get() == f) {
		if(m->frameInstance != NULL) {
			m->reAdjust(f);
			presentFrame = m->frameInstance;
		} else {
			presentFrame = m->topLevelInstance();
		}
	} else
		presentFrame = newinstance(f);
#ifdef NEXT_USE_COMPUTED_GOTO
#define DEFAULT() EXEC_CODE_unknown
	static const void *dispatchTable[] = {
#define OPCODE0(x, y) &&EXEC_CODE_##x,
#define OPCODE1(x, y, z) OPCODE0(x, y)
#define OPCODE2(w, x, y, z) OPCODE0(w, x)
#include "opcodes.h"
	    &&DEFAULT()};

#define LOOP() while(1)
#define SWITCH() \
	{ goto *dispatchTable[*presentFrame->code]; }
#define CASE(x) EXEC_CODE_##x
#define DISPATCH() \
	{ goto *dispatchTable[*(++presentFrame->code)]; }
#define DISPATCH_WINC() \
	{ goto *dispatchTable[*presentFrame->code]; }
#else
#define LOOP() while(1)
#define SWITCH() switch(*presentFrame->code)
#define CASE(x) case BytecodeHolder::CODE_##x
#define DISPATCH()            \
	{                         \
		presentFrame->code++; \
		continue;             \
	}
#define DISPATCH_WINC() \
	{ continue; }
#define DEFAULT() default
#endif

#define TOP presentFrame->stack_[presentFrame->stackPointer - 1]
#define PUSH(x) presentFrame->stack_[presentFrame->stackPointer++] = (x);
#define POP() presentFrame->stack_[--presentFrame->stackPointer]
#define RERR(x, ...)                                                    \
	{                                                                   \
		cout << "\n";                                                   \
		err("Runtime error!");                                          \
		lnerr(x, presentFrame->frame->findLineInfo(presentFrame->code), \
		      ##__VA_ARGS__);                                           \
		printStackTrace(presentFrame);                                  \
		exit(1);                                                        \
	}

#define JUMPTO(x)                \
	{                            \
		presentFrame->code += x; \
		continue;                \
	}

#define next_int()                      \
	(presentFrame->code += sizeof(int), \
	 *(int *)((presentFrame->code - sizeof(int) + 1)))
#define next_double()                      \
	(presentFrame->code += sizeof(double), \
	 *(double *)((presentFrame->code - sizeof(double) + 1)))
#define next_string()                          \
	(presentFrame->code += sizeof(NextString), \
	 *(NextString *)(presentFrame->code - sizeof(NextString) + 1))
#define next_ptr()                            \
	(presentFrame->code += sizeof(uintptr_t), \
	 *(uintptr_t *)(presentFrame->code - sizeof(uintptr_t) + 1))
#define next_value()                      \
	(presentFrame->code += sizeof(Value), \
	 *(Value *)(presentFrame->code - sizeof(Value) + 1))
	// std::cout << "x : " << TOP << " y : " << v << " op : " << #op <<
	// std::endl;

#define binary(op, name, argtype, restype)                              \
	{                                                                   \
		Value v = POP();                                                \
		if(v.is##argtype() && TOP.is##argtype()) {                      \
			TOP.set##restype(TOP.to##argtype() op v.to##argtype());     \
			DISPATCH();                                                 \
		}                                                               \
		RERR("Both of the operands of " #name " are not " #argtype "!") \
	}

#define ref_incr(x)                    \
	{                                  \
		if(x.isObject()) {             \
			x.toObject()->incrCount(); \
		}                              \
	}

#define ref_decr(x)                    \
	{                                  \
		if(x.isObject()) {             \
			x.toObject()->decrCount(); \
		}                              \
	}

	LOOP() {
		SWITCH() {

			CASE(add) : binary(+, addition, Number, Number);
			CASE(sub) : binary(-, subtraction, Number, Number);
			CASE(mul) : binary(*, multiplication, Number, Number);
			CASE(div) : binary(/, division, Number, Number);
			CASE(lor) : binary(||, or, Boolean, Boolean);
			CASE(land) : binary(&&, and, Boolean, Boolean);
			CASE(eq) : binary(==, equals to, Number, Boolean);
			CASE(neq) : binary(!=, not equals to, Number, Boolean);
			CASE(greater) : binary(>, greater than, Number, Boolean);
			CASE(greatereq)
			    : binary(>=, greater than or equals to, Number, Boolean);
			CASE(less) : binary(<, lesser than, Number, Boolean);
			CASE(lesseq)
			    : binary(<=, lesser than or equals to, Number, Boolean);
			CASE(power) : RERR("Yet not implemented!");

			CASE(lnot) : {
				if(TOP.isBoolean()) {
					TOP.setBoolean(!TOP.toBoolean());
					DISPATCH();
				}
				RERR("'!' can only be applied over boolean value!");
			}

			CASE(neg) : {
				if(TOP.isNumber()) {
					TOP = -TOP.toNumber();
					DISPATCH();
				}
				RERR("'-' must only be applied over a number!");
			}

			CASE(push) : {
				PUSH(next_value());
				DISPATCH();
			}

			CASE(pushd) : {
				PUSH(Value(next_double()));
				DISPATCH();
			}

			CASE(pushs) : {
				PUSH(Value(next_string()));
				DISPATCH();
			}

			CASE(pushn) : {
				PUSH(Value::nil);
				DISPATCH();
			}

			CASE(pop) : {
				Value v = POP();
				// If the refcount of the
				// object is already zero,
				// it isn't stored in any
				// slot and probably generated
				// by an unused constructor
				// call
				if(v.isObject()) {
					v.toObject()->freeIfZero();
				}
				DISPATCH();
			}

			CASE(jump) : {
				JUMPTO(next_int() -
				       sizeof(int)); // offset the relative jump address
			}

			CASE(jumpiftrue) : {
				Value v   = POP();
				int   dis = next_int();
				bool  tr  = (v.isBoolean() && v.toBoolean()) ||
				          (v.isNumber() && v.toNumber());
				if(tr) {
					JUMPTO(dis -
					       sizeof(int)); // offset the relative jump address
				}
				DISPATCH();
			}

			CASE(jumpiffalse) : {
				Value v   = POP();
				int   dis = next_int();
				bool  tr  = v.isNil() || (v.isBoolean() && !v.toBoolean()) ||
				          (v.isNumber() && !v.toNumber());
				if(tr) {
					JUMPTO(dis -
					       sizeof(int)); // offset the relative jump address
				}
				DISPATCH();
			}

			CASE(print) : {
				Value v = POP();
				std::cout << v;
				DISPATCH();
			}

			CASE(call) : {
				// NextString       fn = next_string();
				FrameInstance *f = newinstance(m->frames[next_int()]);
				// Copy the arguments
				int numArg = next_int() - 1;
				while(numArg >= 0) {
					f->stack_[numArg] = POP();
					ref_incr(f->stack_[numArg]);
					numArg--;
				}
				f->enclosingFrame = presentFrame;
				presentFrame      = f;
				DISPATCH_WINC();
			}

			CASE(ret) : {
				// Pop the return value
				Value          v   = POP();
				FrameInstance *bak = presentFrame->enclosingFrame;
				delete presentFrame;
				presentFrame = bak;
				PUSH(v);
				DISPATCH();
			}

			CASE(load_slot) : {
				PUSH(presentFrame->stack_[next_int()]);
				DISPATCH();
			}

			CASE(store_slot) : {
				int slot = next_int();
				ref_decr(presentFrame->stack_[slot]);
				ref_incr(TOP);
				// Do not pop the value off the stack yet
				presentFrame->stack_[slot] = TOP;
				DISPATCH();
			}

			CASE(incr_slot) : {
				Value &v = presentFrame->stack_[next_int()];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() + 1);
					DISPATCH();
				}
				RERR("'++' can only be applied on a number!");
			}

			CASE(incr_module_slot) : {
				Value &v = presentFrame->moduleStack[next_int()];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() + 1);
					DISPATCH();
				}
				RERR("'++' can only be applied on a number!");
			}

			CASE(decr_slot) : {
				Value &v = presentFrame->stack_[next_int()];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() - 1);
					DISPATCH();
				}
				RERR("'--' can only be applied on a number!");
			}

			CASE(decr_module_slot) : {
				Value &v = presentFrame->moduleStack[next_int()];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() - 1);
					DISPATCH();
				}
				RERR("'--' can only be applied on a number!");
			}

			CASE(load_module_slot) : {
				int        slot = next_int();
				PUSH(presentFrame->moduleStack[slot]);
				DISPATCH();
			}

			CASE(store_module_slot) : {
				int        slot = next_int();
				ref_decr(presentFrame->moduleStack[slot]);
				ref_incr(TOP);
				presentFrame->moduleStack[slot] = TOP;
				DISPATCH();
			}

			CASE(load_object_slot) : {
				int slot = next_int();
				PUSH(presentFrame->stack_[0].toObject()->slots[slot]);
				DISPATCH();
			}

			CASE(store_object_slot) : {
				int slot = next_int();
				ref_decr(presentFrame->stack_[0].toObject()->slots[slot]);
				ref_incr(TOP);
				presentFrame->stack_[0].toObject()->slots[slot] = TOP;
				DISPATCH();
			}

			CASE(incr_object_slot) : {
				int    slot = next_int();
				Value &v = presentFrame->stack_[0].toObject()->slots[slot];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() + 1);
					DISPATCH();
				}
				RERR("'++' can only be applied on a number!");
			}

			CASE(decr_object_slot) : {
				int    slot = next_int();
				Value &v    = presentFrame->stack_[0].toObject()->slots[slot];
				if(v.isNumber()) {
					v.setNumber(v.toNumber() - 1);
					DISPATCH();
				}
				RERR("'--' can only be applied on a number!");
			}

			CASE(load_field) : {
				NextString field = next_string();
				if(TOP.isObject()) {
					NextObject *obj = TOP.toObject();
					if(obj->Class->members.find(field) !=
					   obj->Class->members.end()) {
						TOP = obj->slots[obj->Class->members[field].slot];
						DISPATCH();
					} else {
						RERR("Member '%s' not found in class '%s'!",
						     StringLibrary::get_raw(field),
						     StringLibrary::get_raw(obj->Class->name));
					}
				}
				RERR("'.' can only be applied over an object!");
			}

			CASE(load_field_pushback) : {
				NextString field = next_string();
				if(TOP.isObject()) {
					NextObject *obj = TOP.toObject();
					if(obj->Class->members.find(field) !=
					   obj->Class->members.end()) {
						Value v = TOP;
						TOP = obj->slots[obj->Class->members[field].slot];
						PUSH(v);
						DISPATCH();
					} else {
						RERR("Member '%s' not found in class '%s'!",
						     StringLibrary::get_raw(field),
						     StringLibrary::get_raw(obj->Class->name));
					}
				}
				RERR("'.' can only be applied over an object!");
			}

			CASE(store_field) : {
				NextString field = next_string();
				if(TOP.isObject()) {
					NextObject *obj = POP().toObject();
					if(obj->Class->members.find(field) !=
					   obj->Class->members.end()) {
						Value &v = obj->slots[obj->Class->members[field].slot];
						ref_decr(v);
						ref_incr(TOP);
						v = TOP;
						DISPATCH();
					} else {
						RERR("Member '%s' not found in class '%s'!",
						     StringLibrary::get_raw(field),
						     StringLibrary::get_raw(obj->Class->name));
					}
				}
				RERR("'.' can only be applied over an object!");
			}

			CASE(incr_field) : {
				NextString field = next_string();
				if(TOP.isObject()) {
					NextObject *obj = TOP.toObject();
					if(obj->Class->members.find(field) !=
					   obj->Class->members.end()) {
						Value &v = obj->slots[obj->Class->members[field].slot];
						if(v.isNumber()) {
							v.setNumber(v.toNumber() + 1);
							DISPATCH();
						}
						RERR("Member '%s' is not a number!",
						     StringLibrary::get_raw(field));
					} else {
						RERR("Member '%s' not found in class '%s'!",
						     StringLibrary::get_raw(field),
						     StringLibrary::get_raw(obj->Class->name));
					}
				}
				RERR("'.' can only be applied over an object!");
			}

			CASE(decr_field) : {
				NextString field = next_string();
				if(TOP.isObject()) {
					NextObject *obj = TOP.toObject();
					if(obj->Class->members.find(field) !=
					   obj->Class->members.end()) {
						Value &v = obj->slots[obj->Class->members[field].slot];
						if(v.isNumber()) {
							v.setNumber(v.toNumber() - 1);
							DISPATCH();
						}
						RERR("Member '%s' is not a number!",
						     StringLibrary::get_raw(field));
					} else {
						RERR("Member '%s' not found in class '%s'!",
						     StringLibrary::get_raw(field),
						     StringLibrary::get_raw(obj->Class->name));
					}
				}
				RERR("'.' can only be applied over an object!");
			}

			CASE(call_method) : {
				NextString method = next_string();
				int        numArg = next_int(); // copy the object also
				Value      v      = presentFrame
				              ->stack_[presentFrame->stackPointer - numArg - 1];
				if(v.isObject()) {
					NextObject *obj = v.toObject();
					if(obj->Class->functions.find(method) !=
					   obj->Class->functions.end()) {
						FrameInstance *f = newinstance(
						    obj->Class->functions[method]->frame.get());
						// Copy the arguments
						while(numArg >= 0) {
							f->stack_[numArg] = POP();
							ref_incr(f->stack_[numArg]);
							numArg--;
						}
						f->enclosingFrame = presentFrame;
						presentFrame      = f;
						DISPATCH_WINC();
					}
					RERR("Method '%s' not found in class '%s'!",
					     StringLibrary::get_raw(method),
					     StringLibrary::get_raw(obj->Class->name));
				} else {
					if(Primitives::hasPrimitive(v.getType(), method)) {
						Value ret = Primitives::invokePrimitive(
						    v.getType(), method,
						    &presentFrame->stack_[presentFrame->stackPointer -
						                          numArg - 1]);

						// Pop the arguments
						while(numArg >= 0) {
							POP(); // TODO: Potential memory leak?
							numArg--;
						}

						PUSH(ret);
						DISPATCH();
					}
				}
				RERR("Primitive method '%s' not found in type '%s'!",
				     StringLibrary::get_raw(method),
				     StringLibrary::get_raw(v.getTypeString()));
			}

			CASE(call_intraclass) : {
				int frame  = next_int();
				int            numArg = next_int() - 1;
				NextObject *   obj    = presentFrame->stack_[0].toObject();
				FrameInstance *f      = newinstance(obj->Class->frames[frame]);
				// Copy the arguments
				while(numArg >= 0) {
					f->stack_[numArg + 1] = POP();
					ref_incr(f->stack_[numArg]);
					numArg--;
				}
				// copy the object manually
				f->stack_[0] = presentFrame->stack_[0];
				f->stack_[0].toObject()->incrCount();
				f->enclosingFrame = presentFrame;
				presentFrame      = f;
				DISPATCH_WINC();
			}

			CASE(construct_ret) : {
				// Pop the return object
				Value v = presentFrame->stack_[0];
				// Increment the refCount so that it doesn't get
				// garbage collected on delete
				v.toObject()->incrCount();
				FrameInstance *bak = presentFrame->enclosingFrame;
				delete presentFrame;
				presentFrame = bak;
				// Reset the refCount, so that if
				// it isn't stored in a slot in the
				// callee function, it gets garbage
				// collected at POP
				v.toObject()->refCount = 0;
				PUSH(v);
				DISPATCH();
			}

			CASE(construct) : {
				NextString mod = next_string();
				NextString cls = next_string();
				NextObject *no =
				    new NextObject(loadedModules[mod]->classes[cls].get());
				no->incrCount();
				presentFrame->stack_[0] = no;
				DISPATCH();
			}

			CASE(halt) : {
				presentFrame->instructionPointer =
				    presentFrame->code -
				    presentFrame->frame->code.bytecodes.data();
				return;
			}

			DEFAULT() : {
				uint8_t code = *presentFrame->code;
				if(code > BytecodeHolder::CODE_halt) {
					panic("Invalid bytecode %d!", code);
				} else {
					panic("Bytecode not implemented : '%s'!",
					      BytecodeHolder::OpcodeNames[code])
				}
			}
		}
	}
}