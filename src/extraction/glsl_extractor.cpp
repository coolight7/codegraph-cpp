/**
 * glsl_extractor.cpp — GLSL 语言提取器实现
 *
 * 使用 tree-sitter 解析 GLSL(OpenGL Shading Language) 源代码，提取：
 *   - 函数定义（function_definition → declarator 字段获取名称）
 *   - 结构体定义（struct_specifier → name 字段）
 *   - 变量声明（declaration → declarator 字段）
 *   - 预处理包含指令（preproc_include → path 字段）
 *   - 函数调用关系
 *
 * tree-sitter 的 GLSL 语言描述符通过 tree_sitter_glsl() 获取。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_glsl();

namespace codegraph {

GlslExtractor::GlslExtractor() : lang_(tree_sitter_glsl()) {}
GlslExtractor::~GlslExtractor() = default;

static std::string get_node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

static void fill_node_location(Node &n, TSNode node) {
  TSPoint start = ts_node_start_point(node);
  TSPoint end = ts_node_end_point(node);
  n.line = start.row + 1;
  n.col = start.column + 1;
  n.end_line = end.row + 1;
  n.end_col = end.column + 1;
}

static std::string extract_function_name(TSNode func_def,
                                         const std::string &source) {
  TSNode declarator = ts_node_child_by_field_name(func_def, "declarator", 10);
  if (ts_node_is_null(declarator))
    return "fn";

  TSNode name_decl = ts_node_child_by_field_name(declarator, "declarator", 10);
  if (!ts_node_is_null(name_decl)) {
    std::function<std::string(TSNode)> find_id = [&](TSNode n) -> std::string {
      const char *t = ts_node_type(n);
      if (strcmp(t, "identifier") == 0 || strcmp(t, "field_identifier") == 0 ||
          strcmp(t, "type_identifier") == 0) {
        return get_node_text(n, source);
      }
      uint32_t cnt = ts_node_named_child_count(n);
      for (uint32_t i = 0; i < cnt; i++) {
        std::string id = find_id(ts_node_named_child(n, i));
        if (!id.empty())
          return id;
      }
      return "";
    };
    return find_id(name_decl);
  }
  return "fn";
}

static std::string extract_declaration_name(TSNode declaration,
                                            const std::string &source) {
  TSNode declarator =
      ts_node_child_by_field_name(declaration, "declarator", 10);
  if (ts_node_is_null(declarator))
    return "";

  const char *dtype = ts_node_type(declarator);
  if (strcmp(dtype, "identifier") == 0 ||
      strcmp(dtype, "field_identifier") == 0) {
    return get_node_text(declarator, source);
  }

  std::function<std::string(TSNode)> find_id = [&](TSNode n) -> std::string {
    const char *tt = ts_node_type(n);
    if (strcmp(tt, "identifier") == 0 || strcmp(tt, "field_identifier") == 0) {
      return get_node_text(n, source);
    }
    uint32_t cc = ts_node_named_child_count(n);
    for (uint32_t j = 0; j < cc; j++) {
      std::string id = find_id(ts_node_named_child(n, j));
      if (!id.empty())
        return id;
    }
    return "";
  };

  uint32_t cnt = ts_node_named_child_count(declarator);
  for (uint32_t i = 0; i < cnt; i++) {
    TSNode child = ts_node_named_child(declarator, i);
    if (strcmp(ts_node_type(child), "init_declarator") == 0) {
      TSNode decl = ts_node_child_by_field_name(child, "declarator", 10);
      if (!ts_node_is_null(decl)) {
        return find_id(decl);
      }
    }
  }
  return find_id(declarator);
}

void GlslExtractor::walk_tree(TSNode node, const std::string &source,
                              const std::string &file_path, int64_t parent_id,
                              ExtractionResult &result) {
  const char *type_name = ts_node_type(node);

  int64_t my_id = parent_id;

  if (strcmp(type_name, "function_definition") == 0) {
    Node n;
    n.kind = NodeKind::Function;
    n.file_path = file_path;
    n.language = "glsl";

    n.name = extract_function_name(node, source);
    fill_node_location(n, node);
    n.qualified_name = n.name;
    n.id = 0;
    result.nodes.push_back(n);
    my_id = -(int64_t)result.nodes.size();
  }

  if (strcmp(type_name, "struct_specifier") == 0) {
    Node n;
    n.kind = NodeKind::Struct;
    n.file_path = file_path;
    n.language = "glsl";

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
      n.name = get_node_text(name_node, source);
    } else {
      n.name = get_node_text(node, source);
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";
    }
    fill_node_location(n, node);
    n.qualified_name = n.name;
    n.id = 0;
    result.nodes.push_back(n);
    my_id = -(int64_t)result.nodes.size();
  }

  if (strcmp(type_name, "preproc_include") == 0) {
    Node n;
    n.kind = NodeKind::Import;
    n.file_path = file_path;
    n.language = "glsl";

    TSNode path_node = ts_node_child_by_field_name(node, "path", 4);
    if (!ts_node_is_null(path_node)) {
      n.name = get_node_text(path_node, source);
    } else {
      n.name = get_node_text(node, source);
      if (n.name.size() > 80)
        n.name = n.name.substr(0, 77) + "...";
    }
    fill_node_location(n, node);
    n.qualified_name = n.name;
    n.id = 0;
    result.nodes.push_back(n);
  }

  if (strcmp(type_name, "declaration") == 0) {
    bool has_func_decl = false;
    uint32_t cnt = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < cnt; i++) {
      TSNode child = ts_node_named_child(node, i);
      if (strcmp(ts_node_type(child), "function_declarator") == 0) {
        has_func_decl = true;
        break;
      }
    }
    if (!has_func_decl) {
      std::string name = extract_declaration_name(node, source);
      if (!name.empty()) {
        Node n;
        n.kind = NodeKind::Variable;
        n.file_path = file_path;
        n.language = "glsl";
        n.name = name;
        fill_node_location(n, node);
        n.qualified_name = n.name;
        n.id = 0;
        result.nodes.push_back(n);
      }
    }
  }

  uint32_t child_cnt = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_cnt; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }

  if (strcmp(type_name, "function_definition") == 0 && my_id != parent_id) {
    std::function<void(TSNode)> find_calls = [&](TSNode n) {
      const char *t = ts_node_type(n);
      if (strcmp(t, "call_expression") == 0) {
        TSNode func = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(func)) {
          std::string callee = get_node_text(func, source);
          if (!callee.empty()) {
            TSPoint pt = ts_node_start_point(n);
            UnresolvedRef ref;
            ref.source_node_id = my_id;
            ref.ref_name = callee;
            ref.ref_kind = "call";
            ref.line = pt.row + 1;
            ref.col = pt.column + 1;
            result.unresolved.push_back(ref);
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

ExtractionResult GlslExtractor::extract(const std::string &file_path,
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

  walk_tree(root, source, file_path, 0, result);

  ts_tree_delete(tree);
  ts_parser_delete(parser);

  return result;
}

} // namespace codegraph