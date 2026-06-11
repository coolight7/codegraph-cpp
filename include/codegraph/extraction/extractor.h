/**
 * extractor.h — 语言提取器接口
 *
 * 定义了代码提取器的抽象接口和具体实现（C++、Python）。
 *
 * 提取器的职责：
 *   1. 将源代码解析为 AST（通过 tree-sitter）
 *   2. 遍历 AST，提取代码符号（Node）和调用关系（UnresolvedRef）
 *   3. 返回 ExtractionResult，供后续批量写入数据库
 *
 * 继承体系：
 *   LanguageExtractor（抽象基类）
 *     ├── CppExtractor   — C/C++ 语言提取器
 *     └── PythonExtractor — Python 语言提取器
 *
 * 工厂函数：
 *   create_extractor("cpp")  → CppExtractor
 *   create_extractor("python") → PythonExtractor
 *   detect_language("foo.cpp") → "cpp"
 *
 * tree-sitter 依赖：
 *   只有本头文件和 cpp_extractor.cpp 依赖 tree-sitter/api.h。
 *   其他模块只使用 ExtractionResult、Node、Edge 等输出类型。
 */

#pragma once

#include "codegraph/core/types.h"
#include <memory>
#include <string>
#include <tree_sitter/api.h>
#include <vector>

namespace codegraph {

/**
 * 提取结果：节点列表 + 边列表 + 未解析引用列表。
 *
 * 节点（Node）：从源代码中提取的符号（函数、类、变量等）
 * 边（Edge）：当前未使用（边在后续解析阶段生成）
 * 未解析引用（UnresolvedRef）：函数调用目标还未解析为正式的边
 */
struct ExtractionResult {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<UnresolvedRef> unresolved;
};

/**
 * 语言提取器的抽象基类。
 *
 * 所有语言提取器都实现这个接口。
 * extract() 方法接收文件路径和源代码，返回 ExtractionResult。
 */
class LanguageExtractor {
public:
  virtual ~LanguageExtractor() = default;

  /**
   * 提取源代码中的符号和调用关系。
   *
   * @param file_path 文件路径（用于记录符号位置）
   * @param source 源代码文本
   * @return 提取结果（节点 + 未解析引用）
   */
  virtual ExtractionResult extract(const std::string &file_path,
                                   const std::string &source) = 0;

  /** 返回语言名（如 "cpp"、"python"）。 */
  virtual const char *language_name() const = 0;
};

/**
 * C++ 语言提取器。
 *
 * 使用 tree-sitter 解析 C/C++ 源代码，提取：
 *   - 函数/方法定义和声明
 *   - 类/结构体/枚举定义
 *   - 命名空间
 *   - #include 导入
 *   - 函数调用关系（作为 UnresolvedRef）
 *
 * tree-sitter 的 C++ 语言描述符通过 tree_sitter_cpp() 获取。
 */
class CppExtractor : public LanguageExtractor {
public:
  CppExtractor();
  ~CppExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "cpp"; }

private:
  TSLanguage *lang_; // tree-sitter 的 C++ 语言描述符

  /** 递归遍历 AST（入口）。 */
  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);

  /** 递归遍历 AST（带作用域追踪）。 */
  void walk_tree_scoped(TSNode node, const std::string &source,
                        const std::string &file_path, int64_t parent_id,
                        const std::string &scope, ExtractionResult &result);

  /** 从 AST 节点提取源代码文本。 */
  std::string get_node_text(TSNode node, const std::string &source);

  /** 提取函数签名（去掉函数体）。 */
  std::string extract_signature(TSNode node, const std::string &source);

  /** 提取文档注释。 */
  std::string extract_docstring(TSNode node, const std::string &source);
};

/**
 * Python 语言提取器。
 *
 * 使用 tree-sitter 解析 Python 源代码，提取：
 *   - 函数定义（def）
 *   - 类定义（class）
 *   - import 语句
 *   - 函数调用关系
 *
 * 比 C++ 提取器简单（没有作用域追踪、没有签名提取）。
 */
class PythonExtractor : public LanguageExtractor {
public:
  PythonExtractor();
  ~PythonExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "python"; }

private:
  TSLanguage *lang_; // tree-sitter 的 Python 语言描述符

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * JavaScript 语言提取器。
 *
 * 使用 tree-sitter 解析 JavaScript 源代码，提取：
 *   - 函数定义（function、箭头函数）
 *   - 类定义（class）
 *   - import/export 语句
 *   - 函数调用关系
 *
 * tree-sitter 的 JavaScript 语言描述符通过 tree_sitter_javascript() 获取。
 */
