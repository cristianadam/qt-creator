// Copyright (c) 2026 Roberto Raggi <roberto.raggi@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cxx/cxx_fwd.h>

#include <string>
#include <utility>
#include <vector>

namespace cxx {
class Name;
class ScopeSymbol;
class Symbol;

#define CXX_FOR_EACH_TYPE_KIND(V) \
  V(Void)                         \
  V(Nullptr)                      \
  V(DecltypeAuto)                 \
  V(Auto)                         \
  V(Bool)                         \
  V(SignedChar)                   \
  V(ShortInt)                     \
  V(Int)                          \
  V(LongInt)                      \
  V(LongLongInt)                  \
  V(Int128)                       \
  V(UnsignedChar)                 \
  V(UnsignedShortInt)             \
  V(UnsignedInt)                  \
  V(UnsignedLongInt)              \
  V(UnsignedLongLongInt)          \
  V(UnsignedInt128)               \
  V(Char)                         \
  V(Char8)                        \
  V(Char16)                       \
  V(Char32)                       \
  V(WideChar)                     \
  V(Float)                        \
  V(Double)                       \
  V(LongDouble)                   \
  V(Float16)                      \
  V(Qual)                         \
  V(BoundedArray)                 \
  V(UnboundedArray)               \
  V(Pointer)                      \
  V(LvalueReference)              \
  V(RvalueReference)              \
  V(Function)                     \
  V(Class)                        \
  V(Enum)                         \
  V(ScopedEnum)                   \
  V(MemberObjectPointer)          \
  V(MemberFunctionPointer)        \
  V(Namespace)                    \
  V(TypeParameter)                \
  V(TemplateTypeParameter)        \
  V(UnresolvedName)               \
  V(UnresolvedBoundedArray)       \
  V(UnresolvedUnderlying)         \
  V(UnresolvedBuiltin)            \
  V(OverloadSet)                  \
  V(BuiltinVaList)                \
  V(BuiltinMetaInfo)              \
  V(BitInt)                       \
  V(UnsignedBitInt)               \
  V(UnresolvedBitInt)

class Type;

#define PROCESS_TYPE(K) class K##Type;
CXX_FOR_EACH_TYPE_KIND(PROCESS_TYPE)
#undef PROCESS_TYPE

#define PROCESS_TYPE(K) k##K,
enum class TypeKind { CXX_FOR_EACH_TYPE_KIND(PROCESS_TYPE) };
#undef PROCESS_TYPE

enum class CvQualifiers {
  kNone = 0,
  kConst = 1,
  kVolatile = 2,
  kConstVolatile = kConst | kVolatile,
};

[[nodiscard]] constexpr auto operator|(CvQualifiers a, CvQualifiers b)
    -> CvQualifiers {
  return CvQualifiers(std::to_underlying(a) | std::to_underlying(b));
}

[[nodiscard]] constexpr auto operator&(CvQualifiers a, CvQualifiers b)
    -> CvQualifiers {
  return CvQualifiers(std::to_underlying(a) & std::to_underlying(b));
}

[[nodiscard]] constexpr auto operator~(CvQualifiers a) -> CvQualifiers {
  return CvQualifiers(~std::to_underlying(a) &
                      std::to_underlying(CvQualifiers::kConstVolatile));
}

constexpr auto operator|=(CvQualifiers& a, CvQualifiers b) -> CvQualifiers& {
  a = a | b;
  return a;
}

constexpr auto operator&=(CvQualifiers& a, CvQualifiers b) -> CvQualifiers& {
  a = a & b;
  return a;
}

[[nodiscard]] constexpr auto has_const(CvQualifiers cv) -> bool {
  return (cv & CvQualifiers::kConst) != CvQualifiers::kNone;
}

[[nodiscard]] constexpr auto has_volatile(CvQualifiers cv) -> bool {
  return (cv & CvQualifiers::kVolatile) != CvQualifiers::kNone;
}

[[nodiscard]] constexpr auto is_at_least_as_cv_qualified(CvQualifiers cv,
                                                         CvQualifiers other)
    -> bool {
  return (other & ~cv) == CvQualifiers::kNone;
}

[[nodiscard]] constexpr auto is_more_cv_qualified(CvQualifiers cv,
                                                  CvQualifiers other) -> bool {
  return cv != other && is_at_least_as_cv_qualified(cv, other);
}

enum class RefQualifier {
  kNone,
  kLvalue,
  kRvalue,
};

struct TypeParamInfo {
  int index = 0;
  int depth = 0;
  bool isPack = false;
};

struct TypePrintOptions {
  bool omitFunctionReturnType = false;

