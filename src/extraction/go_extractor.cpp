/**
 * go_extractor.cpp — Go 语言提取器实现
 *
 * 使用 tree-sitter 解析 Go 源代码，提取：
 *   - 函数定义（func name()）
 *   - 方法定义（func (r Receiver) name()）
 *   - 类型定义（type name struct/interface）
 *   - import 声明
 *   - 函数调用关系
 *
 * tree-sitter 的 Go 语言描述符通过 tree_sitter_go() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_go();

namespace codegraph {

GoExtractor::GoExtractor() : lang_(tree_sitter_go()) {}
GoExtractor::~GoExtractor() = default;

std::string GoExtractor::get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static NodeKind classify_go_node(const char *type_name) {
  if (strcmp(type_name, "function_declaration") == 0)
    return NodeKind::Function;
  if (strcmp(type_name, "method_declaration") == 0)
    return NodeKind::Method;
  if (strcmp(type_name, "type_spec") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "import_declaration") == 0)
    return NodeKind::Import;
  if (strcmp(type_name, "var_declaration") == 0)
    return NodeKind::Variable;
  if (strcmp(type_name, "short_var_declaration") == 0)
    return NodeKind::Variable;
  return NodeKind::Variable;
}

void GoExtractor::walk_tree(TSNode node, const std::string &source,
                            const std::string &file_path, int64_t parent_id,
                            ExtractionResult &result) {
  const char *type_name = ts_node_type(node);
  NodeKind kind = classify_go_node(type_name);
  bool is_interesting =
      (kind == NodeKind::Function || kind == NodeKind::Method ||
       kind == NodeKind::Class || kind == NodeKind::Import);

  int64_t my_id = parent_id;

  if (is_interesting) {
    Node n;
    n.kind = kind;
    n.file_path = file_path;
    n.language = "go";

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

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }

  if ((kind == NodeKind::Function || kind == NodeKind::Method) &&
      my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn)) {
          std::string callee = get_node_text(fn, source);
          auto pos = callee.rfind('.');
          if (pos != std::string::npos)
            callee = callee.substr(pos + 1);

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

ExtractionResult GoExtractor::extract(const std::string &file_path,
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