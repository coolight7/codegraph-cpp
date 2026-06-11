/**
 * rust_extractor.cpp — Rust 语言提取器实现
 *
 * 使用 tree-sitter 解析 Rust 源代码，提取：
 *   - 函数定义（fn）
 *   - 结构体定义（struct）
 *   - 枚举定义（enum）
 *   - trait 定义（trait）
 *   - impl 块（impl）
 *   - 模块定义（mod）
 *   - use 声明（use / extern crate）
 *   - 宏定义（macro_rules!）
 *   - 函数调用关系
 *
 * Rust 提取器与 Python 提取器类似，不需要复杂的作用域追踪。
 * 调用关系通过 call_expression 节点的 "function" 字段提取，
 * 方法调用通过 field_expression 提取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_rust();

namespace codegraph {

RustExtractor::RustExtractor() : lang_(tree_sitter_rust()) {}
RustExtractor::~RustExtractor() = default;

std::string RustExtractor::get_node_text(TSNode node,
                                         const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static NodeKind classify_rust_node(const char *type_name) {
  if (strcmp(type_name, "function_item") == 0)
    return NodeKind::Function;
  if (strcmp(type_name, "struct_item") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "enum_item") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "trait_item") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "impl_item") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "mod_item") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "use_declaration") == 0)
    return NodeKind::Import;
  if (strcmp(type_name, "extern_crate_declaration") == 0)
    return NodeKind::Import;
  if (strcmp(type_name, "macro_definition") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "let_declaration") == 0)
    return NodeKind::Variable;
  return NodeKind::Variable;
}

void RustExtractor::walk_tree(TSNode node, const std::string &source,
                              const std::string &file_path, int64_t parent_id,
                              ExtractionResult &result) {
  const char *type_name = ts_node_type(node);
  NodeKind kind = classify_rust_node(type_name);
  bool is_interesting = (kind == NodeKind::Function ||
                         kind == NodeKind::Class || kind == NodeKind::Import);

  int64_t my_id = parent_id;

  if (is_interesting) {
    Node n;
    n.kind = kind;
    n.file_path = file_path;
    n.language = "rust";

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
      n.name = get_node_text(name_node, source);
    } else {
      n.name = get_node_text(node, source);
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";
    }

    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    n.line = start.row + 1;
    n.col = start.column + 1;
    n.end_line = end.row + 1;
    n.end_col = end.column + 1;

    if (kind == NodeKind::Function || kind == NodeKind::Class) {
      uint32_t child_count = ts_node_child_count(node);
      for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "attribute_item") == 0 ||
            strcmp(ts_node_type(child), "inner_attribute_item") == 0) {
          n.is_exported = true;
          break;
        }
      }
    }

    n.qualified_name = n.name;
    result.nodes.push_back(n);
    my_id = -(int64_t)result.nodes.size();
  }

  if (kind == NodeKind::Function && my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
          std::string callee = get_node_text(fn, source);
          if (!callee.empty()) {
            UnresolvedRef ref;
            ref.source_node_id = my_id;
            ref.ref_name = callee;
            ref.ref_kind = "call";
            TSPoint pt = ts_node_start_point(n);
            ref.line = pt.row + 1;
            ref.col = pt.column + 1;
            result.unresolved.push_back(ref);
          }
        }
      }
      if (strcmp(t, "field_expression") == 0) {
        TSNode field = ts_node_child_by_field_name(n, "field", 5);
        if (!ts_node_is_null(field)) {
          std::string callee = get_node_text(field, source);
          if (!callee.empty()) {
            UnresolvedRef ref;
            ref.source_node_id = my_id;
            ref.ref_name = callee;
            ref.ref_kind = "call";
            TSPoint pt = ts_node_start_point(n);
            ref.line = pt.row + 1;
            ref.col = pt.column + 1;
            result.unresolved.push_back(ref);
          }
        }
      }
      uint32_t cnt = ts_node_child_count(n);
      for (uint32_t j = 0; j < cnt; j++) {
        find_calls(ts_node_child(n, j));
      }
    };
    find_calls(node);
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }
}

ExtractionResult RustExtractor::extract(const std::string &file_path,
                                        const std::string &source) {
  ExtractionResult result;

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, lang_);

  TSTree *tree =
      ts_parser_parse_string(parser, nullptr, source.c_str(), source.size());
  if (!tree) {
    ts_parser_delete(parser);
    return result;
  }

  TSNode root = ts_tree_root_node(tree);

  if (!ts_node_has_error(root)) {
    walk_tree(root, source, file_path, 0, result);
  }

  ts_tree_delete(tree);
  ts_parser_delete(parser);

  return result;
}

} // namespace codegraph