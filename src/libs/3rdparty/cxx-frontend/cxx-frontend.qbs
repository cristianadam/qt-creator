// A snapshot of the parser library of https://github.com/robertoraggi/cplusplus.
// See README.md for the imported revision and how to refresh it.
//
// Kept in sync with CMakeLists.txt. cxx/flatbuffers/ is deliberately left out:
// AST serialization is not needed, hence CXX_NO_FLATBUFFERS.

QtcLibrary {
    name: "cxx-frontend"
    type: "staticlibrary"

    // Nothing in Qt Creator depends on this yet; it mirrors the
    // QTC_ENABLE_CXX_FRONTEND CMake option being off by default.
    builtByDefault: false

    useQt: false

    // Upstream requires C++23; the rest of Qt Creator is built as C++20.
    cpp.cxxLanguageVersion: "c++23"
    cpp.warningLevel: "none"
    cpp.defines: base.concat(["CXX_NO_FLATBUFFERS", 'CXX_VERSION="0.0.0"'])
    cpp.includePaths: base.concat([
        product.sourceDirectory,
        product.sourceDirectory + "/../utfcpp",
    ])

    Group {
        name: "Sources"
        files: [
            "cxx/access_control.cc",
            "cxx/access_control.h",
            "cxx/arena.h",
            "cxx/ast.cc",
            "cxx/ast.h",
            "cxx/ast_cursor.cc",
            "cxx/ast_cursor.h",
            "cxx/ast_fwd.h",
            "cxx/ast_interpreter.cc",
            "cxx/ast_interpreter.h",
            "cxx/ast_interpreter_builtins.cc",
            "cxx/ast_interpreter_declarations.cc",
            "cxx/ast_interpreter_declarators.cc",
            "cxx/ast_interpreter_expressions.cc",
            "cxx/ast_interpreter_names.cc",
            "cxx/ast_interpreter_specifiers.cc",
            "cxx/ast_interpreter_statements.cc",
            "cxx/ast_interpreter_units.cc",
            "cxx/ast_kind.h",
            "cxx/ast_pretty_printer.cc",
            "cxx/ast_pretty_printer.h",
            "cxx/ast_printer.cc",
            "cxx/ast_printer.h",
            "cxx/ast_rewriter.cc",
            "cxx/ast_rewriter.h",
            "cxx/ast_rewriter_declarations.cc",
            "cxx/ast_rewriter_declarators.cc",
            "cxx/ast_rewriter_expressions.cc",
            "cxx/ast_rewriter_instantiate.cc",
            "cxx/ast_rewriter_names.cc",
            "cxx/ast_rewriter_packs.cc",
            "cxx/ast_rewriter_partial_specialization.cc",
            "cxx/ast_rewriter_pending_body.cc",
            "cxx/ast_rewriter_requires.cc",
            "cxx/ast_rewriter_specifiers.cc",
            "cxx/ast_rewriter_statements.cc",
            "cxx/ast_rewriter_subsumption.cc",
            "cxx/ast_rewriter_units.cc",
            "cxx/ast_slot.cc",
            "cxx/ast_slot.h",
            "cxx/ast_validator.cc",
            "cxx/ast_validator.h",
            "cxx/ast_visitor.cc",
            "cxx/ast_visitor.h",
            "cxx/base_classes.cc",
            "cxx/bind_class.cc",
            "cxx/binder.cc",
            "cxx/binder.h",
            "cxx/binder_complete_class.cc",
            "cxx/binder_declare_function.cc",
            "cxx/binder_range_for.cc",
            "cxx/binder_resolve_id.cc",
            "cxx/binder_structured_bindings.cc",
            "cxx/class_template_deduction.cc",
            "cxx/class_template_deduction.h",
            "cxx/class_value_abi.cc",
            "cxx/class_value_abi.h",
            "cxx/cli.cc",
            "cxx/cli.h",
            "cxx/const_value.cc",
            "cxx/const_value.h",
            "cxx/control.cc",
            "cxx/control.h",
            "cxx/cxx.cc",
            "cxx/cxx_fwd.h",
            "cxx/decl.cc",
            "cxx/decl.h",
            "cxx/decl_specs.cc",
            "cxx/decl_specs.h",
            "cxx/dependent_types.cc",
            "cxx/dependent_types.h",
            "cxx/diagnostic.cc",
            "cxx/diagnostic.h",
            "cxx/diagnostics_client.cc",
            "cxx/diagnostics_client.h",
            "cxx/external_name_encoder.cc",
            "cxx/external_name_encoder.h",
            "cxx/gcc_linux_toolchain.cc",
            "cxx/gcc_linux_toolchain.h",
            "cxx/implicit_conversion_sequence.cc",
            "cxx/implicit_conversion_sequence.h",
            "cxx/initialization.cc",
            "cxx/initialization.h",
            "cxx/lambda_captures.h",
            "cxx/lexer.cc",
            "cxx/lexer.h",
            "cxx/literals.cc",
            "cxx/literals.h",
            "cxx/literals_fwd.h",
            "cxx/macos_toolchain.cc",
            "cxx/macos_toolchain.h",
            "cxx/memory_layout.cc",
            "cxx/memory_layout.h",
            "cxx/name_lookup.cc",
            "cxx/name_lookup.h",
            "cxx/name_printer.cc",
            "cxx/names.cc",
            "cxx/names.h",
            "cxx/names_fwd.h",
            "cxx/overload_resolution.cc",
            "cxx/overload_resolution.h",
            "cxx/parser.cc",
            "cxx/parser.h",
            "cxx/parser_fwd.h",
            "cxx/parser_lookup.h",
            "cxx/path.cc",
            "cxx/preprocessor.cc",
            "cxx/preprocessor.h",
            "cxx/preprocessor_fwd.h",
            "cxx/scope.cc",
            "cxx/scope.h",
            "cxx/source_location.cc",
            "cxx/source_location.h",
            "cxx/standard_conversion.cc",
            "cxx/standard_conversion.h",
            "cxx/substitution.cc",
            "cxx/substitution.h",
            "cxx/symbol_chain_view.cc",
            "cxx/symbol_printer.cc",
            "cxx/symbols.cc",
            "cxx/symbols.h",
            "cxx/symbols_fwd.h",
            "cxx/template_argument_deduction.cc",
            "cxx/template_argument_deduction.h",
            "cxx/template_equivalence.cc",
            "cxx/template_equivalence.h",
            "cxx/token.cc",
            "cxx/token.h",
            "cxx/token_fwd.h",
            "cxx/toolchain.cc",
            "cxx/toolchain.h",
            "cxx/toolchain_config.cc",
            "cxx/toolchain_config.h",
            "cxx/translation_unit.cc",
            "cxx/translation_unit.h",
            "cxx/type_checker.cc",
            "cxx/type_checker.h",
            "cxx/type_checker_initializer.cc",
            "cxx/type_printer.cc",
            "cxx/type_traits.cc",
            "cxx/type_traits.h",
            "cxx/types.cc",
            "cxx/types.h",
            "cxx/types_fwd.h",
            "cxx/util.cc",
            "cxx/util.h",
            "cxx/wasm32_wasi_toolchain.cc",
            "cxx/wasm32_wasi_toolchain.h",
            "cxx/windows_toolchain.cc",
            "cxx/windows_toolchain.h",
        ]
    }

    Group {
        name: "Private headers"
        files: [
            "cxx/private/builtin_function_keywords-priv.h",
            "cxx/private/builtins-priv.h",
            "cxx/private/builtins_interpreter-priv.h",
            "cxx/private/builtins_typechecker-priv.h",
            "cxx/private/c_keywords-priv.h",
            "cxx/private/keywords-priv.h",
            "cxx/private/path.h",
            "cxx/private/pp_directives-priv.h",
        ]
    }

    Group {
        name: "Views"
        files: [
            "cxx/views/base_classes.h",
            "cxx/views/symbol_chain.h",
            "cxx/views/symbols.h",
        ]
    }
    Export {
        Depends { name: "cpp" }
        cpp.cxxLanguageVersion: "c++23"
        cpp.defines: ['CXX_VERSION="0.0.0"']
        cpp.includePaths: [
            project.ide_source_tree + "/src/libs/3rdparty/cxx-frontend",
            project.ide_source_tree + "/src/libs/3rdparty/utfcpp",
        ]
    }
}
