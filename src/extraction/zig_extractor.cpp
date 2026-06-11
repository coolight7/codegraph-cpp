/**
 * zig_extractor.cpp — Zig 语言提取器实现
 *
 * 使用 tree-sitter 解析 Zig 源代码，提取：
 *   - 函数定义（function_declaration → name 字段）
 *   - 容器定义（variable_declaration → struct/enum/union_declaration）→ Class
 *   - 导入声明（variable_declaration → builtin_function @import）→ Import
 *   - 函数调用关系
 *
 * tree-sitter 的 Zig 语言描述符通过 tree_sitter_zig() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_zig();

namespace codegraph {

ZigExtractor::ZigExtractor() : lang_(tree_sitter_zig()) {}
ZigExtractor::~ZigExtractor() = default;

static std::string get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static bool is_container_decl(const char *type_name) {
  return strcmp(type_name, "struct_declaration") == 0 ||
         strcmp(type_name, "enum_declaration") == 0 ||
         strcmp(type_name, "union_declaration") == 0;
}

static bool is_import_builtin(TSNode node, const std::string &source) {
  if (strcmp(ts_node_type(node), "builtin_function") != 0)
    return false;
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    if (strcmp(ts_node_type(child), "builtin_identifier") == 0) {
      std::string ident = get_node_text(child, source);
      return ident == "import" || ident == "cImport";
    }
  }
  return false;
}

static bool has_container_in_expr(TSNode node) {
  if (is_container_decl(ts_node_type(node)))
    return true;
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    if (has_container_in_expr(ts_node_child(node, i)))
      return true;
  }
  return false;
}

static bool has_import_in_expr(TSNode node, const std::string &source) {
  if (is_import_builtin(node, source))
    return true;
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    if (has_import_in_expr(ts_node_child(node, i), source))
      return true;
  }
  return false;
}

static std::string get_identifier_child(TSNode node,
                                        const std::string &source) {
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    if (strcmp(ts_node_type(child), "identifier") == 0) {
      return get_node_text(child, source);
    }
  }
  return "";
}

void ZigExtractor::walk_tree(TSNode node, const std::string &source,
                             const std::string &file_path, int64_t parent_id,
                             ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  int64_t my_id = parent_id;

  if (strcmp(type_name, "function_declaration") == 0) {
    Node n;
    n.kind = NodeKind::Function;
    n.file_path = file_path;
    n.language = "zig";

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
      n.name = get_node_text(name_node, source);
    } else {
      n.name = "fn";
    }

    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    n.line = start.row + 1;
    n.col = start.column + 1;
    n.end_line = end.row + 1;
    n.end_col = end.column + 1;

    n.qualified_name = n.name;
    n.id = 0;
    result.nodes.push_back(n);
    my_id = -(int64_t)result.nodes.size();
  }

  if (strcmp(type_name, "variable_declaration") == 0) {
    std::string var_name = get_identifier_child(node, source);
    bool handled = false;

    bool is_container = false;
    bool is_import = false;

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
      TSNode child = ts_node_child(node, i);
      if (strcmp(ts_node_type(child), "expression") == 0) {
        is_container = has_container_in_expr(child);
        is_import = has_import_in_expr(child, source);
        break;
      }
    }

    if (is_container && !var_name.empty()) {
      Node n;
      n.kind = NodeKind::Class;
      n.file_path = file_path;
      n.language = "zig";
      n.name = var_name;
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";

      TSPoint start = ts_node_start_point(node);
      TSPoint end = ts_node_end_point(node);
      n.line = start.row + 1;
      n.col = start.column + 1;
      n.end_line = end.row + 1;
      n.end_col = end.column + 1;

      n.qualified_name = n.name;
      n.id = 0;
      result.nodes.push_back(n);
      handled = true;
    }

    if (is_import && !var_name.empty()) {
      Node n;
      n.kind = NodeKind::Import;
      n.file_path = file_path;
      n.language = "zig";
      n.name = var_name;
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";

      TSPoint start = ts_node_start_point(node);
      TSPoint end = ts_node_end_point(node);
      n.line = start.row + 1;
      n.col = start.column + 1;
      n.end_line = end.row + 1;
      n.end_col = end.column + 1;

      n.qualified_name = n.name;
      n.id = 0;
      result.nodes.push_back(n);
      handled = true;
    }

    if (!handled && !var_name.empty()) {
      Node n;
      n.kind = NodeKind::Variable;
      n.file_path = file_path;
      n.language = "zig";
      n.name = var_name;

      TSPoint start = ts_node_start_point(node);
      TSPoint end = ts_node_end_point(node);
      n.line = start.row + 1;
      n.col = start.column + 1;
      n.end_line = end.row + 1;
      n.end_col = end.column + 1;

      n.qualified_name = n.name;
      n.id = 0;
      result.nodes.push_back(n);
    }
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }

  if (strcmp(type_name, "function_declaration") == 0 && my_id != parent_id) {
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
      uint32_t cnt = ts_node_child_count(n);
      for (uint32_t j = 0; j < cnt; j++) {
        find_calls(ts_node_child(n, j));
      }
    };
    find_calls(node);
  }
}

ExtractionResult ZigExtractor::extract(const std::string &file_path,
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