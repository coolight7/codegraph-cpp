/**
 * python_extractor.cpp — Python 语言提取器实现
 *
 * 使用 tree-sitter 解析 Python 源代码，提取：
 *   - 函数定义（def）
 *   - 类定义（class）
 *   - import 语句
 *   - 函数调用关系
 *
 * 比 C++ 提取器简单（没有作用域追踪、没有签名提取）。
 */

#include "codegraph/extraction/extractor.h"
#include <cstring>
#include <functional>
#include <tree_sitter/api.h>

extern "C" TSLanguage *tree_sitter_python();

namespace codegraph {

/**
 * Python 提取器的构造函数。
 * tree_sitter_python() 返回 tree-sitter-python 库的语言描述符。
 */
PythonExtractor::PythonExtractor() : lang_(tree_sitter_python()) {}
PythonExtractor::~PythonExtractor() = default;

/**
 * Python 版本的 get_node_text（与 C++ 版本逻辑相同）。
 */
std::string PythonExtractor::get_node_text(TSNode node,
                                           const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (start >= source.size())
    return "";
  return source.substr(start, std::min(end, (uint32_t)source.size()) - start);
}

/**
 * Python 节点类型分类。
 * 比 C++ 简单得多，因为 Python 语法更简洁：
 *   - function_definition → Function
 *   - class_definition    → Class
 *   - import_statement    → Import
 *   - assignment          → Variable
 */
static NodeKind classify_python_node(const char *type_name) {
  if (strcmp(type_name, "function_definition") == 0)
    return NodeKind::Function;
  if (strcmp(type_name, "class_definition") == 0)
    return NodeKind::Class;
  if (strcmp(type_name, "import_statement") == 0)
    return NodeKind::Import;
  if (strcmp(type_name, "import_from_statement") == 0)
    return NodeKind::Import;
  if (strcmp(type_name, "assignment") == 0)
    return NodeKind::Variable;
  return NodeKind::Variable;
}

/**
 * Python AST 遍历，提取节点和调用关系。
 *
 * 与 C++ 版本的区别：
 *   - 没有作用域追踪（Python 用模块级导入，不需要限定名）
 *   - 装饰器检测：有装饰器的函数/类标记为 exported
 *   - 调用提取：Python 的 call 节点直接用 "function" 字段
 *   - 属性访问处理：obj.method → 提取 "method"（去掉 "obj."）
 */
void PythonExtractor::walk_tree(TSNode node, const std::string &source,
                                const std::string &file_path, int64_t parent_id,
                                ExtractionResult &result) {
  const char *type_name = ts_node_type(node);
  NodeKind kind = classify_python_node(type_name);
  bool is_interesting = (kind == NodeKind::Function ||
                         kind == NodeKind::Class || kind == NodeKind::Import);

  int64_t my_id = parent_id;

  if (is_interesting) {
    Node n;
    n.kind = kind;
    n.file_path = file_path;
    n.language = "python";

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
        if (strcmp(ts_node_type(child), "decorator") == 0 ||
            strcmp(ts_node_type(child), "decorator_list") == 0) {
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
      if (strcmp(t, "call") == 0) {
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

  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    walk_tree(child, source, file_path, my_id, result);
  }
}

/**
 * Python 提取器的主入口（与 C++ 版本逻辑相同）。
 */
ExtractionResult PythonExtractor::extract(const std::string &file_path,
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