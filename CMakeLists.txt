cmake_minimum_required(VERSION 3.20)
project(codegraph-cpp VERSION 0.1.0 LANGUAGES C CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0600 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}     /utf-8")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()

# ── tree-sitter from npm (matching version for grammars) ──
set(TS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/node_modules/tree-sitter/vendor/tree-sitter)

add_library(tree_sitter STATIC
    ${TS_DIR}/lib/src/parser.c
    ${TS_DIR}/lib/src/lexer.c
    ${TS_DIR}/lib/src/subtree.c
    ${TS_DIR}/lib/src/stack.c
    ${TS_DIR}/lib/src/node.c
    ${TS_DIR}/lib/src/tree.c
    ${TS_DIR}/lib/src/tree_cursor.c
    ${TS_DIR}/lib/src/query.c
    ${TS_DIR}/lib/src/language.c
    ${TS_DIR}/lib/src/alloc.c
    ${TS_DIR}/lib/src/get_changed_ranges.c
    ${TS_DIR}/lib/src/wasm_store.c
)
target_include_directories(tree_sitter PUBLIC ${TS_DIR}/lib/include ${TS_DIR}/lib/src)

# ── tree-sitter grammars (from npm install) ──
set(GRAMMAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/node_modules)

add_library(ts_cpp STATIC
    ${GRAMMAR_DIR}/tree-sitter-cpp/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-cpp/src/scanner.c
)
target_include_directories(ts_cpp PUBLIC ${GRAMMAR_DIR}/tree-sitter-cpp/src ${TS_DIR}/lib/include)
target_link_libraries(ts_cpp PUBLIC tree_sitter)

add_library(ts_python STATIC
    ${GRAMMAR_DIR}/tree-sitter-python/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-python/src/scanner.c
)
target_include_directories(ts_python PUBLIC ${GRAMMAR_DIR}/tree-sitter-python/src ${TS_DIR}/lib/include)
target_link_libraries(ts_python PUBLIC tree_sitter)

add_library(ts_javascript STATIC
    ${GRAMMAR_DIR}/tree-sitter-javascript/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-javascript/src/scanner.c
)
target_include_directories(ts_javascript PUBLIC ${GRAMMAR_DIR}/tree-sitter-javascript/src ${TS_DIR}/lib/include)
target_link_libraries(ts_javascript PUBLIC tree_sitter)

add_library(ts_dart STATIC
    ${GRAMMAR_DIR}/@ast-grep/lang-dart/src/parser.c
    ${GRAMMAR_DIR}/@ast-grep/lang-dart/src/scanner.c
)
target_include_directories(ts_dart PUBLIC ${GRAMMAR_DIR}/@ast-grep/lang-dart/src ${TS_DIR}/lib/include)
target_link_libraries(ts_dart PUBLIC tree_sitter)

add_library(ts_typescript STATIC
    ${GRAMMAR_DIR}/tree-sitter-typescript/typescript/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-typescript/typescript/src/scanner.c
)
target_include_directories(ts_typescript PUBLIC ${GRAMMAR_DIR}/tree-sitter-typescript/typescript/src ${TS_DIR}/lib/include)
target_link_libraries(ts_typescript PUBLIC tree_sitter)

add_library(ts_tsx STATIC
    ${GRAMMAR_DIR}/tree-sitter-typescript/tsx/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-typescript/tsx/src/scanner.c
)
target_include_directories(ts_tsx PUBLIC ${GRAMMAR_DIR}/tree-sitter-typescript/tsx/src ${TS_DIR}/lib/include)
target_link_libraries(ts_tsx PUBLIC tree_sitter)

add_library(ts_markdown STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown/src/scanner.c
)
target_include_directories(ts_markdown PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown/src ${TS_DIR}/lib/include)
target_link_libraries(ts_markdown PUBLIC tree_sitter)

add_library(ts_markdown_inline STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.c
)
target_include_directories(ts_markdown_inline PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-markdown/tree-sitter-markdown-inline/src ${TS_DIR}/lib/include)
target_link_libraries(ts_markdown_inline PUBLIC tree_sitter)