  // Print a class, enum or namespace by its own name rather than by the path
  // to it. What a name has to be qualified with depends on where it is being
  // shown, which the printer cannot know; a tool showing a declaration in the
  // scope it was written in wants the short form.
  bool omitEnclosingScope = false;

  // Print a class template by the name it was declared under, without
  // the arguments it was given. A tool with a rule per type matches the
  // type it has a rule for by that name -- a setting about QList is
  // about every QList -- and cannot take the arguments off the answer
  // itself, a type being written around a name and not in front of it.
  bool omitTemplateArguments = false;

  // Leave out the exception specification. A tool showing a declaration in a
  // list -- an outline, a completion popup -- has a line to fill and shows
  // what tells one declaration from another, and whether a function throws
  // is not part of that.
  bool omitExceptionSpecification = false;

  // Where the answer is going to be written. A class, enum or namespace is
  // then named with as little in front of it as still finds it from there:
  // plain C inside the namespace that declares it, N::C outside, and the
  // whole path where nothing shorter reaches it.
  //
  // This is what a tool writing a declaration into another file needs, and
  // it is neither of the two the options above offer: the path is right
  // nowhere in particular, the bare name only where the reader happens to be
  // standing in the right scope. Ignored when omitEnclosingScope is set,
  // which is the stronger instruction.
  ScopeSymbol* writtenIn = nullptr;

  // The names to write the parameters of a function under, in order. A
  // parameter's name is not part of a type, so nothing here can be worked
  // out from the type being printed; a tool writing a definition out has
  // read the names off the declaration and is the one that knows them.
  //
  // Fewer names than parameters, or an empty one, leaves those unnamed.
  // They apply to the function being printed and not to any function type
  // written inside it: the names of a function pointer parameter's own
  // parameters are not these.
  std::vector<std::string> parameterNames;

  // What the type belongs to, for writing the template parameters in it
  // under the names they were given. A type parameter is a depth and an
  // index and has no name of its own -- one type stands for the first
  // parameter of every template there is -- so the names can only come
  // from the declaration the type was read off.
  //
  // Without this a parameter is written as "type-param<0, 0>", which says
  // what it is rather than what it is called, and a list showing
  // declarations to a reader wants the latter.
  Symbol* templateParametersOf = nullptr;

  // Where the spaces go around the * and & of a pointer or a reference,
  // which is a matter of style and not of meaning: "char* s" by default,
  // "char *s" binding them to the name, "char * s" with both spaces, and
  // "char*s" with neither.
  //
  // A tool that rewrites declarations to a chosen style cannot work this
  // out afterwards: which run of characters in the answer is the pointer
  // operator, and which of the spaces around it were asked for, is only
  // plain while the type is being written. Left alone where the operators
  // stand inside parentheses -- the (* of a pointer to a function or to an
  // array -- since there the spelling is not a choice.
  bool spaceBeforePointerOperators = false;
  bool spaceAfterPointerOperators = true;
};

auto to_string(const Type* type, const std::string& id = "",
               TypePrintOptions options = {}) -> std::string;
// The name \a symbol is declared under, written as somebody standing where
// the answer is going would write it: as little in front of it as still
// finds this very symbol from there, by the same rule writtenIn applies to
// the names inside a type.
//
// This is the other half of writing a declaration into another file. The
// types in it are written for the place by printing them with writtenIn;
// what the declaration is *of* is this -- "C::f" written outside the class
// and "f" within it -- and a caller cannot work it out without redoing the
// search this does.
//
// Without writtenIn the answer is the whole path, which is what the option
// means everywhere else.
auto to_string(Symbol* symbol, TypePrintOptions options = {}) -> std::string;

auto to_string(const Type* type, const Name* name,
               TypePrintOptions options = {}) -> std::string;
}  // namespace cxx
