/**
 * markdown_extractor.cpp — Markdown 语言提取器实现
 *
 * 使用 tree-sitter 解析 Markdown 文档，提取：
 *   - 标题（atx_heading / setext_heading）→ 文档节结构
 *   - 链接引用定义（link_reference_definition）→ 外部资源引用
 *   - 代码块（fenced_code_block）→ 内嵌代码段
 *
 * Markdown 提取器不需要作用域追踪和调用提取，
 * 因为 Markdown 是文档格式而非编程语言。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_markdown();

namespace codegraph {

MarkdownExtractor::MarkdownExtractor() : lang_(tree_sitter_markdown()) {}
MarkdownExtractor::~MarkdownExtractor() = default;

std::string MarkdownExtractor::get_node_text(TSNode node,
                                             const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

void MarkdownExtractor::walk_tree(TSNode node, const std::string &source,
                                  const std::string &file_path,
                                  ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  if (strcmp(type_name, "atx_heading") == 0 ||
      strcmp(type_name, "setext_heading") == 0) {
    Node n;
    n.kind = NodeKind::Function;
    n.file_path = file_path;
    n.language = "markdown";

    std::string heading_text;
    TSNode content_node =
        ts_node_child_by_field_name(node, "heading_content", 15);
    if (!ts_node_is_null(content_node)) {
      heading_text = get_node_text(content_node, source);
    }
    if (heading_text.empty()) {
      heading_text = get_node_text(node, source);
      while (!heading_text.empty() && heading_text[0] == '#')
        heading_text.erase(0, 1);
      while (!heading_text.empty() && heading_text[0] == ' ')
        heading_text.erase(0, 1);
      while (!heading_text.empty() && heading_text.back() == '\n')
        heading_text.pop_back();
    }

    n.name = heading_text.empty() ? "(heading)" : heading_text;
    n.qualified_name = n.name;

    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    n.line = start.row + 1;
    n.col = start.column + 1;
    n.end_line = end.row + 1;
    n.end_col = end.column + 1;

    result.nodes.push_back(n);
  } else if (strcmp(type_name, "link_reference_definition") == 0) {
    Node n;
    n.kind = NodeKind::Import;
    n.file_path = file_path;
    n.language = "markdown";

    std::string label, dest;
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
      TSNode child = ts_node_child(node, i);
      const char *ct = ts_node_type(child);
      if (strcmp(ct, "link_label") == 0) {
        label = get_node_text(child, source);
      } else if (strcmp(ct, "link_destination") == 0) {
        dest = get_node_text(child, source);
      }
    }

    n.name = label.empty() ? (dest.empty() ? "(link)" : dest)
                           : (dest.empty() ? label : label + " -> " + dest);
    n.qualified_name = n.name;

    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    n.line = start.row + 1;
    n.col = start.column + 1;
    n.end_line = end.row + 1;
    n.end_col = end.column + 1;

    result.nodes.push_back(n);
  } else if (strcmp(type_name, "fenced_code_block") == 0) {
    Node n;
    n.kind = NodeKind::Variable;
    n.file_path = file_path;
    n.language = "markdown";

    std::string info, code;
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
      TSNode child = ts_node_child(node, i);
      const char *ct = ts_node_type(child);
      if (strcmp(ct, "info_string") == 0) {
        info = get_node_text(child, source);
      } else if (strcmp(ct, "code_fence_content") == 0) {
        code = get_node_text(child, source);
        if (code.size() > 80)
          code = code.substr(0, 77) + "...";
      }
    }

    n.name = info.empty() ? "(code block)" : info;
    n.signature = code;

    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    n.line = start.row + 1;
    n.col = start.column + 1;
    n.end_line = end.row + 1;
    n.end_col = end.column + 1;

    result.nodes.push_back(n);
  }

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, result);
  }
}

ExtractionResult MarkdownExtractor::extract(const std::string &file_path,
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