add_library(ts_rust STATIC
    ${GRAMMAR_DIR}/tree-sitter-rust/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-rust/src/scanner.c
)
target_include_directories(ts_rust PUBLIC ${GRAMMAR_DIR}/tree-sitter-rust/src ${TS_DIR}/lib/include)
target_link_libraries(ts_rust PUBLIC tree_sitter)

add_library(ts_bash STATIC
    ${GRAMMAR_DIR}/tree-sitter-bash/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-bash/src/scanner.c
)
target_include_directories(ts_bash PUBLIC ${GRAMMAR_DIR}/tree-sitter-bash/src ${TS_DIR}/lib/include)
target_link_libraries(ts_bash PUBLIC tree_sitter)

add_library(ts_go STATIC
    ${GRAMMAR_DIR}/tree-sitter-go/src/parser.c
)
target_include_directories(ts_go PUBLIC ${GRAMMAR_DIR}/tree-sitter-go/src ${TS_DIR}/lib/include)
target_link_libraries(ts_go PUBLIC tree_sitter)

add_library(ts_java STATIC
    ${GRAMMAR_DIR}/tree-sitter-java/src/parser.c
)
target_include_directories(ts_java PUBLIC ${GRAMMAR_DIR}/tree-sitter-java/src ${TS_DIR}/lib/include)
target_link_libraries(ts_java PUBLIC tree_sitter)

add_library(ts_kotlin STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-kotlin/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-kotlin/src/scanner.c
)
target_include_directories(ts_kotlin PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-kotlin/src ${TS_DIR}/lib/include)
target_link_libraries(ts_kotlin PUBLIC tree_sitter)

add_library(ts_php STATIC
    ${GRAMMAR_DIR}/tree-sitter-php/php/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-php/php/src/scanner.c
)
target_include_directories(ts_php PUBLIC ${GRAMMAR_DIR}/tree-sitter-php/php/src ${TS_DIR}/lib/include)
target_link_libraries(ts_php PUBLIC tree_sitter)

add_library(ts_swift STATIC
    ${GRAMMAR_DIR}/@ast-grep/lang-swift/src/parser.c
    ${GRAMMAR_DIR}/@ast-grep/lang-swift/src/scanner.c
)
target_include_directories(ts_swift PUBLIC ${GRAMMAR_DIR}/@ast-grep/lang-swift/src ${TS_DIR}/lib/include)
target_link_libraries(ts_swift PUBLIC tree_sitter)

add_library(ts_objc STATIC
    ${GRAMMAR_DIR}/tree-sitter-objc/src/parser.c
)
target_include_directories(ts_objc PUBLIC ${GRAMMAR_DIR}/tree-sitter-objc/src ${TS_DIR}/lib/include)
target_link_libraries(ts_objc PUBLIC tree_sitter)

add_library(ts_csharp STATIC
    ${GRAMMAR_DIR}/tree-sitter-c-sharp/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-c-sharp/src/scanner.c
)
target_include_directories(ts_csharp PUBLIC ${GRAMMAR_DIR}/tree-sitter-c-sharp/src ${TS_DIR}/lib/include)
target_link_libraries(ts_csharp PUBLIC tree_sitter)

add_library(ts_sql STATIC
    ${GRAMMAR_DIR}/@derekstride/tree-sitter-sql/src/parser.c
    ${GRAMMAR_DIR}/@derekstride/tree-sitter-sql/src/scanner.c
)
target_include_directories(ts_sql PUBLIC ${GRAMMAR_DIR}/@derekstride/tree-sitter-sql/src ${TS_DIR}/lib/include)
target_link_libraries(ts_sql PUBLIC tree_sitter)

add_library(ts_lua STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-lua/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-lua/src/scanner.c
)
target_include_directories(ts_lua PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-lua/src ${TS_DIR}/lib/include)
target_link_libraries(ts_lua PUBLIC tree_sitter)

