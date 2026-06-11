/**
 * objc_extractor.cpp — Objective-C 语言提取器实现
 *
 * 使用 tree-sitter 解析 Objective-C 源代码，提取：
 *   - 类接口/实现（@interface / @implementation）
 *   - 协议定义（@protocol）
 *   - 方法定义（- / + method）
 *   - 方法调用关系（[obj method]）
 *
 * tree-sitter 的 Objective-C 语言描述符通过 tree_sitter_objc() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_objc();

namespace codegraph {

ObjcExtractor::ObjcExtractor() : lang_(tree_sitter_objc()) {}
ObjcExtractor::~ObjcExtractor() = default;

std::string ObjcExtractor::get_node_text(TSNode node,
                                         const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static NodeKind classify_objc_node(const char *type_name) {
  if (strcmp(type_name, "class_interface") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "class_implementation") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "protocol_declaration") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "method_definition") == 0)
    return NodeKind::Method;
  if (strcmp(type_name, "method_declaration") == 0)
    return NodeKind::Method;
  if (strcmp(type_name, "preproc_include") == 0)
    return NodeKind::Import;
  return NodeKind::Variable;
}

static std::string find_objc_name(TSNode node, const std::string &source) {
  TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
  if (!ts_node_is_null(name_node)) {
    uint32_t start = ts_node_start_byte(name_node);
    uint32_t end = ts_node_end_byte(name_node);
    if (start >= source.size())
      return "";
    return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
  }

  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *ct = ts_node_type(child);

    if (strcmp(ct, "identifier") == 0) {
      uint32_t start = ts_node_start_byte(child);
      uint32_t end = ts_node_end_byte(child);
      if (start >= source.size())
        continue;
      return source.substr(start,
                           std::min(end, (uint32_t)source.size()) - start);
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    std::string name = find_objc_name(child, source);
    if (!name.empty())
      return name;
  }

  return "";
}

static std::string get_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static std::string find_objc_method_name(TSNode node,
                                         const std::string &source) {
  std::string out;
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *ct = ts_node_type(child);
    if (strcmp(ct, "identifier") == 0) {
      std::string ident = get_text(child, source);
      if (!ident.empty()) {
        out += ident;
        if (i + 1 < count) {
          TSNode next = ts_node_child(node, i + 1);
          const char *nt = ts_node_type(next);
          if (strcmp(nt, "method_parameter") == 0) {
            out += ":";
          }
        }
      }
    }
  }
  return out;
}

void ObjcExtractor::walk_tree(TSNode node, const std::string &source,
                              const std::string &file_path, int64_t parent_id,
                              ExtractionResult &result) {
  const char *type_name = ts_node_type(node);
  NodeKind kind = classify_objc_node(type_name);
  bool is_interesting = (kind == NodeKind::Class || kind == NodeKind::Method ||
                         kind == NodeKind::Import);

  int64_t my_id = parent_id;

  if (is_interesting) {
    Node n;
    n.kind = kind;
    n.file_path = file_path;
    n.language = "objc";

    if (kind == NodeKind::Method) {
      n.name = find_objc_method_name(node, source);
      if (n.name.empty())
        n.name = find_objc_name(node, source);
    } else {
      n.name = find_objc_name(node, source);
    }
    if (n.name.empty()) {
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

  if (kind == NodeKind::Method && my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "message_expression") == 0) {
        uint32_t cnt = ts_node_child_count(n);
        bool saw_receiver = false;
        std::string callee;
        for (uint32_t j = 0; j < cnt; j++) {
          TSNode child = ts_node_child(n, j);
          const char *ct = ts_node_type(child);
          if (strcmp(ct, "identifier") == 0) {
            if (!saw_receiver) {
              saw_receiver = true;
              continue;
            }
            callee += get_node_text(child, source);
          }
          if (strcmp(ct, ":") == 0 && saw_receiver) {
            callee += ":";
          }
        }
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
      uint32_t cc = ts_node_child_count(n);
      for (uint32_t j = 0; j < cc; j++) {
        find_calls(ts_node_child(n, j));
      }
    };
    find_calls(node);
  }
}

ExtractionResult ObjcExtractor::extract(const std::string &file_path,
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