class JsExtractor : public LanguageExtractor {
public:
  JsExtractor();
  ~JsExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "javascript"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 const std::string &scope, ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Dart 语言提取器。
 *
 * 使用 tree-sitter 解析 Dart 源代码，提取：
 *   - 函数定义
 *   - 方法定义
 *   - 类/mixin/extension 定义
 *   - 枚举定义
 *   - import/export 语句
 *   - 函数调用关系
 *
 * tree-sitter 的 Dart 语言描述符通过 tree_sitter_dart() 获取。
 */
class DartExtractor : public LanguageExtractor {
public:
  DartExtractor();
  ~DartExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "dart"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 const std::string &scope, ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * TypeScript 语言提取器。
 *
 * 使用 tree-sitter 解析 TypeScript 源代码，提取：
 *   - 函数定义（function、箭头函数）
 *   - 类定义（class、abstract class）
 *   - 接口定义（interface）
 *   - 枚举定义（enum）
 *   - 类型别名（type alias）
 *   - import/export 语句
 *   - 函数调用关系
 *
 * tree-sitter 的 TypeScript 语言描述符通过 tree_sitter_typescript() 获取。
 */
class TsExtractor : public LanguageExtractor {
public:
  TsExtractor();
  ~TsExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "typescript"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 const std::string &scope, ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * TSX 语言提取器。
 *
 * 使用 tree-sitter 解析 TSX（TypeScript + JSX）源代码。
 * 提取的节点类型与 TypeScript 提取器相同。
 *
 * tree-sitter 的 TSX 语言描述符通过 tree_sitter_tsx() 获取。
 */
class TsxExtractor : public LanguageExtractor {
public:
  TsxExtractor();
  ~TsxExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "tsx"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 const std::string &scope, ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Rust 语言提取器。
 *
 * 使用 tree-sitter 解析 Rust 源代码，提取：
 *   - 函数定义（fn）
 *   - 结构体/枚举/trait/impl/mod 定义
 *   - use/extern crate 声明
 *   - 宏定义（macro_rules!）
 *   - 函数调用关系
 *
 * tree-sitter 的 Rust 语言描述符通过 tree_sitter_rust() 获取。
 */
class RustExtractor : public LanguageExtractor {
public:
  RustExtractor();
  ~RustExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "rust"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Markdown 语言提取器。
 *
 * 使用 tree-sitter 解析 Markdown 文档，提取：
 *   - 标题（atx_heading / setext_heading）→ 文档节结构
 *   - 链接引用定义（link_reference_definition）→ 外部资源引用
 *   - 代码块（fenced_code_block）→ 内嵌代码段
 *
 * Markdown 提取器不需要作用域追踪和调用提取，
 * 因为 Markdown 是文档格式而非编程语言。
 */
class MarkdownExtractor : public LanguageExtractor {
public:
  MarkdownExtractor();
  ~MarkdownExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "markdown"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Bash 语言提取器。
 *
 * 使用 tree-sitter 解析 Bash 脚本，提取：
 *   - 函数定义（function name / name()）
 *   - 命令调用关系
 *
 * tree-sitter 的 Bash 语言描述符通过 tree_sitter_bash() 获取。
 */
class BashExtractor : public LanguageExtractor {
public:
  BashExtractor();
  ~BashExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "bash"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Go 语言提取器。
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
class GoExtractor : public LanguageExtractor {
public:
  GoExtractor();
  ~GoExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "go"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Java 语言提取器。
 *
 * 使用 tree-sitter 解析 Java 源代码，提取：
 *   - 类定义（class）
 *   - 接口定义（interface）
 *   - 方法/构造函数定义
 *   - import 声明
 *   - 方法调用关系
 *
 * tree-sitter 的 Java 语言描述符通过 tree_sitter_java() 获取。
 */
class JavaExtractor : public LanguageExtractor {
public:
  JavaExtractor();
  ~JavaExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "java"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Kotlin 语言提取器。
 *
 * 使用 tree-sitter 解析 Kotlin 源代码，提取：
 *   - 类/接口定义（class / interface）
 *   - 函数定义（fun name）
 *   - import 声明
 *   - 函数调用关系
 *
 * tree-sitter 的 Kotlin 语言描述符通过 tree_sitter_kotlin() 获取。
 */
class KotlinExtractor : public LanguageExtractor {
public:
  KotlinExtractor();
  ~KotlinExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "kotlin"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * PHP 语言提取器。
 *
 * 使用 tree-sitter 解析 PHP 源代码，提取：
 *   - 类/接口定义（class / interface）
 *   - 函数定义（function）
 *   - 方法定义
 *   - namespace use 声明
 *   - 函数调用关系
 *
 * tree-sitter 的 PHP 语言描述符通过 tree_sitter_php() 获取。
 */
class PhpExtractor : public LanguageExtractor {
public:
  PhpExtractor();
  ~PhpExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "php"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Swift 语言提取器。
 *
 * 使用 tree-sitter 解析 Swift 源代码，提取：
 *   - 类/结构体/actor 定义（class / struct / actor）
 *   - 协议定义（protocol）
 *   - 函数定义（func name）
 *   - import 声明
 *   - 函数调用关系
 *
 * tree-sitter 的 Swift 语言描述符通过 tree_sitter_swift() 获取。
 */
class SwiftExtractor : public LanguageExtractor {
public:
  SwiftExtractor();
  ~SwiftExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "swift"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Objective-C 语言提取器。
 *
 * 使用 tree-sitter 解析 Objective-C 源代码，提取：
 *   - 类接口/实现（@interface / @implementation）
 *   - 协议定义（@protocol）
 *   - 方法定义（- / + method）
 *   - #import 声明
 *   - 方法调用关系（[obj method]）
 *
 * tree-sitter 的 Objective-C 语言描述符通过 tree_sitter_objc() 获取。
 */
class ObjcExtractor : public LanguageExtractor {
public:
  ObjcExtractor();
  ~ObjcExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "objc"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * C# 语言提取器。
 *
 * 使用 tree-sitter 解析 C# 源代码，提取：
 *   - 类/结构体/接口（class / struct / interface）
 *   - 命名空间（namespace）
 *   - 方法/构造函数
 *   - using 指令
 *   - 函数调用关系
 *
 * tree-sitter 的 C# 语言描述符通过 tree_sitter_c_sharp() 获取。
 */
class CSharpExtractor : public LanguageExtractor {
public:
  CSharpExtractor();
  ~CSharpExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "csharp"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * SQL 语言提取器。
 *
 * 使用 tree-sitter 解析 SQL 源代码，提取：
 *   - 表定义（CREATE TABLE）
 *   - 视图定义（CREATE VIEW）
 *   - 函数/存储过程定义（CREATE FUNCTION / CREATE PROCEDURE）
 *   - 函数调用关系（invocation）
 *
 * tree-sitter 的 SQL 语言描述符通过 tree_sitter_sql() 获取。
 */
class SqlExtractor : public LanguageExtractor {
public:
  SqlExtractor();
  ~SqlExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "sql"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * Lua 语言提取器。
 *
 * 使用 tree-sitter 解析 Lua 源代码，提取：
 *   - 函数定义（function name / local function name）
 *   - 函数调用关系
 *
 * tree-sitter 的 Lua 语言描述符通过 tree_sitter_lua() 获取。
 */
class LuaExtractor : public LanguageExtractor {
public:
  LuaExtractor();
  ~LuaExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "lua"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, int64_t parent_id,
                 ExtractionResult &result);
  std::string get_node_text(TSNode node, const std::string &source);
};

/**
 * YAML 语言提取器。
 *
 * 使用 tree-sitter 解析 YAML，提取：
 *   - 顶层映射键（block_mapping_pair 的 key 字段）
 *
 * tree-sitter 的 YAML 语言描述符通过 tree_sitter_yaml() 获取。
 */
class YamlExtractor : public LanguageExtractor {
public:
  YamlExtractor();
  ~YamlExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "yaml"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, ExtractionResult &result);
};

/**
 * JSON 语言提取器。
 *
 * 使用 tree-sitter 解析 JSON，提取：
 *   - 键值对（pair 的 key 字段）
 *
 * tree-sitter 的 JSON 语言描述符通过 tree_sitter_json() 获取。
 */
class JsonExtractor : public LanguageExtractor {
public:
  JsonExtractor();
  ~JsonExtractor() override;

  ExtractionResult extract(const std::string &file_path,
                           const std::string &source) override;
  const char *language_name() const override { return "json"; }

private:
  TSLanguage *lang_;

  void walk_tree(TSNode node, const std::string &source,
                 const std::string &file_path, ExtractionResult &result);
};

/**
 * 根据语言名创建对应的提取器。
 *
 * @param language
 * 语言标识（"c"/"cpp"/"cxx"/..."python"/"py"/.../"java"/..."go"）
 * @return 提取器实例，不支持的语言返回 nullptr
 */
std::unique_ptr<LanguageExtractor>
create_extractor(const std::string &language);

/**
 * 根据文件扩展名检测语言。
 *
 * @param file_path 文件路径
 * @return
 * 语言标识（"cpp"/"python"/"javascript"/"dart"/"typescript"/"tsx"/"markdown"/""）
 */
std::string detect_language(const std::string &file_path);

} // namespace codegraph