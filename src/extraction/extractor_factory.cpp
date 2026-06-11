/**
 * extractor_factory.cpp — 提取器工厂函数
 *
 * 提供 create_extractor() 工厂函数和 detect_language() 语言检测函数。
 *
 * create_extractor() 根据语言名创建对应的提取器实例。
 * detect_language() 根据文件扩展名检测语言类型。
 */

#include "codegraph/extraction/extractor.h"
#include <memory>
#include <string>

namespace codegraph {

/**
 * 根据语言名创建对应的提取器。
 *
 * 支持的语言标识：
 *   - "cpp" / "c" / "h" / "hpp" / "hxx" / "hh" → C++ 提取器
 *   - "python" / "py" → Python 提取器
 *   - "javascript" / "js" → JavaScript 提取器
 *   - "dart" → Dart 提取器
 *   - "typescript" / "ts" → TypeScript 提取器
 *   - "tsx" → TSX 提取器
 *   - "markdown" / "md" / "mdx" → Markdown 提取器
 *   - 其他 → 返回 nullptr（不支持）
 */
std::unique_ptr<LanguageExtractor>
create_extractor(const std::string &language) {
  if (language == "cpp" || language == "c" || language == "h" ||
      language == "hpp" || language == "hxx" || language == "hh") {
    return std::make_unique<CppExtractor>();
  }
  if (language == "python" || language == "py") {
    return std::make_unique<PythonExtractor>();
  }
  if (language == "javascript" || language == "js") {
    return std::make_unique<JsExtractor>();
  }
  if (language == "dart") {
    return std::make_unique<DartExtractor>();
  }
  if (language == "typescript" || language == "ts") {
    return std::make_unique<TsExtractor>();
  }
  if (language == "tsx") {
    return std::make_unique<TsxExtractor>();
  }
  if (language == "markdown" || language == "md" || language == "mdx") {
    return std::make_unique<MarkdownExtractor>();
  }
  if (language == "rust" || language == "rs") {
    return std::make_unique<RustExtractor>();
  }
  if (language == "bash" || language == "sh") {
    return std::make_unique<BashExtractor>();
  }
  if (language == "go") {
    return std::make_unique<GoExtractor>();
  }
  if (language == "java") {
    return std::make_unique<JavaExtractor>();
  }
  if (language == "kotlin" || language == "kt") {
    return std::make_unique<KotlinExtractor>();
  }
  if (language == "php") {
    return std::make_unique<PhpExtractor>();
  }
  if (language == "swift") {
    return std::make_unique<SwiftExtractor>();
  }
  if (language == "objc" || language == "objective-c") {
    return std::make_unique<ObjcExtractor>();
  }
  if (language == "csharp" || language == "cs" || language == "c#") {
    return std::make_unique<CSharpExtractor>();
  }
  if (language == "sql") {
    return std::make_unique<SqlExtractor>();
  }
  if (language == "lua") {
    return std::make_unique<LuaExtractor>();
  }
  if (language == "yaml" || language == "yml") {
    return std::make_unique<YamlExtractor>();
  }
  if (language == "json") {
    return std::make_unique<JsonExtractor>();
  }
  return nullptr;
}

/**
 * 根据文件扩展名检测语言。
 *
 * 返回值：
 *   - "cpp" → C/C++ 源文件或头文件
 *   - "python" → Python 文件
 *   - "javascript" → JavaScript 文件
 *   - "dart" → Dart 文件
 *   - "typescript" → TypeScript 文件
 *   - "tsx" → TSX 文件
 *   - "markdown" → Markdown 文件
 *   - "" → 不支持的语言
 */
std::string detect_language(const std::string &file_path) {
  auto dot = file_path.rfind('.');
  if (dot == std::string::npos)
    return "";
  std::string ext = file_path.substr(dot + 1);
  if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c" || ext == "h" ||
      ext == "hpp" || ext == "hxx" || ext == "hh")
    return "cpp";
  if (ext == "py" || ext == "pyi")
    return "python";
  if (ext == "js" || ext == "mjs" || ext == "jsx" || ext == "cjs")
    return "javascript";
  if (ext == "dart")
    return "dart";
  if (ext == "ts" || ext == "mts" || ext == "cts")
    return "typescript";
  if (ext == "tsx")
    return "tsx";
  if (ext == "md" || ext == "mdx" || ext == "markdown")
    return "markdown";
  if (ext == "rs")
    return "rust";
  if (ext == "sh" || ext == "bash")
    return "bash";
  if (ext == "go")
    return "go";
  if (ext == "java")
    return "java";
  if (ext == "kt" || ext == "kts")
    return "kotlin";
  if (ext == "php")
    return "php";
  if (ext == "swift")
    return "swift";
  if (ext == "m" || ext == "mm")
    return "objc";
  if (ext == "cs")
    return "csharp";
  if (ext == "sql")
    return "sql";
  if (ext == "lua")
    return "lua";
  if (ext == "yaml" || ext == "yml")
    return "yaml";
  if (ext == "json")
    return "json";
  return "";
}

} // namespace codegraph