add_library(ts_yaml STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src/scanner.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src/schema.json.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src/schema.core.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src/schema.legacy.c
)
target_include_directories(ts_yaml PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-yaml/src ${TS_DIR}/lib/include)
target_link_libraries(ts_yaml PUBLIC tree_sitter)

add_library(ts_json STATIC
    ${GRAMMAR_DIR}/tree-sitter-json/src/parser.c
)
target_include_directories(ts_json PUBLIC ${GRAMMAR_DIR}/tree-sitter-json/src ${TS_DIR}/lib/include)
target_link_libraries(ts_json PUBLIC tree_sitter)

add_library(ts_xml STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-xml/xml/src/parser.c
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-xml/xml/src/scanner.c
)
target_include_directories(ts_xml PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-xml/xml/src ${TS_DIR}/lib/include)
target_link_libraries(ts_xml PUBLIC tree_sitter)

add_library(ts_html STATIC
    ${GRAMMAR_DIR}/tree-sitter-html/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-html/src/scanner.c
)
target_include_directories(ts_html PUBLIC ${GRAMMAR_DIR}/tree-sitter-html/src ${TS_DIR}/lib/include)
target_link_libraries(ts_html PUBLIC tree_sitter)

add_library(ts_css STATIC
    ${GRAMMAR_DIR}/tree-sitter-css/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-css/src/scanner.c
)
target_include_directories(ts_css PUBLIC ${GRAMMAR_DIR}/tree-sitter-css/src ${TS_DIR}/lib/include)
target_link_libraries(ts_css PUBLIC tree_sitter)

add_library(ts_zig STATIC
    ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-zig/src/parser.c
)
target_include_directories(ts_zig PUBLIC ${GRAMMAR_DIR}/@tree-sitter-grammars/tree-sitter-zig/src ${TS_DIR}/lib/include)
target_link_libraries(ts_zig PUBLIC tree_sitter)

add_library(ts_ruby STATIC
    ${GRAMMAR_DIR}/tree-sitter-ruby/src/parser.c
    ${GRAMMAR_DIR}/tree-sitter-ruby/src/scanner.c
)
target_include_directories(ts_ruby PUBLIC ${GRAMMAR_DIR}/tree-sitter-ruby/src ${TS_DIR}/lib/include)
target_link_libraries(ts_ruby PUBLIC tree_sitter)

add_library(ts_glsl STATIC
    ${GRAMMAR_DIR}/tree-sitter-glsl/src/parser.c
)
target_include_directories(ts_glsl PUBLIC ${GRAMMAR_DIR}/tree-sitter-glsl/src ${TS_DIR}/lib/include)
target_link_libraries(ts_glsl PUBLIC tree_sitter)

# ── SQLite3 ──
find_package(SQLite3 REQUIRED)

# ── simdjson ──
find_package(simdjson 4.0 QUIET)
if(NOT TARGET simdjson::simdjson)
    set(SIMDJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/simdjson)
    if(NOT EXISTS ${SIMDJSON_DIR}/simdjson.h)
        file(DOWNLOAD
            https://raw.githubusercontent.com/simdjson/simdjson/v4.6.4/singleheader/simdjson.h
            ${SIMDJSON_DIR}/simdjson.h
            TIMEOUT 60
            STATUS simdjson_h_status
        )
        list(GET simdjson_h_status 0 simdjson_h_code)
        if(NOT simdjson_h_code EQUAL 0)
            message(FATAL_ERROR "Failed to download simdjson.h. Please install simdjson manually or check your network.")
        endif()
    endif()
    if(NOT EXISTS ${SIMDJSON_DIR}/simdjson.cpp)
        file(DOWNLOAD
            https://raw.githubusercontent.com/simdjson/simdjson/v4.6.4/singleheader/simdjson.cpp
            ${SIMDJSON_DIR}/simdjson.cpp
            TIMEOUT 60
            STATUS simdjson_cpp_status
        )
        list(GET simdjson_cpp_status 0 simdjson_cpp_code)
        if(NOT simdjson_cpp_code EQUAL 0)
            message(FATAL_ERROR "Failed to download simdjson.cpp. Please install simdjson manually or check your network.")
        endif()
    endif()
    add_library(simdjson_fallback STATIC ${SIMDJSON_DIR}/simdjson.cpp)
    target_include_directories(simdjson_fallback PUBLIC ${SIMDJSON_DIR})
    add_library(simdjson::simdjson ALIAS simdjson_fallback)
