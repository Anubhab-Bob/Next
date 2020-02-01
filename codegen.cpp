#include "codegen.h"
#include "builtins.h"
#include "bytecode.h"
#include "core.h"
#include "display.h"
#include "engine.h"
#include "import.h"
#include "loader.h"
#include "stringconstants.h"
#include "symboltable.h"

using namespace std;

#define lnerr_(str, t, ...)                   \
	{                                         \
		lnerr(str, t, ##__VA_ARGS__);         \
		t.highlight(false, "", Token::ERROR); \
		errorsOccurred++;                     \
	}

CodeGenerator::CodeGenerator() {
	frame                = nullptr;
	state                = COMPILE_DECLARATION;
	onLHS                = false;
	scopeID              = 0;
	inClass              = false;
	currentClass         = NULL;
	currentVisibility    = AccessModifiableEntity::PUB;
	onRefer              = false;
	tryBlockStart        = 0;
	tryBlockEnd          = 0;
	lastMemberReferenced = 0;
	errorsOccurred       = 0;
}

void CodeGenerator::compile(Module *compileIn, const vector<StmtPtr> &stmts) {
	frame  = nullptr;
	module = compileIn;
	ExecutionEngine::registerModule(compileIn);
	initFrame(module->frame.get(),
	          stmts.size() > 0 ? stmts[0]->token : Token::PlaceholderToken);
	NextString lastName = StringConstants::core;
	if(compileIn->name != lastName) {
		module->importedModules[lastName] = CoreModule::core;
		VarInfo v                         = lookForVariable(lastName, true);
		bytecode->push(Value(module->importedModules[lastName]));
		bytecode->store_slot(v.slot);
		bytecode->pop2();
	}
	compileAll(stmts);
	bytecode->halt();
	popFrame();
#ifdef DEBUG
	cout << "Code generated for module " << StringLibrary::get(module->name)
	     << endl;
	cout << *module;
#endif
	if(errorsOccurred)
		throw CodeGeneratorException(errorsOccurred);
}

int CodeGenerator::pushScope() {
	return ++scopeID;
}

void CodeGenerator::popScope() {
	if(frame == nullptr)
		return;
	for(auto &i : frame->slots) {
		if(i.second.scopeID >= scopeID) {
			i.second.isValid = false;
		}
	}
	--scopeID;
}

void CodeGenerator::compileAll(const vector<StmtPtr> &stmts) {
	// Backup current state
	CompilationState bak = state;
	// First compile all declarations
	state = COMPILE_DECLARATION;
	for(auto i = stmts.begin(), j = stmts.end(); i != j; i++) {
		// Pass only declaration statements
		if((*i)->isDeclaration())
			(*i)->accept(this);
	}
	// Mark this module as already compiled, so that even if a
	// cyclic import occures, this module is not recompiled
	module->isCompiled = true;
	// Then compile all imports
	state = COMPILE_IMPORTS;
	for(auto i = stmts.begin(), j = stmts.end(); i != j; i++) {
		if((*i)->isImport())
			(*i)->accept(this);
	}
	// Then compile all bodies
	state = COMPILE_BODY;
	for(auto i = stmts.begin(), j = stmts.end(); i != j; i++) {
		(*i)->accept(this);
	}
	state = bak;
}

void CodeGenerator::initFrame(Frame *f, Token t) {
	f->parent = frame;
	f->insertdebug(t);
	frame    = f;
	bytecode = &f->code;
}

CodeGenerator::CompilationState CodeGenerator::getState() {
	return state;
}

void CodeGenerator::popFrame() {
	if(!frame)
		return;
	frame->finalizeDebug();
	frame = frame->parent;
	if(frame)
		bytecode = &frame->code;
}

Module *CodeGenerator::compile(NextString name, const vector<StmtPtr> &stmts) {
	module = new Module(name);
	frame  = module->frame.get();
	compileAll(stmts);
	return module;
}

int CodeGenerator::createTempSlot() {
	char tempname[20] = {0};
	int  len          = snprintf(&tempname[0], 20, "temp %d", frame->slotSize);
	int  slot         = frame->declareVariable(tempname, len, scopeID);
	return slot;
}

void CodeGenerator::visit(BinaryExpression *bin) {
#ifdef DEBUG
	dinfo("");
	bin->token.highlight();
#endif
	bin->left->accept(this);
	int jumpto = -1;
	switch(bin->token.type) {
		case TOKEN_and: jumpto = bytecode->land(0); break;
		case TOKEN_or: jumpto = bytecode->lor(0); break;
		default: break;
	}
	bin->right->accept(this);
	frame->insertdebug(bin->token);
	switch(bin->token.type) {
		case TOKEN_PLUS: bytecode->add(); break;
		case TOKEN_MINUS: bytecode->sub(); break;
		case TOKEN_STAR: bytecode->mul(); break;
		case TOKEN_SLASH: bytecode->div(); break;
		case TOKEN_CARET: bytecode->power(); break;
		case TOKEN_BANG: bytecode->lnot(); break;
		case TOKEN_EQUAL_EQUAL: bytecode->eq(); break;
		case TOKEN_BANG_EQUAL: bytecode->neq(); break;
		case TOKEN_LESS: bytecode->less(); break;
		case TOKEN_LESS_EQUAL: bytecode->lesseq(); break;
		case TOKEN_GREATER: bytecode->greater(); break;
		case TOKEN_GREATER_EQUAL: bytecode->greatereq(); break;
		case TOKEN_and:
			bytecode->land(jumpto, bytecode->getip() - jumpto);
			break;
		case TOKEN_or: bytecode->lor(jumpto, bytecode->getip() - jumpto); break;
		case TOKEN_in: bytecode->in_(); break;

		default:
			panic("Invalid binary operator '%s'!",
			      Token::FormalNames[bin->token.type]);
	}
}

void CodeGenerator::visit(GroupingExpression *g) {
#ifdef DEBUG
	dinfo("");
	g->token.highlight();
#endif
	g->exp->accept(this);
}

int CodeGenerator::getFrameIndex(vector<Frame *> &frames, Frame *searching) {
	int k = 0;
	for(auto i = frames.begin(), j = frames.end(); i != j; i++, k++) {
		if(*i == searching) {
			return k;
		}
	}
	return -1;
}

CodeGenerator::CallInfo
CodeGenerator::resolveCall(NextString signature, bool isImported, Module *mod) {
	CallInfo info = {CallInfo::UNDEFINED, NULL, 0};

	// If its marked as imported, then we know there's already
	// a valid module
	if(isImported) {
		info.type     = CallInfo::IMPORTED;
		info.fn       = mod->functions[signature].get();
		info.frameIdx = module->getIndexOfImportedFrame(
		    mod->functions[signature]->frame.get());
		return info;
	}
	// If we are inside a class, first search for methods inside
	// of it
	uint64_t sym;
	if(inClass)
		sym = SymbolTable::insertSymbol(signature);
	if(inClass && currentClass->hasMethod(sym)) {
		info.type = CallInfo::INTRA_CLASS;
		// Search for the frame in the class
		Frame *searching = currentClass->functions[sym]->frame.get();
		info.frameIdx    = frame->getCallFrameIndex(searching);
		info.fn          = currentClass->functions[sym].get();
		return info;

	}
	// Search for methods with generated signature in the present
	// module
	else if(module->functions.find(signature) != module->functions.end()) {
		info.type = CallInfo::INTRA_MODULE;
		// Search for the frame in the module
		Frame *searching = module->functions[signature]->frame.get();
		info.frameIdx    = frame->getCallFrameIndex(searching);
		info.fn          = module->functions[signature].get();
		return info;
	} else { // try searching for builtin functions
		if(Builtin::has_builtin(signature)) {
			info.type = CallInfo::BUILTIN;
			return info;
		}
	}
	// Do not automatically link indirect function imports
	/*else {
	    // Search in the imported modules
	    Frame *searching     = NULL;
	    for(auto i = module->importedModules.begin(),
	             j = module->importedModules.end();
	        i != j; i++) {
	        if((*i).second->functions.find(signature) !=
	           (*i).second->functions.end()) {
	            if((*i).second->symbolTable[signature]->vis ==
	               AccessModifiableEntity::PUB) {
	                searching = (*i).second->functions[signature]->frame.get();
	                info.type = CallInfo::IMPORTED;
	                info.fn   = (*i).second->functions[signature].get();
	                info.frameIdx = module->getIndexOfImportedFrame(searching);
	                return info;
	            }
	        }
	    }
	}*/

	return info;
}

void CodeGenerator::emitCall(CallExpression *call, bool isImported,
                             Module *mod) {
#ifdef DEBUG
	dinfo("Generating call for");
	call->callee->token.highlight();
#endif
	int argSize = call->arguments.size();
	// bytecode->stackEffect(-argSize + 1);
	// Since CALL has higher precedence than REFERENCE,
	// a CallExpression will necessarily contain an identifier
	// as its callee.
	// Hence 'call->callee->accept(this)' is not required. We
	// can directly use the token to generate the signature.
	NextString signature = generateSignature(call->callee->token, argSize);

	CallInfo info = {CallInfo::UNDEFINED, NULL, 0};

	if(!onRefer) {
		info = resolveCall(signature, isImported, mod);
	}

	// If this is a constructor call, we pass in
	// a dummy value to slot 0, which will later
	// be replaced by the instance itself
	if(!onRefer && info.type != CallInfo::UNDEFINED &&
	   info.type != CallInfo::BUILTIN && info.fn->isConstructor) {
		// bytecode->stackEffect(-1);
		bytecode->pushd(0);
	}
	if(!onRefer && info.type == CallInfo::INTRA_CLASS) {
		// load the object first, so no manual stack manipulation
		// is needed
		bytecode->load_slot_0();
	}
	// Reset the referral status for arguments
	bool bak = onRefer;
	onRefer  = false;
	for(auto &i : call->arguments) {
		i->accept(this);
	}
	onRefer = bak;
	frame->insertdebug(call->callee->token);
	// If this a reference expression, dynamic dispatch will be used
	if(onRefer) {
		bytecode->call_method(SymbolTable::insertSymbol(signature), argSize);
		bytecode->stackEffect(-argSize);
		return;
	} else {
		switch(info.type) {
			case CallInfo::INTRA_CLASS:
				if(frame->isStatic && !info.fn->frame->isStatic) {
					lnerr_("Non-static method '%.*s' cannot be called inside a "
					       "static method!",
					       call->callee->token, call->callee->token.length,
					       call->callee->token.start);
				}
				// +1 for the object
				bytecode->call(info.frameIdx, argSize + 1);
				// argSize + 1 values sent, 1 value returned
				bytecode->stackEffect(-argSize + 2);
				break;
			case CallInfo::INTRA_MODULE:
				bytecode->call(info.frameIdx,
				               argSize
				                   // if this is a constructor call, reserve
				                   // slot 0 for the dummy value which will be
				                   // replaced by the instance itself
				                   + info.fn->isConstructor);
				bytecode->stackEffect(-argSize + 1);
				break;
			/*case CallInfo::IMPORTED:
				bytecode->call_imported(info.frameIdx,
				                        call->arguments.size() +
				                            info.fn->isConstructor);
				break;*/
			case CallInfo::BUILTIN:
				bytecode->call_builtin(signature, argSize);
				bytecode->stackEffect(-argSize + 1);
				break;
			default: {

				// Function is not found
				lnerr_("No function with the specified signature found in "
				       "module '%s'!",
				       call->callee->token,
				       StringLibrary::get_raw(module->name));
				NextString s = StringLibrary::insert(
				    call->callee->token.start, call->callee->token.length);
				for(auto const &i : module->functions) {
					if(s == i.second->name) {
						lninfo("Found similar function (takes %zu arguments, "
						       "provided "
						       "%zu)",
						       i.second->token, i.second->arity, argSize);
						i.second->token.highlight();
					}
				}
				break;
			}
		}
	}
}

void CodeGenerator::visit(CallExpression *call) {
	emitCall(call);
}

CodeGenerator::VarInfo CodeGenerator::lookForVariable(NextString name,
                                                      bool       declare) {
	int slot = 0, isLocal = 1;
	// first check the present frame
	if(frame->hasVariable(name)) {
		slot = frame->slots[name].slot;
		return (VarInfo){slot, LOCAL};
	} else { // It's in an enclosing class, or parent frame or another module
		// Check if we are inside a class
		if(inClass) {
			// Check if it is in present class
			if(currentClass->hasVariable(name)) {
				return (VarInfo){currentClass->members[name].slot, CLASS};
			}
		}
		// Check if it is in a parent frame
		Frame *f = frame->parent;
		if(f != nullptr) {
			while(f != nullptr && !f->hasVariable(name)) f = f->parent;
			if(f != nullptr) {
				isLocal = 0;
				return (VarInfo){f->slots.find(name)->second.slot, MODULE};
			}
		}

		// Check if it is a built-in constant
		// Since built-in constants cannot be overridden, it will only
		// be a built-in constant if it is on the right side of an
		// expression. If it is on the left side of an expression,
		// an error will be raised by the respective code generator module.
		if(Builtin::has_constant(name)) {
			return (VarInfo){0, BUILTIN};
		}

		/* Later

		// Check if it is in an imported module
		for(auto i = module->importedModules.begin(),
		         j = module->importedModules.end();
		    i != j; i++) {
		    Module *m = (*i).second;
		    if(m->variables.find(name) != m->variables.end() &&
		       get<1>(m->variables.find(name)->second) == PUBLIC) {
		        bytecode->load_module_slot((*i).first, name);
		        return;
		    }
		}

		*/
		if(declare) {
			// Finally, declare the variable in the present frame
			slot = frame->declareVariable(name, scopeID);
			return (VarInfo){slot, LOCAL};
		}
	}
	return (VarInfo){-1, UNDEFINED};
}

CodeGenerator::VarInfo CodeGenerator::lookForVariable(Token t, bool declare,
                                                      bool       showError,
                                                      Visibility vis) {
	NextString name = StringLibrary::insert(t.start, t.length);
	VarInfo    var  = lookForVariable(name, declare);
	if(var.position == UNDEFINED && showError) {
		lnerr_("No such variable found : '%s'", t,
		       StringLibrary::get_raw(name));
	} else if(var.position == CLASS && !currentClass->members[name].isStatic &&
	          frame->isStatic) {
		lnerr_(
		    "Non-static member '%.*s' cannot be used inside a static method!",
		    t, t.length, t.start);
	} else if(declare && var.position == LOCAL && frame->parent == NULL &&
	          module->variables.find(name) == module->variables.end()) {
		module->variables[name] =
		    Variable(vis == VIS_PUB ? AccessModifiableEntity::PUB
		                            : AccessModifiableEntity::PRIV,
		             t);
		module->variables[name].isStatic = false;
		module->variables[name].slot     = var.slot;
	}
	return var;
}

void CodeGenerator::visit(AssignExpression *as) {
#ifdef DEBUG
	dinfo("");
	as->token.highlight();
#endif
	if(as->target->isMemberAccess())
		panic("AssignExpression should not contain member access!");

	if(as->target->type == Expr::SUBSCRIPT) {
		// it is a subscript setter
		// subscript setters are compiled as the
		// target first, then the value to avoid
		// stack manipulation in case we need to
		// call op method [](_,_)
		if(state == COMPILE_BODY) {
			bool b = onLHS;
			onLHS  = true;
			as->target->accept(this);
			onLHS = b;
			as->val->accept(this);
			bytecode->subscript_set();
		}
	} else {

		//
		if(state == COMPILE_BODY) {
			// Resolve the expression
			as->val->accept(this);
		}

		VarInfo var = lookForVariable(as->target->token, true);

		if(state == COMPILE_BODY) {
			frame->insertdebug(as->token);
			// target of an assignment expression cannot be a
			// builtin constant
			switch(var.position) {
				case LOCAL: bytecode->store_slot(var.slot); break;
				case CLASS: bytecode->store_object_slot(var.slot); break;
				case MODULE: bytecode->store_module_slot(var.slot); break;
				case BUILTIN: {
					lnerr_("Built-in constant '%.*s' cannot be reassigned!",
					       as->target->token, as->target->token.length,
					       as->target->token.start);
					break;
				}
				default: break;
			}
		}
	}
}

void CodeGenerator::visit(ArrayLiteralExpression *al) {
#ifdef DEBUG
	dinfo("");
	al->token.highlight();
#endif
	// load the core module if we're not already in it
	if(module->name != StringConstants::core) {
		// core is declared as the 0th slot in the module
		bytecode->load_module_slot(0);
		bytecode->pushd((double)al->exprs.size());
		bytecode->call_method(
		    SymbolTable::insertSymbol(StringConstants::sig_array_1), 1);
		bytecode->stackEffect(0);
	} else {
		// we are in the core module
		CallInfo info = resolveCall(StringConstants::sig_array_1, false, NULL);
		bytecode->pushd(0);
		bytecode->pushd((double)al->exprs.size());
		bytecode->call(info.frameIdx, 2);
		bytecode->stackEffect(0);
	}
	if(al->exprs.size() > 0) {
		// now evalute all the expressions
		for(auto &i : al->exprs) {
			// bytecode->pushd((double)j);
			i->accept(this);
			// bytecode->subscript_set();
			// bytecode->pop2();
		}
		// finally emit opcode to assign those
		// expressions to the array, and leave
		// the array at the top of the stack
		bytecode->array_build(al->exprs.size());
		bytecode->stackEffect(-(int)al->exprs.size());
	}
}

void CodeGenerator::visit(HashmapLiteralExpression *al) {
#ifdef DEBUG
	dinfo("");
	al->token.highlight();
#endif
	// load the core module if we're not already in it
	if(module->name != StringConstants::core) {
		// core is declared as the 0th slot in the module
		bytecode->load_module_slot(0);
		bytecode->call_method(
		    SymbolTable::insertSymbol(StringConstants::sig_hashmap_0), 0);
		// bytecode->stackEffect(1);
	} else {
		// we are in the core module
		CallInfo info =
		    resolveCall(StringConstants::sig_hashmap_0, false, NULL);
		bytecode->pushd(0);
		bytecode->call(info.frameIdx, 1);
		// bytecode->stackEffect(1);
	}
	int slot = createTempSlot();
	bytecode->store_slot_pop(slot);
	if(al->keys.size() > 0) {
		// now evalute all the key:value pairs
		int p = 0;
		for(auto &i : al->keys) {
			bytecode->load_slot(slot);
			i->accept(this);
			al->values[p]->accept(this);
			bytecode->subscript_set();
			bytecode->pop();
			p++;
		}
	}
	bytecode->load_slot(slot);
}

void CodeGenerator::visit(LiteralExpression *lit) {
#ifdef DEBUG
	dinfo("");
	lit->token.highlight();
#endif
	frame->insertdebug(lit->token);
	bytecode->push(lit->value);
	/*
	switch(lit->value.t) {
	    case Value::VAL_Number: bytecode->pushd(lit->value.toNumber()); break;
	    case Value::VAL_String: bytecode->pushs(lit->value.toString()); break;
	    case Value::VAL_NIL: bytecode->pushn(); break;
	    default: panic("Handling other values not implemented!");
	}*/
}

void CodeGenerator::visit(SetExpression *sete) {
	if(state == COMPILE_BODY) {
#ifdef DEBUG
		dinfo("");
		sete->token.highlight();
#endif
		sete->value->accept(this);
		bool b = onLHS;
		onLHS  = true;
		sete->object->accept(this);
		bytecode->store_field(lastMemberReferenced);
		onLHS = b;
	}
}

void CodeGenerator::visit(GetExpression *get) {
#ifdef DEBUG
	dinfo("");
	get->token.highlight();
#endif
	// TODO: Here we have to check if both side of the
	// get expression is 'VariableExpression's (which
	// will only happen for the start of a reference
	// expression), or VariableExpression.CallExpression
	// the expression can actually be a
	// module reference.
	/*
	if(get->object->isVariable()) {
	    // Find out the name
	    NextString name = StringLibrary::insert(get->object->token.start,
	                                            get->object->token.length);
	    // If a variable shadows the module name, the variable name will be
	    // prioritized
	    VarInfo var = lookForVariable(name, false);
	    // Check if there is a module of the same name
	    if(var.position == UNDEFINED && module->importedModules.find(name) !=
	                                        module->importedModules.end()) {
	        // voila
	        // Now resolve the reference.
	        // Only valid function calls and variable
	        // accesses will be optimized. Everything else
	        // will be delegated into the runtime as a
	        // method call to the module primitive.
	        Module *m = module->importedModules[name];
	        if(get->refer->getType() == Expr::Type::CALL) {
	            CallExpression *ce = (CallExpression *)get->refer.get();
	            NextString      sig =
	                generateSignature(ce->callee->token, ce->arguments.size());
	            if(m->hasSignature(sig)) {
	                emitCall(ce, true, m);
	                return;
	            } else {
	                // Load the module* at runtime and delegate the call
	                lookForVariable(name);
	                bool b  = onRefer;
	                onRefer = true;
	                emitCall(ce);
	                onRefer = b;
	                return;
	            }
	        }
	    }
	}
	*/
	bool lb = onLHS;
	onLHS   = false;
	get->object->accept(this);
	bool b  = onRefer;
	onRefer = true;
	onLHS   = lb;
	get->refer->accept(this);
	onRefer = b;
}

void CodeGenerator::visit(SubscriptExpression *sube) {
#ifdef DEBUG
	dinfo("");
	sube->token.highlight();
#endif
	bool b = onLHS;
	onLHS  = false;
	sube->object->accept(this);
	sube->idx->accept(this);
	onLHS = b;
	if(!onLHS)
		bytecode->subscript_get();
}

void CodeGenerator::visit(PrefixExpression *pe) {
#ifdef DEBUG
	dinfo("");
	pe->token.highlight();
#endif
	switch(pe->token.type) {
		case TOKEN_PLUS: pe->right->accept(this); break;
		case TOKEN_BANG:
			pe->right->accept(this);
			bytecode->lnot();
			break;
		case TOKEN_MINUS:
			pe->right->accept(this);
			frame->insertdebug(pe->token);
			bytecode->neg();
			break;
		case TOKEN_PLUS_PLUS:
			if(!pe->right->isAssignable()) {
				lnerr_("Cannot apply '++' on a non-assignable expression!",
				       pe->token);
			} else {
				onLHS = true;
				pe->right->accept(this);
				onLHS = false;
				if(pe->right->isMemberAccess()) {
					bytecode->incr_field(lastMemberReferenced);
					bytecode->load_field(lastMemberReferenced);
					return;
				}
				switch(variableInfo.position) {
					case LOCAL:
						bytecode->incr_slot(variableInfo.slot);
						bytecode->load_slot_n(variableInfo.slot);
						break;
					case MODULE:
						bytecode->incr_module_slot(variableInfo.slot);
						bytecode->load_module_slot(variableInfo.slot);
						break;
					case CLASS:
						bytecode->incr_object_slot(variableInfo.slot);
						bytecode->load_object_slot(variableInfo.slot);
						break;
					case BUILTIN: {
						lnerr_(
						    "Built-in constant '%.*s' cannot be incremented!",
						    pe->right->token, pe->right->token.length,
						    pe->right->token.start);
						break;
					}
					default: break;
				}
			}
			break;
		case TOKEN_MINUS_MINUS:
			if(!pe->right->isAssignable()) {
				lnerr_("Cannot apply '--' on a non-assignable expression!",
				       pe->token);
			} else {
				frame->insertdebug(pe->token);
				onLHS = true;
				pe->right->accept(this);
				onLHS = false;
				if(pe->right->isMemberAccess()) {
					bytecode->decr_field(lastMemberReferenced);
					bytecode->load_field(lastMemberReferenced);
					return;
				}
				switch(variableInfo.position) {
					case LOCAL:
						bytecode->decr_slot(variableInfo.slot);
						bytecode->load_slot_n(variableInfo.slot);
						break;
					case MODULE:
						bytecode->decr_module_slot(variableInfo.slot);
						bytecode->load_module_slot(variableInfo.slot);
						break;
					case CLASS:
						bytecode->decr_object_slot(variableInfo.slot);
						bytecode->load_object_slot(variableInfo.slot);
						break;
					case BUILTIN: {
						lnerr_(
						    "Built-in constant '%.*s' cannot be decremented!",
						    pe->right->token, pe->right->token.length,
						    pe->right->token.start);
						break;
					}
					default: break;
				}
			}
			break;
		default: panic("Bad prefix operator!");
	}
}

void CodeGenerator::visit(PostfixExpression *pe) {
#ifdef DEBUG
	dinfo("");
	pe->token.highlight();
#endif
	if(!pe->left->isAssignable()) {
		lnerr_("Cannot apply postfix operator on a non-assignable expression!",
		       pe->token);
	}
	onLHS = true;
	pe->left->accept(this);
	onLHS = false;
	switch(pe->token.type) {
		case TOKEN_PLUS_PLUS:
			if(pe->left->isMemberAccess()) {
				bytecode->load_field_pushback(lastMemberReferenced);
				bytecode->incr_field(lastMemberReferenced);
				// pop off the object load_field_pushback pushed back
				bytecode->pop2();
				return;
			}
			switch(variableInfo.position) {
				case LOCAL:
					bytecode->load_slot_n(variableInfo.slot);
					bytecode->incr_slot(variableInfo.slot);
					break;
				case MODULE:
					bytecode->load_module_slot(variableInfo.slot);
					bytecode->incr_module_slot(variableInfo.slot);
					break;
				case CLASS:
					bytecode->load_object_slot(variableInfo.slot);
					bytecode->incr_object_slot(variableInfo.slot);
					break;
				case BUILTIN: {
					lnerr_("Built-in constant '%.*s' cannot be incremented!",
					       pe->left->token, pe->left->token.length,
					       pe->left->token.start);
					break;
				}
				default: break;
			}
			break;
		case TOKEN_MINUS_MINUS:
			if(pe->left->isMemberAccess()) {
				bytecode->load_field_pushback(lastMemberReferenced);
				bytecode->decr_field(lastMemberReferenced);
				// pop off the object load_field_pushback pushed back
				bytecode->pop2();
				return;
			}
			switch(variableInfo.position) {
				case LOCAL:
					bytecode->load_slot_n(variableInfo.slot);
					bytecode->decr_slot(variableInfo.slot);
					break;
				case MODULE:
					bytecode->load_module_slot(variableInfo.slot);
					bytecode->decr_module_slot(variableInfo.slot);
					break;
				case CLASS:
					bytecode->load_object_slot(variableInfo.slot);
					bytecode->decr_object_slot(variableInfo.slot);
					break;
				case BUILTIN: {
					lnerr_("Built-in constant '%.*s' cannot be decremented!",
					       pe->left->token, pe->left->token.length,
					       pe->left->token.start);
					break;
				}
				default: break;
			}
			break;
		default:
			panic("Bad postfix operator '%s'!",
			      Token::TokenNames[pe->token.type]);
	}
}

void CodeGenerator::visit(VariableExpression *vis) {
#ifdef DEBUG
	dinfo("");
	vis->token.highlight();
#endif
	NextString name =
	    StringLibrary::insert(string(vis->token.start, vis->token.length));
	if(!onRefer) {
		VarInfo var = lookForVariable(vis->token);
		frame->insertdebug(vis->token);
		if(onLHS) { // in case of LHS, just pass on the information
			variableInfo = var;
			onLHS        = false;
		} else {
			switch(var.position) {
				case LOCAL: bytecode->load_slot_n(var.slot); break;
				case MODULE: bytecode->load_module_slot(var.slot); break;
				case CLASS: bytecode->load_object_slot(var.slot); break;
				case BUILTIN: bytecode->load_constant(name); break;
				default: break;
			}
		}
	} else {
		if(onLHS)
			lastMemberReferenced = name;
		else
			bytecode->load_field(name);
	}
}

void CodeGenerator::visit(IfStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	ifs->condition->accept(this);
	frame->insertdebug(ifs->token);
	int jif = bytecode->jumpiffalse(0), jumpto = 0, exitif = -1;
	ifs->thenBlock->accept(this);
	if(ifs->elseBlock != nullptr) {
		exitif = bytecode->jump(0);
		jumpto = bytecode->getip();
		ifs->elseBlock->accept(this);
	} else
		jumpto = bytecode->getip();
	// patch the conditional jump
	bytecode->jumpiffalse(jif, jumpto - jif);
	// if there is an else block, patch the
	// exit jump
	if(exitif != -1)
		bytecode->jump(exitif, bytecode->getip() - exitif);
}

void CodeGenerator::visit(WhileStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	if(!ifs->isDo) {
		int pos = bytecode->getip();
		ifs->condition->accept(this);
		frame->insertdebug(ifs->token);
		int loopexit = bytecode->jumpiffalse(0);
		ifs->thenBlock->accept(this);
		bytecode->jump(pos - bytecode->getip());
		bytecode->jumpiffalse(loopexit, bytecode->getip() - loopexit);
	} else {
		int pos = bytecode->getip();
		ifs->thenBlock->accept(this);
		ifs->condition->accept(this);
		frame->insertdebug(ifs->token);
		bytecode->jumpiftrue(pos - bytecode->getip());
	}
}

void CodeGenerator::visit(ReturnStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	ifs->expr->accept(this);
	frame->insertdebug(ifs->token);
	bytecode->ret();
}

void CodeGenerator::visit(ForStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	if(ifs->is_iterator) {
		// iterators
		// first, validate the in expression
		BinaryExpression *it = (BinaryExpression *)ifs->init[0].get();
		if(it->left->type != Expr::VARIABLE) {
			lnerr_("Iterator assignment is not a variable!", it->left->token);
		} else {
			// create a new scope
			pushScope();
			// create a temporary slot to store the iterator pointer
			int slot = createTempSlot();
			// get info about the assignment variable
			VarInfo var = lookForVariable(it->left->token, true);
			// then, evalute the RHS
			it->right->accept(this);
			// initialize the iterator
			bytecode->iterate_init(slot);
			// iterate
			int pos = bytecode->iterate_next(slot, 0);
			// store the iterated value in the given variable
			switch(var.position) {
				case LOCAL: bytecode->store_slot_pop(var.slot); break;
				case MODULE:
					bytecode->store_module_slot(var.slot);
					bytecode->pop();
					break;
				case CLASS:
					bytecode->store_object_slot(var.slot);
					bytecode->pop();
					break;
				default: break;
			}
			// execute the body
			ifs->body->accept(this);
			// jump back to iterate
			bytecode->jump(pos - bytecode->getip());
			// patch the exit
			bytecode->iterate_next(pos, slot, bytecode->getip() - pos);
			// finally, pop the RHS
			bytecode->pop();
			// pop the scope
			popScope();
		}
	} else {
		// push a new scope so that
		// if any new variables are declared,
		// they will only be visible to
		// the loop
		pushScope();
		// evaluate the initializers
		if(ifs->init.size() > 0) {
			for(auto &a : ifs->init) {
				a->accept(this);
				// pop the result
				bytecode->pop();
			}
		}
		// evalute the condition
		int cond_at    = bytecode->getip();
		int patch_exit = -1;
		if(ifs->cond.get() != NULL) {
			ifs->cond->accept(this);
			// exit if the condition is violated
			patch_exit = bytecode->jumpiffalse(0);
		}
		// evalute the body
		ifs->body->accept(this);
		// evalute the incrementers
		if(ifs->incr.size() > 0) {
			for(auto &a : ifs->incr) {
				a->accept(this);
				// pop the result
				bytecode->pop();
			}
		}
		// come out to the parent scope
		popScope();
		// finally, jump back to the beginning
		bytecode->jump(cond_at - bytecode->getip());
		// and patch the exit
		if(patch_exit != -1) {
			bytecode->jumpiffalse(patch_exit, bytecode->getip() - patch_exit);
		}
	}
}

NextString CodeGenerator::generateSignature(const string &name, int arity) {
	string sig = name + "(";
	if(arity > 0) {
		sig += "_";
		size_t bak = arity - 1;
		while(bak != 0) {
			sig += ",_";
			bak--;
		}
	}
	sig += ")";
#ifdef DEBUG
	cout << "Signature generated : " << sig << "\n";
#endif
	return StringLibrary::insert(sig);
}

NextString CodeGenerator::generateSignature(const Token &name, int arity) {
	return generateSignature(string(name.start, name.length), arity);
}

void CodeGenerator::visit(FnStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	NextString signature;
	uint64_t   sym;
	// Constructors are registered as module functions,
	// and are called in the same way as normal functions.
	// For example, a class named LinkedList which has a
	// constructor like 'new(x, y, z)' will generate a
	// signature 'LinkedList(_,_,_)' which will be registered
	// to the module as a function. This means you can't
	// have another function named 'LinkedList(x, y, z)' in
	// the same module, which otherwise would've been also
	// syntatically ambiguous, since functions and constructors
	// are called with the exact same syntax.
	bool inConstructor = inClass && ifs->isConstructor;
	if(inConstructor) {
		signature = generateSignature(StringLibrary::get(currentClass->name),
		                              ifs->arity);
	} else {
		signature = generateSignature(ifs->name, ifs->arity);
	}
	if(inClass) {
		sym = SymbolTable::insertSymbol(signature);
	}
	if(getState() == COMPILE_DECLARATION) {
		if((!inClass || inConstructor) && module->hasSignature(signature)) {
			if(inConstructor) {
				lnerr_("Ambiguous constructor declaration for class '%s'!",
				       ifs->name, StringLibrary::get_raw(currentClass->name));

			} else {
				lnerr_("Redefinition of function with same signature!",
				       ifs->name);
			}
			lnerr("Previously declared at : ",
			      module->functions.find(signature)->second->token);
			module->functions.find(signature)->second->token.highlight();
		} else if(inClass && currentClass->hasMethod(sym)) {
			lnerr_("Redefinition of method with same signature!", ifs->name);
			lnerr("Previously declared at : ",
			      currentClass->functions[sym]->token);
			currentClass->functions[sym]->token.highlight();

		} else {
			FnPtr f = unq(Fn,
			              !inClass ? (AccessModifiableEntity::Visibility)(
			                             VIS_PUB + ifs->visibility)
			                       : currentVisibility,
			              frame, module);
			f->name = StringLibrary::insert(
			    string(ifs->name.start, ifs->name.length));
			f->token = ifs->name;
			f->arity = ifs->arity;
			// f->frame                       = unq(Frame, frame, module);
			f->isConstructor   = inConstructor;
			f->frame->isStatic = ifs->isStatic;
			if(!inClass || inConstructor) {
				module->frames.push_back(f->frame.get());
				module->symbolTable[signature] = f.get();
				module->functions[signature]   = FnPtr(f.release());
			} else {
				currentClass->insertMethod(sym, f.release());
			}
		}
	} else {
		if(!inClass || inConstructor)
			initFrame(module->functions[signature]->frame.get(), ifs->name);
		else
			initFrame(currentClass->functions[sym]->frame.get(), ifs->name);

		if(inClass || inConstructor) {
			// Object will be stored in 0
			frame->declareVariable("this", 4, scopeID + 1);
		}

		if(ifs->isNative) {
			if(!inClass)
				module->functions[signature]->isNative = true;
			else
				currentClass->functions[sym]->isNative = true;
			popFrame();
		} else {
			if(inConstructor) {
				bytecode->construct(module->name, currentClass->name);
			}
			ifs->body->accept(this);

			// Each function returns nil by default
			// Only module root emits 'halt'
			// Only constructor emits construct_ret

			if(inConstructor) {
				bytecode->construct_ret();
			} else {
				// Even if the last bytecode was ret, we can't be sure
				// that's the last statement of the function because of
				// branching and looping. So we add a 'ret' anyway.
				bytecode->pushn();
				bytecode->ret();
			}

			popFrame();
		}
	}

	// panic("Not yet implemented!");
}

void CodeGenerator::visit(FnBodyStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	for(auto i = ifs->args.begin(), j = ifs->args.end(); i != j; i++) {
		// body will automatically contain a block statement,
		// which will obviously push a new scope.
		// So we speculatively do that here.
		frame->declareVariable((*i).start, (*i).length, scopeID + 1);
	}
	ifs->body->accept(this);
}

void CodeGenerator::visit(BlockStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	pushScope();
	// Back up the previous stack specifications
	int present = bytecode->stackSize();
	for(auto i = ifs->statements.begin(), j = ifs->statements.end(); i != j;
	    i++)
		(*i)->accept(this);
	bytecode->restoreStackSize(present);
	popScope();
}

void CodeGenerator::visit(ExpressionStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	for(auto i = ifs->exprs.begin(), j = ifs->exprs.end(); i != j; i++) {
		i->get()->accept(this);
		// An expression should always return a value.
		// Pop the value to minimize the stack length
		bytecode->pop2();
	}
}

void CodeGenerator::visit(PrintStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	for(auto i = ifs->exprs.begin(), j = ifs->exprs.end(); i != j; i++) {
		(*i)->accept(this);
		frame->insertdebug(ifs->token);
		bytecode->print();
	}
}

void CodeGenerator::visit(ClassStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	NextString className =
	    StringLibrary::insert(ifs->name.start, ifs->name.length);
	if(getState() == COMPILE_DECLARATION) {
		if(module->classes.find(className) != module->classes.end()) {
			lnerr_("Class '%s' is already declared in module '%s'!", ifs->name,
			       StringLibrary::get_raw(className),
			       StringLibrary::get_raw(module->name));
			lnerr("Previously declared at : ",
			      module->classes[className]->token);
			module->classes[className]->token.highlight();
		}
		ClassPtr c = unq(
		    NextClass, (AccessModifiableEntity::Visibility)(VIS_PUB + ifs->vis),
		    module, className);
		c->token                   = ifs->name;
		module->classes[className] = ClassPtr(c.release());
		inClass                    = true;
		currentClass               = module->classes[className].get();
		pushScope();
		for(auto i = ifs->declarations.begin(), j = ifs->declarations.end();
		    i != j; i++) {
			(*i)->accept(this);
		}
		popScope();
		inClass      = false;
		currentClass = NULL;
	} else {
		inClass      = true;
		currentClass = module->classes[className].get();
		for(auto i = ifs->declarations.begin(), j = ifs->declarations.end();
		    i != j; i++) {
			(*i)->accept(this);
		}
		inClass      = false;
		currentClass = NULL;
	}
}

void CodeGenerator::visit(ImportStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	if(getState() == COMPILE_IMPORTS) {
		Token      last     = *(ifs->import.end() - 1);
		NextString lastName = StringLibrary::insert(last.start, last.length);
		// Check for collisions first
		if(module->importedModules.find(lastName) !=
		   module->importedModules.end()) {
			// lnerr_("Import collision between two modules of the same name!",
			//       ifs->token);
			// the module is already imported
			// skip
			return;
		}
		ImportStatus is = import(ifs->import);
		Token        t =
		    ifs->import[is.toHighlight ? is.toHighlight - 1 : is.toHighlight];
		switch(is.res) {
			case ImportStatus::BAD_IMPORT: {
				lnerr_("Folder does not exist or is not accessible!", t);
				break;
			}
			case ImportStatus::FOLDER_IMPORT: {
				lnerr_("Importing a folder is not supported!", t);
				break;
			}
			case ImportStatus::FILE_NOT_FOUND: {
				lnerr_("No such module found in the given folder!", t);
				break;
			}
			case ImportStatus::IMPORT_SUCCESS: {
				Module *m = compile_and_load(is.fileName);
				if(m == NULL) {
					// compilation of the module failed
					lnerr_("Compilation of imported module failed!", t);
					break;
				}
				// The name of the imported module for the
				// importee module would be the last part
				// of the import statement
				// i.e.:    import sys.io
				// Registered module name : io
				module->importedModules[lastName] = m;
				// Warn if a variable name shadows the imported
				// module
				if(module->frame->slots.find(lastName) !=
				   module->frame->slots.end()) {
					lnwarn("Variable '%s' shadows imported module!",
					       ifs->token);
				} else {
					VarInfo v = lookForVariable(lastName, true);
					bytecode->push(Value(m));
					bytecode->store_slot(v.slot);
					bytecode->pop2();
				}
				break;
			}
		}
	}
}

void CodeGenerator::visit(VardeclStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	VarInfo v = lookForVariable(ifs->token, true, false, ifs->vis);
	if(getState() == COMPILE_BODY) {
		if(v.position == BUILTIN) {
			lnerr_("Built-in constant '%.*s' cannot be reassigned!", ifs->token,
			       ifs->token.length, ifs->token.start);
		}
		ifs->expr->accept(this);
		bytecode->store_slot(v.slot);
		bytecode->pop2();
	}
}

void CodeGenerator::visit(MemberVariableStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	if(getState() == COMPILE_DECLARATION) {
		for(auto i = ifs->members.begin(), j = ifs->members.end(); i != j;
		    i++) {
			NextString name = StringLibrary::insert((*i).start, (*i).length);
			if(currentClass->members.find(name) !=
			   currentClass->members.end()) {
				lnerr_("Member '%s' variable already declared!", (*i),
				       StringLibrary::get_raw(name));
				lnerr("Previously declared at : ",
				      currentClass->members[name].token);
				currentClass->members[name].token.highlight();
				lnerr("Redefined at : ", (*i));
			} else {
				currentClass->declareVariable(name, currentVisibility,
				                              ifs->isStatic, (*i));
			}
		}
	}
}

void CodeGenerator::visit(VisibilityStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	currentVisibility = ifs->token.type == TOKEN_pub
	                        ? AccessModifiableEntity::PUB
	                        : AccessModifiableEntity::PRIV;
}

void CodeGenerator::visit(TryStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	int from = bytecode->getip();
	ifs->tryBlock->accept(this);
	// if the try block succeeds, we should get out
	// of all the remaining catch blocks
	int skipAll = bytecode->jump(0);
	int to      = bytecode->getip() - 1;
	int bf = tryBlockStart, bt = tryBlockEnd;
	tryBlockStart = from;
	tryBlockEnd   = to;
	// there will be a value pushed on to the stack
	// if an exception occurs, so count for it
	bytecode->insertSlot();
	// after one catch block is executed, the control
	// should get out of remaining catch blocks
	vector<int> jumpAddresses;
	for(auto i = ifs->catchBlocks.begin(), j = ifs->catchBlocks.end(); i != j;
	    i++) {
		(*i)->accept(this);
		// keep a backup of the jump opcode address
		jumpAddresses.push_back(bytecode->jump(0));
	}
	for(auto i = jumpAddresses.begin(), j = jumpAddresses.end(); i != j; i++) {
		// patch the jump addresses
		bytecode->jump((*i), bytecode->getip() - (*i));
	}
	// patch the try jump
	bytecode->jump(skipAll, bytecode->getip() - skipAll);
	tryBlockStart = bf;
	tryBlockEnd   = bt;
}

void CodeGenerator::visit(CatchStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	NextString tname =
	    StringLibrary::insert(ifs->typeName.start, ifs->typeName.length);
	NextType t;
	if(NextType::isPrimitive(tname)) {
		t = NextType::getPrimitiveType(tname);
	} else {
		t = module->resolveType(tname);
		if(t == NextType::Error) {
			lnerr_("No such type found in present module!", ifs->typeName);
		}
	}
	// catch block will push a new scope
	int slot = frame->declareVariable(ifs->varName.start, ifs->varName.length,
	                                  scopeID + 1);
	frame->handlers->push_back(
	    (ExHandler){tryBlockStart, tryBlockEnd, bytecode->getip(), t});
	bytecode->store_slot(slot); // store the thrown object
	bytecode->pop2();           // pop it manually since store_slot doesn't
	ifs->block->accept(this);
}

void CodeGenerator::visit(ThrowStatement *ifs) {
#ifdef DEBUG
	dinfo("");
	ifs->token.highlight();
#endif
	ifs->expr->accept(this);
	bytecode->throw_();
}
