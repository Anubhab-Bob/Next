#include "errors.h"
#include "../engine.h"
#include "class.h"
#include "string.h"

TypeError *TypeError::create(String *o, String *m, String *e, Value r,
                             int arg) {
	TypeError *t      = GcObject::allocTypeError();
	t->ontype         = o;
	t->method         = m;
	t->expected       = e;
	t->received       = r;
	t->argumentNumber = arg;
	return t;
}

Value TypeError::sete(String *o, String *m, String *e, Value r, int arg) {
	TypeError *t = TypeError::create(o, m, e, r, arg);
	ExecutionEngine::setPendingException(Value(t));
	return ValueNil;
}

Value TypeError::sete(const char *o, const char *m, const char *e, Value r,
                      int arg) {
	return sete(String::from(o), String::from(m), String::from(e), r, arg);
}

Value next_typeerror_object_type(const Value *args) {
	return Value(args[0].toTypeError()->ontype);
}

Value next_typeerror_method_name(const Value *args) {
	return Value(args[0].toTypeError()->method);
}

Value next_typeerror_expected_type(const Value *args) {
	return Value(args[0].toTypeError()->expected);
}

Value next_typeerror_received_value(const Value *args) {
	return args[0].toTypeError()->received;
}

Value next_typeerror_argument_number(const Value *args) {
	return Value((double)args[0].toTypeError()->argumentNumber);
}

void TypeError::init() {
	Class *TypeErrorClass = GcObject::TypeErrorClass;

	TypeErrorClass->init("type_error", Class::ClassType::BUILTIN);

	TypeErrorClass->add_builtin_fn("object_type()", 0,
	                               next_typeerror_object_type);
	TypeErrorClass->add_builtin_fn("method_name()", 0,
	                               next_typeerror_method_name);
	TypeErrorClass->add_builtin_fn("expected_type()", 0,
	                               next_typeerror_expected_type);
	TypeErrorClass->add_builtin_fn("received_value()", 0,
	                               next_typeerror_received_value);
	TypeErrorClass->add_builtin_fn("argument_number()", 0,
	                               next_typeerror_argument_number);
}

void TypeError::mark() {
	GcObject::mark(ontype);
	GcObject::mark(method);
	GcObject::mark(expected);
	GcObject::mark(received);
}

void TypeError::print(std::ostream &o) {
	o << "Expected argument " << argumentNumber << " of method " << ontype->str
	  << "." << method->str << "() to be '" << expected->str << "'! Received '"
	  << received << "'!";
}

std::ostream &operator<<(std::ostream &o, const TypeError &a) {
	(void)a;
	return o << "<type_error object>";
}

RuntimeError *RuntimeError::create(String *m) {
	RuntimeError *re = GcObject::allocRuntimeError();
	re->message      = m;
	return re;
}

Value RuntimeError::sete(String *m) {
	ExecutionEngine::setPendingException(create(m));
	return ValueNil;
}

Value RuntimeError::sete(const char *m) {
	return sete(String::from(m));
}

Value next_runtimerror_str(const Value *args) {
	return Value(args[0].toRuntimeError()->message);
}

void RuntimeError::init() {
	Class *RuntimeErrorClass = GcObject::RuntimeErrorClass;

	RuntimeErrorClass->init("runtime_error", Class::ClassType::BUILTIN);

	RuntimeErrorClass->add_builtin_fn("str()", 0, next_runtimerror_str);
}

void RuntimeError::mark() {
	GcObject::mark(message);
}

void RuntimeError::print(std::ostream &o) {
	o << message->str;
}

std::ostream &operator<<(std::ostream &o, const RuntimeError &a) {
	(void)a;
	return o << "<runtime_error object>";
}

IndexError *IndexError::create(String *m, long l, long h, long r) {
	IndexError *ie = GcObject::allocIndexError();
	ie->message    = m;
	ie->hi         = h;
	ie->low        = l;
	ie->received   = r;
	return ie;
}

Value IndexError::sete(String *m, long l, long h, long r) {
	ExecutionEngine::setPendingException(Value(create(m, l, h, r)));
	return ValueNil;
}

Value IndexError::sete(const char *m, long l, long h, long r) {
	return sete(String::from(m), l, h, r);
}

Value next_indexerror_from(const Value *args) {
	return Value((double)args[0].toIndexError()->low);
}

Value next_indexerror_to(const Value *args) {
	return Value((double)args[0].toIndexError()->hi);
}

Value next_indexerror_received(const Value *args) {
	return Value((double)args[0].toIndexError()->received);
}

void IndexError::init() {
	Class *IndexErrorClass = GcObject::IndexErrorClass;

	IndexErrorClass->init("index_error", Class::ClassType::BUILTIN);

	IndexErrorClass->add_builtin_fn("from()", 0, next_indexerror_from);
	IndexErrorClass->add_builtin_fn("to()", 0, next_indexerror_to);
	IndexErrorClass->add_builtin_fn("received()", 0, next_indexerror_received);
}

void IndexError::mark() {
	GcObject::mark(message);
}

void IndexError::print(std::ostream &o) {
	o << "Index should be between " << low << " <= index <= " << hi
	  << ". Received " << received << "!";
}

std::ostream &operator<<(std::ostream &o, const IndexError &a) {
	(void)a;
	return o << "<index_error object>";
}

void Error::print_error(GcObject *o, std::ostream &os) {
	switch(o->objType) {
		case GcObject::OBJ_RuntimeError: ((RuntimeError *)o)->print(os); break;
		case GcObject::OBJ_IndexError: ((IndexError *)o)->print(os); break;
		case GcObject::OBJ_TypeError: ((TypeError *)o)->print(os); break;
		default: break;
	}
}
