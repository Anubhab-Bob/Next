#pragma once

#include "../gc.h"
#include "../value.h"

// the sete method on the structs allocates a
// new struct and sets it as pendingException
// in the engine. they always return nil.

// the base class for all Next generated exceptions.
// we use cpp inheritance to avoid copying the header,
// but each struct definitely needs its own create and
// sete methods, since those methods distinctly allocs
// objects with different tags for the Next runtime
// to distinguish.
// for the same reason, each class will also definitely
// need its own constructor.
struct Error {
	GcObject obj;

	String *message;

	static Error *create(const String2 &message);
	static Value  sete(const String2 &message);
	static Value  sete(const char *message);
	static void   init();

	static Value setTypeError(const String2 &o, const String2 &m,
	                          const String2 &e, Value r, int arg);
	static Value setTypeError(const char *o, const char *m, const char *e,
	                          Value r, int arg);
	static Value setIndexError(const char *m, int64_t l, int64_t h, int64_t r);

	void mark() { GcObject::mark(message); }
	void release() {}
#ifdef DEBUG_GC
	const char *gc_repr();
#endif
};

#ifdef DEBUG_GC
#define ERROR_GC_REPR const char *gc_repr();
#else
#define ERROR_GC_REPR
#endif

#define ERROR(x, name)                           \
	struct x : public Error {                    \
		static x *   create(const String2 &msg); \
		static Value sete(const String2 &msg);   \
		static Value sete(const char *msg);      \
		static void  init();                     \
		ERROR_GC_REPR                            \
	};
#include "error_types.h"
#undef ERROR_GC_REPR

#define RERR(x) return RuntimeError::sete(x);
#define IDXERR(m, l, h, r) return Error::setIndexError(m, l, h, r);

#define EXPECT(obj, name, idx, type)                                   \
	if(!args[idx].is##type()) {                                        \
		return Error::setTypeError(#obj, name, #type, args[idx], idx); \
	}

#define FERR(x) return FormatError::sete(x);
#define IMPORTERR(x) return ImportError::sete(x);
