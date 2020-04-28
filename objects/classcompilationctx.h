#pragma once
#include "../gc.h"
#include "class.h"
#include "map.h"
#include "set.h"

// everything that is needed during compilation
// is clubbed together in this struct.
// most of the members of this struct is only
// marked safe at compile time. at runtime, only
// members needed to report debug informations
// are kept, and everything else is unmarked, and
// freed in a subsequent gc.
struct ClassCompilationContext {
	GcObject  obj;
	ValueMap *members;            // string:slot
	ValueMap *public_signatures;  // string:token to report overload errors
	ValueMap *private_signatures; // string:token
	Class *   klass;              // generated runtime representation of a class
	int       slotCount;
	// super context
	struct ClassCompilationContext *moduleContext;
	// for module
	FunctionCompilationContext *defaultConstructor;

	static ClassCompilationContext *
	create(struct ClassCompilationContext *superContext, String *name);

	void add_public_class(Class *c);
	void add_private_class(Class *c);
	bool add_public_mem(String *name);
	bool add_private_mem(String *name);
	bool has_mem(String *name);
	// unchecked. use has_mem before
	int get_mem_slot(String *name);

	bool add_public_fn(String *sig, Function *f);
	bool add_private_fn(String *sig, Function *f);
	bool has_fn(String *sig);
	// unchecked. use has_fn before
	int get_fn_sym(String *sig);

	// called for modules
	FunctionCompilationContext *get_default_constructor();

	Class *get_class();

	static void init();
	// mark2 only marks runtime-necessary members
	void mark();
	void mark2();
	void release() {}
};