endif()

# ── Main library ──
add_library(codegraph_core STATIC
    src/core/types.cpp
    src/db/database.cpp
    src/extraction/extractor.cpp
    src/extraction/cpp_extractor.cpp
    src/extraction/python_extractor.cpp
    src/extraction/javascript_extractor.cpp
    src/extraction/dart_extractor.cpp
    src/extraction/typescript_extractor.cpp
    src/extraction/markdown_extractor.cpp
    src/extraction/rust_extractor.cpp
    src/extraction/bash_extractor.cpp
    src/extraction/go_extractor.cpp
    src/extraction/java_extractor.cpp
    src/extraction/kotlin_extractor.cpp
    src/extraction/php_extractor.cpp
    src/extraction/swift_extractor.cpp
    src/extraction/objc_extractor.cpp
    src/extraction/csharp_extractor.cpp
    src/extraction/sql_extractor.cpp
    src/extraction/lua_extractor.cpp
    src/extraction/yaml_extractor.cpp
    src/extraction/json_extractor.cpp
    src/extraction/xml_extractor.cpp
    src/extraction/html_extractor.cpp
    src/extraction/css_extractor.cpp
    src/extraction/zig_extractor.cpp
    src/extraction/ruby_extractor.cpp
    src/extraction/glsl_extractor.cpp
    src/extraction/extractor_factory.cpp
    src/graph/traverser.cpp
    src/search/fts_search.cpp
    src/context/context_builder.cpp
    src/sync/file_watcher.cpp
    src/mcp/mcp_server.cpp
    src/diff/diff_parser.cpp
)

target_include_directories(codegraph_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TS_DIR}/lib/include
)

target_link_libraries(codegraph_core PUBLIC
    SQLite::SQLite3
    simdjson::simdjson
    tree_sitter
    ts_cpp
    ts_python
    ts_javascript
    ts_dart
    ts_typescript
    ts_tsx
    ts_markdown
    ts_markdown_inline
    ts_rust
    ts_bash
    ts_go
    ts_java
    ts_kotlin
    ts_php
    ts_swift
    ts_objc
    ts_csharp
    ts_sql
    ts_lua
    ts_yaml
    ts_json
    ts_xml
    ts_html
    ts_css
    ts_zig
    ts_ruby
    ts_glsl
)

# Android bionic 将 pthread 并入 libc, 无独立 libpthread 库, 不链接
if(NOT WIN32 AND NOT ANDROID)
    target_link_libraries(codegraph_core PUBLIC pthread)
endif()

# ── CLI executable ──
add_executable(codegraph src/main.cpp)
target_link_libraries(codegraph PRIVATE codegraph_core)
target_compile_definitions(codegraph PRIVATE CMAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ── Tests ──
enable_testing()
add_executable(test_basic tests/test_basic.cpp)
target_link_libraries(test_basic PRIVATE codegraph_core)
add_test(NAME basic COMMAND test_basic)

install(TARGETS codegraph_core  DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS tree_sitter     DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_cpp          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_python       DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_javascript   DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_dart         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_typescript   DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_tsx          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_markdown     DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_markdown_inline   DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_rust         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_bash         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_go           DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_java         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_kotlin       DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_php          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_swift        DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_objc         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_csharp       DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_sql          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_lua          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_yaml         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_json         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_xml          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_html         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_css          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_zig          DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_ruby         DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS ts_glsl         DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(TARGETS codegraph_core
    EXPORT codegraphTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/codegraph"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY "${TS_DIR}/lib/include/tree_sitter"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)