/**
 * sql_extractor.cpp — SQL 语言提取器实现
 *
 * 使用 tree-sitter 解析 SQL 源代码，提取：
 *   - 表定义（CREATE TABLE）
 *   - 视图定义（CREATE VIEW）
 *   - 函数/存储过程定义（CREATE FUNCTION / CREATE PROCEDURE）
 *   - 函数调用关系（invocation）
 *
 * tree-sitter 的 SQL 语言描述符通过 tree_sitter_sql() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_sql();

namespace codegraph {

SqlExtractor::SqlExtractor() : lang_(tree_sitter_sql()) {}
SqlExtractor::~SqlExtractor() = default;

std::string SqlExtractor::get_node_text(TSNode node,
                                        const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static std::string get_text_sql(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static NodeKind classify_sql_node(const char *type_name) {
  if (strcmp(type_name, "create_table") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "create_view") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "create_materialized_view") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "create_function") == 0)
    return NodeKind::Function;
  if (strcmp(type_name, "create_procedure") == 0)
    return NodeKind::Function;
  return NodeKind::Variable;
}

static std::string find_object_name(TSNode node, const std::string &source) {
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *ct = ts_node_type(child);
    if (strcmp(ct, "object_reference") == 0) {
      return get_text_sql(child, source);
    }
  }
  return "";
}

void SqlExtractor::walk_tree(TSNode node, const std::string &source,
                             const std::string &file_path, int64_t parent_id,
                             ExtractionResult &result) {
  const char *type_name = ts_node_type(node);
  NodeKind kind = classify_sql_node(type_name);
  bool is_interesting = (kind == NodeKind::Class || kind == NodeKind::Function);

  int64_t my_id = parent_id;

  if (is_interesting) {
    Node n;
    n.kind = kind;
    n.file_path = file_path;
    n.language = "sql";

    n.name = find_object_name(node, source);

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

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }

  if (kind == NodeKind::Function && my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "invocation") == 0) {
        uint32_t cnt = ts_node_child_count(n);
        for (uint32_t j = 0; j < cnt; j++) {
          TSNode child = ts_node_child(n, j);
          const char *ct = ts_node_type(child);
          if (strcmp(ct, "object_reference") == 0) {
            std::string callee = get_node_text(child, source);
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
      }
      uint32_t cc = ts_node_child_count(n);
      for (uint32_t j = 0; j < cc; j++) {
        find_calls(ts_node_child(n, j));
      }
    };
    find_calls(node);
  }
}

ExtractionResult SqlExtractor::extract(const std::string &file_path,
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