/**
 * html_extractor.cpp — HTML 语言提取器实现
 *
 * 使用 tree-sitter 解析 HTML，提取：
 *   - 开始标签名称（start_tag 的 tag_name 子节点）
 *   - 自闭合标签名称（self_closing_tag 的 tag_name）
 *
 * tree-sitter 的 HTML 语言描述符通过 tree_sitter_html() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_html();

namespace codegraph {

HtmlExtractor::HtmlExtractor() : lang_(tree_sitter_html()) {}
HtmlExtractor::~HtmlExtractor() = default;

static std::string get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

void HtmlExtractor::walk_tree(TSNode node, const std::string &source,
                              const std::string &file_path,
                              ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  if (strcmp(type_name, "start_tag") == 0 ||
      strcmp(type_name, "self_closing_tag") == 0) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
      TSNode child = ts_node_child(node, i);
      if (strcmp(ts_node_type(child), "tag_name") == 0) {
        std::string tag_name = get_node_text(child, source);
        if (!tag_name.empty()) {
          Node n;
          n.kind = NodeKind::Class;
          n.file_path = file_path;
          n.language = "html";
          n.name = tag_name;

          TSPoint start = ts_node_start_point(node);
          TSPoint end = ts_node_end_point(node);
          n.line = start.row + 1;
          n.col = start.column + 1;
          n.end_line = end.row + 1;
          n.end_col = end.column + 1;

          n.qualified_name = tag_name;
          n.id = 0;
          result.nodes.push_back(n);
        }
        break;
      }
    }
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, result);
  }
}

ExtractionResult HtmlExtractor::extract(const std::string &file_path,
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