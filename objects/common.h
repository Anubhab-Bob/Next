#pragma once
#include "../value.h"

typedef Value (*next_builtin_fn)(const Value *args);

enum Visibility : uint8_t { PUBLIC = 0, PRIVATE = 1 };

#define NEXT_BUILTIN(type, name) Value next_##type##_##name(const Value *args)
