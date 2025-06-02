#pragma once

#include "Bitfield.h"

#ifdef __cplusplus

#include <cmath>
#include "MathTypes.hpp"

#define SLANG_MUTATING
#define SLANG_DIFFERENTIABLE
#define SLANG_GENERIC(T, Name)
#define CPP_CONST const
#define CPP_TEMPLATE(T, Name) template<T Name>
#define THIS_REF *this
#define INOUT_ARG(T, name) T& name
#define OUT_ARG(T, name) T& name

#endif

#ifdef __SLANG_COMPILER__

#define SLANG_MUTATING [mutating]
#define SLANG_DIFFERENTIABLE [Differentiable]
#define SLANG_GENERIC(T, Name) <let Name : T>
#define CPP_CONST
#define CPP_TEMPLATE
#define THIS_REF this
#define INOUT_ARG(T, name) inout T name
#define OUT_ARG(T, name) out T name

#endif