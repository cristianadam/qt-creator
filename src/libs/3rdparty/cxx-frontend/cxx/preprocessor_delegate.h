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

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cxx {

// What the preprocessor did on the way to its output.
//
// The tokens it produces say what the translation unit is; they do not say how
// it got there. A tool that shows the source rather than compiles it needs the
// second as well: which name was a macro, where it was used and with which
// arguments, which lines the conditionals left out, which macro guards a
// header. All of it is known while preprocessing and none of it survives into
// the token stream, so it is reported here as it happens.
//
// Every callback has an empty default: a delegate implements the ones it cares
// about. Nothing is computed unless a delegate is set.

// A half-open range of bytes in one of the files the preprocessor has read.
// Pair the id with Preprocessor::sourceFileName() or source() to make
// something of it.
struct PreprocessorRange {
  std::uint32_t fileId = 0;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;

  [[nodiscard]] auto end() const -> std::uint32_t { return offset + length; }
  [[nodiscard]] explicit operator bool() const { return fileId != 0; }
};

// A macro, as it was defined.
struct MacroInfo {
  std::string_view name;

  // The parameters of a function-like macro, in order, without the trailing
  // ... of a variadic one. Empty for an object-like macro.
  std::span<const std::string> parameters;

  // The spelling of the replacement list. Empty for a built-in macro, whose
  // body is code rather than tokens.
  std::string_view body;

  // Where the name of the macro appears in its #define. Absent for a built-in.
  PreprocessorRange definition;

  bool isFunctionLike = false;
  bool isVariadic = false;
  bool isBuiltin = false;
};

// A use of a macro name.
struct MacroUse {
  const MacroInfo* macro = nullptr;

  // Where the name appears.
  PreprocessorRange range;

  // False when the name was only asked about -- by defined() or #ifdef -- and
  // so was not replaced by anything.
  bool expanded = false;

  // For an expanded function-like macro, where each argument came from, in
  // order. An argument that came out of another expansion has no range of its
  // own and is reported empty.
  std::span<const PreprocessorRange> arguments;
};

class PreprocessorDelegate {
 public:
  PreprocessorDelegate() = default;
  virtual ~PreprocessorDelegate();

  PreprocessorDelegate(const PreprocessorDelegate&) = delete;
  auto operator=(const PreprocessorDelegate&) -> PreprocessorDelegate& = delete;

  // A #define. Called for a redefinition too, after the old one is gone.
  virtual void macroDefined(const MacroInfo& macro) {}

  // An #undef that removed a macro. Not called when the name was not defined.
  virtual void macroUndefined(const MacroInfo& macro, PreprocessorRange range) {
  }

  // A macro name was used, whether it was replaced or only asked about.
  virtual void macroUsed(const MacroUse& use) {}

  // defined() or #ifdef asked about a name that is not a macro.
  virtual void undefinedMacroUsed(std::string_view name,
                                  PreprocessorRange range) {}

  // The conditional directives left this out. The range runs from the end of
  // the directive that started skipping to the start of the one that ended it.
  virtual void regionSkipped(PreprocessorRange range) {}

  // The file just finished is guarded by this macro, so including it again
  // will produce nothing.
  virtual void includeGuardFound(std::uint32_t fileId,
                                 std::string_view macroName) {}

  // A #pragma, spanning the tokens after the keyword.
  virtual void pragmaDirective(PreprocessorRange range) {}
};

}  // namespace cxx
