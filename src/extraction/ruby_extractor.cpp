/**
 * ruby_extractor.cpp — Ruby 语言提取器实现
 *
 * 使用 tree-sitter 解析 Ruby 源代码，提取：
 *   - 方法定义（def name / def self.name）
 *   - 类定义（class Name）
 *   - 模块定义（module Name）
 *   - 单例类定义（class << obj）
 *   - require/load/include 导入语句
 *   - 变量赋值（assignment）
 *   - 方法调用关系
 *
 * tree-sitter 的 Ruby 语言描述符通过 tree_sitter_ruby() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_ruby();

namespace codegraph {

RubyExtractor::RubyExtractor() : lang_(tree_sitter_ruby()) {}
RubyExtractor::~RubyExtractor() = default;

static std::string get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static bool is_require_call(TSNode node, const std::string &source) {
  if (strcmp(ts_node_type(node), "call") != 0)
    return false;
  TSNode method = ts_node_child_by_field_name(node, "method", 6);
  if (ts_node_is_null(method))
    return false;
  std::string text = get_node_text(method, source);
  return text == "require" || text == "require_relative" || text == "load" ||
         text == "include" || text == "extend" || text == "prepend" ||
         text == "autoload";
}

void RubyExtractor::walk_tree(TSNode node, const std::string &source,
                              const std::string &file_path, int64_t parent_id,
                              ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  int64_t my_id = parent_id;

  bool is_class =
      (strcmp(type_name, "class") == 0 || strcmp(type_name, "module") == 0);
  bool is_singleton_class = (strcmp(type_name, "singleton_class") == 0);
  bool is_method = (strcmp(type_name, "method") == 0 ||
                    strcmp(type_name, "singleton_method") == 0);
  bool is_assign = (strcmp(type_name, "assignment") == 0);

  if (is_class) {
    Node n;
    n.kind = NodeKind::Class;
    n.file_path = file_path;
    n.language = "ruby";

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

    n.qualified_name = n.name;
    n.id = 0;
    result.nodes.push_back(n);
    my_id = -(int64_t)result.nodes.size();
  }

  if (is_singleton_class) {
    Node n;
    n.kind = NodeKind::Class;
    n.file_path = file_path;
    n.language = "ruby";

    TSNode value_node = ts_node_child_by_field_name(node, "value", 5);
    if (!ts_node_is_null(value_node)) {
      n.name = "singleton:" + get_node_text(value_node, source);
    } else {
      n.name = "singleton_class";
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

  if (is_method) {
    Node n;
    n.kind = NodeKind::Function;
    n.file_path = file_path;
    n.language = "ruby";

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
      n.name = get_node_text(name_node, source);
    } else {
      n.name = get_node_text(node, source);
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";
    }

    if (strcmp(type_name, "singleton_method") == 0) {
      TSNode obj_node = ts_node_child_by_field_name(node, "object", 6);
      if (!ts_node_is_null(obj_node)) {
        std::string obj = get_node_text(obj_node, source);
        n.name = obj + "." + n.name;
      } else {
        n.name = "self." + n.name;
      }
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

  if (is_assign) {
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    if (!ts_node_is_null(left)) {
      Node n;
      n.kind = NodeKind::Variable;
      n.file_path = file_path;
      n.language = "ruby";
      n.name = get_node_text(left, source);

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

  if (is_require_call(node, source) && parent_id == 0) {
    Node n;
    n.kind = NodeKind::Import;
    n.file_path = file_path;
    n.language = "ruby";

    TSNode arguments = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(arguments)) {
      n.name = get_node_text(arguments, source);
    } else {
      n.name = get_node_text(node, source);
    }
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
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }

  if ((is_method || is_class || is_singleton_class) && my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "call") == 0) {
        TSNode method = ts_node_child_by_field_name(n, "method", 6);
        if (!ts_node_is_null(method)) {
          std::string callee = get_node_text(method, source);
          if (!callee.empty() && callee != "require" &&
              callee != "require_relative" && callee != "load" &&
              callee != "include" && callee != "extend" &&
              callee != "prepend" && callee != "autoload") {
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

ExtractionResult RubyExtractor::extract(const std::string &file_path,
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