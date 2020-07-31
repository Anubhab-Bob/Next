#pragma once

#include "../gc.h"
#include "../value.h"

// the sete method on the structs allocates a
// new struct and sets it as pendingException
// in the engine. they always return nil.

struct TypeError {
	GcObject obj;

	String *ontype;
	String *method;
	String *expected;
	Value   received;
	int     argumentNumber;

	static TypeError *create(String *o, String *m, String *e, Value r, int arg);
	static Value      sete(String *o, String *m, String *e, Value r, int arg);
	static Value      sete(const char *o, const char *m, const char *e, Value r,
	                       int arg);

	static void init();

	void mark() const {
		GcObject::mark(ontype);
		GcObject::mark(method);
		GcObject::mark(expected);
		GcObject::mark(received);
	}
	void release() {}
#ifdef DEBUG_GC
	const char *gc_repr();
#endif
};

struct RuntimeError {
	GcObject obj;

	String *message;

	static RuntimeError *create(String *msg);
	static Value         sete(String *msg);
	static Value         sete(const char *msg);

	static void init();

	void mark() const { GcObject::mark(message); }

	void release() {}
#ifdef DEBUG_GC
	const char *gc_repr();
#endif
};

struct IndexError {
	GcObject obj;

	String *message;
	long    low, hi, received;

	static IndexError *create(String *m, long lo, long hi, long received);
	static Value       sete(String *m, long l, long h, long r);
	static Value       sete(const char *m, long l, long h, long r);

	static void init();

	void mark() const { GcObject::mark(message); }

	void release() {}
#ifdef DEBUG_GC
	const char *gc_repr();
#endif
};

struct FormatError {
	GcObject obj;

	String *message;

	static FormatError *create(String *msg);
	static Value        sete(String *msg);
	static Value        sete(const char *msg);

	static void init();

	void mark() const { GcObject::mark(message); }

	void release() {}

#ifdef DEBUG_GC
	const char *gc_repr();
#endif
};

#define RERR(x) return RuntimeError::sete(x);
#define IDXERR(m, l, h, r) return IndexError::sete(m, l, h, r);

#define EXPECT(obj, name, idx, type)                               \
	if(!args[idx].is##type()) {                                    \
		return TypeError::sete(#obj, name, #type, args[idx], idx); \
	}

#define FERR(x) return FormatError::sete(x);
