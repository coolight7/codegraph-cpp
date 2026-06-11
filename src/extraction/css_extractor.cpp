/**
 * css_extractor.cpp — CSS 语言提取器实现
 *
 * 使用 tree-sitter 解析 CSS 样式表，提取：
 *   - 规则集选择器（rule_set → selectors）→ Class 类型节点
 *   - @ 规则语句（media_statement、keyframes_statement、at_rule 等）→ Variable
 * 类型节点
 *
 * tree-sitter 的 CSS 语言描述符通过 tree_sitter_css() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_css();

namespace codegraph {

CssExtractor::CssExtractor() : lang_(tree_sitter_css()) {}
CssExtractor::~CssExtractor() = default;

static std::string get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static const char *at_rule_name(const char *type_name) {
  if (strcmp(type_name, "media_statement") == 0)
    return "@media";
  if (strcmp(type_name, "keyframes_statement") == 0)
    return "@keyframes";
  if (strcmp(type_name, "import_statement") == 0)
    return "@import";
  if (strcmp(type_name, "supports_statement") == 0)
    return "@supports";
  if (strcmp(type_name, "charset_statement") == 0)
    return "@charset";
  if (strcmp(type_name, "namespace_statement") == 0)
    return "@namespace";
  if (strcmp(type_name, "scope_statement") == 0)
    return "@scope";
  return nullptr;
}

static bool is_at_rule_type(const char *type_name) {
  return at_rule_name(type_name) != nullptr ||
         strcmp(type_name, "at_rule") == 0;
}

void CssExtractor::walk_tree(TSNode node, const std::string &source,
                             const std::string &file_path,
                             ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  if (strcmp(type_name, "rule_set") == 0) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
      TSNode child = ts_node_child(node, i);
      if (strcmp(ts_node_type(child), "selectors") == 0) {
        std::string selector_text = get_node_text(child, source);
        if (!selector_text.empty()) {
          Node n;
          n.kind = NodeKind::Class;
          n.file_path = file_path;
          n.language = "css";
          n.name = selector_text;

          TSPoint start = ts_node_start_point(node);
          TSPoint end = ts_node_end_point(node);
          n.line = start.row + 1;
          n.col = start.column + 1;
          n.end_line = end.row + 1;
          n.end_col = end.column + 1;

          n.qualified_name = selector_text;
          n.id = 0;
          result.nodes.push_back(n);
        }
        break;
      }
    }
  }

  if (is_at_rule_type(type_name)) {
    std::string rule_name;

    const char *known_name = at_rule_name(type_name);
    if (known_name) {
      rule_name = known_name;
    } else {
      uint32_t count = ts_node_child_count(node);
      for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "at_keyword") == 0) {
          rule_name = get_node_text(child, source);
          break;
        }
      }
    }

    if (!rule_name.empty()) {
      Node n;
      n.kind = NodeKind::Variable;
      n.file_path = file_path;
      n.language = "css";
      n.name = rule_name;

      TSPoint start = ts_node_start_point(node);
      TSPoint end = ts_node_end_point(node);
      n.line = start.row + 1;
      n.col = start.column + 1;
      n.end_line = end.row + 1;
      n.end_col = end.column + 1;

      n.qualified_name = rule_name;
      n.id = 0;
      result.nodes.push_back(n);
    }
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, result);
  }
}

ExtractionResult CssExtractor::extract(const std::string &file_path,
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
    walk_tree(root, source, file_path, result);
  }

  ts_tree_delete(tree);
  ts_parser_delete(parser);

  return result;
}

} // namespace codegraph