#include "codegraph/context/context_builder.h"
#include "codegraph/core/types.h"
#include "codegraph/db/database.h"
#include "codegraph/diff/diff_parser.h"
#include "codegraph/extraction/extractor.h"
#include "codegraph/graph/traverser.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace codegraph;
namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail_check(const char *expr, const char *file, int line) {
  std::ostringstream msg;
  msg << file << ":" << line << ": check failed: " << expr;
  throw std::runtime_error(msg.str());
}

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr))                                                               \
      fail_check(#expr, __FILE__, __LINE__);                                   \
  } while (false)

std::string temp_db_path(const std::string &name) {
  return (fs::temp_directory_path() /
          ("codegraph_test_" + std::to_string(getpid()) + "_" + name))
      .string();
}

void remove_db_files(const std::string &path) {
  fs::remove_all(path);
  fs::remove_all(path + "-wal");
  fs::remove_all(path + "-shm");
}

struct TempDb {
  explicit TempDb(const std::string &name) : path(temp_db_path(name)) {
    remove_db_files(path);
  }

  ~TempDb() { remove_db_files(path); }

  std::string path;
};

fs::path find_codegraph_exe() {
  const fs::path cwd = fs::current_path();
  const std::vector<fs::path> candidates = {cwd / "build" / "codegraph",
                                            cwd / "codegraph",
                                            cwd.parent_path() / "codegraph"};

  for (const auto &candidate : candidates) {
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return fs::absolute(candidate);
    }
  }

  throw std::runtime_error(
      "Cannot find codegraph executable. Build target 'codegraph' first.");
}

} // namespace

void test_types() {
  CHECK(strcmp(node_kind_str(NodeKind::Function), "function") == 0);
  CHECK(strcmp(edge_kind_str(EdgeKind::Calls), "calls") == 0);
  std::cout << "  [PASS] types\n";
}

void test_database() {
  TempDb temp_db("database.db");

  Database db(temp_db.path);
  db.init_schema();

  Node n;
  n.kind = NodeKind::Function;
  n.name = "test_func";
  n.qualified_name = "ns::test_func";
  n.file_path = "/tmp/test.cpp";
  n.language = "cpp";
  n.line = 10;
  n.col = 1;
  n.end_line = 20;
  n.end_col = 1;
  n.signature = "void test_func(int x)";
  int64_t id = db.insert_node(n);
  CHECK(id > 0);

  auto got = db.get_node(id);
  CHECK(got.has_value());
  CHECK(got->name == "test_func");
  CHECK(got->kind == NodeKind::Function);

  Node n2;
  n2.kind = NodeKind::Function;
  n2.name = "caller_func";
  n2.file_path = "/tmp/test.cpp";
  n2.language = "cpp";
  int64_t id2 = db.insert_node(n2);

  Edge e;
  e.source_id = id2;
  e.target_id = id;
  e.kind = EdgeKind::Calls;
  e.line = 15;
  int64_t eid = db.insert_edge(e);
  CHECK(eid > 0);

  auto callers = db.get_edges_to(id, EdgeKind::Calls);
  CHECK(callers.size() == 1);
  CHECK(callers[0].source_id == id2);

  auto results = db.search_fts("test_func");
  CHECK(!results.empty());

  auto qualified_results = db.search_fts("ns::test_func");
  CHECK(!qualified_results.empty());

  CHECK(db.count_nodes() == 2);
  CHECK(db.count_edges() == 1);

  std::cout << "  [PASS] database\n";
}

void test_database_lookup_and_errors() {
  TempDb temp_db("database_lookup.db");

  Database db(temp_db.path);
  db.init_schema();

  Node exact;
  exact.kind = NodeKind::Function;
  exact.name = "foo";
  exact.qualified_name = "ns::foo";
  exact.file_path = "/tmp/a.cpp";
  exact.language = "cpp";
  int64_t exact_id = db.insert_node(exact);
  CHECK(exact_id > 0);

  Node fuzzy;
  fuzzy.kind = NodeKind::Function;
  fuzzy.name = "foo_helper";
  fuzzy.qualified_name = "ns::foo_helper";
  fuzzy.file_path = "/tmp/b.cpp";
  fuzzy.language = "cpp";
  int64_t fuzzy_id = db.insert_node(fuzzy);
  CHECK(fuzzy_id > 0);

  auto exact_results = db.find_nodes_by_name("foo", 1);
  CHECK(exact_results.size() == 1);
  CHECK(exact_results[0].id == exact_id);

  Edge invalid;
  invalid.source_id = 999999;
  invalid.target_id = exact_id;
  invalid.kind = EdgeKind::Calls;
  CHECK(db.insert_edge(invalid) < 0);

  bool batch_failed = false;
  try {
    db.insert_edges_batch(std::vector<Edge>{invalid});
  } catch (const std::runtime_error &) {
    batch_failed = true;
  }
  CHECK(batch_failed);

  std::cout << "  [PASS] database_lookup_and_errors\n";
}

void test_cpp_extractor() {
  CppExtractor extractor;
  std::string source = R"(
#include <iostream>

namespace math {
    int add(int a, int b) {
        return a + b;
    }

    class Calculator {
    public:
        int multiply(int a, int b) {
            return a * b;
        }
    };
}
)";

  auto result = extractor.extract("/tmp/test.cpp", source);
  CHECK(!result.nodes.empty());

  bool found_add = false, found_calc = false, found_multiply = false;
  bool found_qualified_add = false, found_qualified_multiply = false;
  for (auto &n : result.nodes) {
    if (n.name == "add")
      found_add = true;
    if (n.name == "Calculator")
      found_calc = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.qualified_name == "math::add")
      found_qualified_add = true;
    if (n.qualified_name == "math::Calculator::multiply")
      found_qualified_multiply = true;
  }
  CHECK(found_add);
  CHECK(found_calc);
  CHECK(found_multiply);
  CHECK(found_qualified_add);
  CHECK(found_qualified_multiply);

  std::cout << "  [PASS] cpp_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_cpp_member_call_extraction() {
  CppExtractor extractor;
  std::string source = R"(
namespace math {
    int add(int a, int b) {
        return a + b;
    }

    struct Helper {
        void reset() {}
        template <typename T>
        T get() { return T{}; }
    };

    int use(Helper& ref, Helper* ptr) {
        add(1, 2);
        math::add(3, 4);
        ref.reset();
        ptr->reset();
        return ref.template get<int>();
    }
}
)";

  auto result = extractor.extract("/tmp/member_calls.cpp", source);
  std::unordered_set<std::string> calls;
  for (const auto &ref : result.unresolved) {
    calls.insert(ref.ref_name);
  }

  CHECK(calls.contains("add") > 0);
  CHECK(calls.contains("reset") > 0);
  CHECK(calls.contains("get") > 0);
  std::cout << "  [PASS] cpp_member_call_extraction\n";
}

void test_run_git_diff_does_not_execute_shell() {
  const char *marker = "/tmp/codegraph_git_diff_shell_injection_marker";
  std::remove(marker);

  (void)run_git_diff(std::string("HEAD; touch ") + marker);

  FILE *file = std::fopen(marker, "r");
  CHECK(file == nullptr);
  if (file != nullptr) {
    std::fclose(file);
    std::remove(marker);
  }

  std::cout << "  [PASS] run_git_diff_does_not_execute_shell\n";
}

void test_context_builder_splits_callers_and_callees() {
  TempDb temp_db("context.db");

  Database db(temp_db.path);
  db.init_schema();

  Node caller;
  caller.kind = NodeKind::Function;
  caller.name = "caller";
  caller.file_path = "/tmp/context.cpp";
  caller.language = "cpp";
  int64_t caller_id = db.insert_node(caller);

  Node target;
  target.kind = NodeKind::Function;
  target.name = "target";
  target.file_path = "/tmp/context.cpp";
  target.language = "cpp";
  int64_t target_id = db.insert_node(target);

  Node callee;
  callee.kind = NodeKind::Function;
  callee.name = "callee";
  callee.file_path = "/tmp/context.cpp";
  callee.language = "cpp";
  int64_t callee_id = db.insert_node(callee);

  Edge caller_edge;
  caller_edge.source_id = caller_id;
  caller_edge.target_id = target_id;
  caller_edge.kind = EdgeKind::Calls;
  CHECK(db.insert_edge(caller_edge) > 0);

  Edge callee_edge;
  callee_edge.source_id = target_id;
  callee_edge.target_id = callee_id;
  callee_edge.kind = EdgeKind::Calls;
  CHECK(db.insert_edge(callee_edge) > 0);

  GraphTraverser traverser(db);
  ContextBuilder context(db, traverser);
  auto result = context.build_context("target");

  CHECK(result.contains("callers"));
  CHECK(result.contains("callees"));
  CHECK(result["callers"].size() == 1);
  CHECK(result["callees"].size() == 1);
  CHECK(result["callers"][0]["name"] == "caller");
  CHECK(result["callees"][0]["name"] == "callee");

  std::cout << "  [PASS] context_builder_splits_callers_and_callees\n";
}

void test_python_extractor() {
  PythonExtractor extractor;
  std::string source = R"(
import os

def hello(name):
    """Say hello"""
    print(f"Hello {name}")

class Greeter:
    def greet(self):
        hello("world")
)";

  auto result = extractor.extract("/tmp/test.py", source);
  CHECK(!result.nodes.empty());

  bool found_hello = false, found_greeter = false;
  for (auto &n : result.nodes) {
    if (n.name == "hello")
      found_hello = true;
    if (n.name == "Greeter")
      found_greeter = true;
  }
  CHECK(found_hello);
  CHECK(found_greeter);

  std::cout << "  [PASS] python_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_python_call_extraction() {
  PythonExtractor extractor;
  std::string source = R"(
def foo():
    bar()

def bar():
    foo()
)";

  auto result = extractor.extract("/tmp/test_calls.py", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  std::cout << "  [PASS] python_call_extraction\n";
}

void test_js_extractor() {
  JsExtractor extractor;
  std::string source = R"(
import { helper } from './utils';

function greet(name) {
    console.log(`Hello, ${name}`);
}

class Greeter {
    sayHello(name) {
        greet(name);
    }

    async fetchData(url) {
        const response = await fetch(url);
        return response.json();
    }
}

const formatName = (first, last) => {
    return `${last}, ${first}`;
};
)";

  auto result = extractor.extract("/tmp/test.js", source);
  CHECK(!result.nodes.empty());

  bool found_greet = false, found_greeter = false, found_sayHello = false,
       found_fetchData = false;
  bool found_qualified_sayHello = false;
  for (auto &n : result.nodes) {
    if (n.name == "greet")
      found_greet = true;
    if (n.name == "Greeter")
      found_greeter = true;
    if (n.name == "sayHello")
      found_sayHello = true;
    if (n.name == "fetchData")
      found_fetchData = true;
    if (n.qualified_name == "Greeter.sayHello")
      found_qualified_sayHello = true;
  }
  CHECK(found_greet);
  CHECK(found_greeter);
  CHECK(found_sayHello);
  CHECK(found_fetchData);
  CHECK(found_qualified_sayHello);

  std::cout << "  [PASS] js_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_js_call_extraction() {
  JsExtractor extractor;
  std::string source = R"(
function foo() {
    bar();
}

function bar() {
    foo();
    baz(1, 2);
}

function baz(a, b) {
    return a + b;
}

const obj = {
    doWork() {
        foo();
    }
};
)";

  auto result = extractor.extract("/tmp/test_calls.js", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] js_call_extraction\n";
}

void test_js_member_call_extraction() {
  JsExtractor extractor;
  std::string source = R"(
class Calculator {
    add(a, b) {
        return a + b;
    }

    compute() {
        this.add(1, 2);
        const result = this.add(3, 4);
        return result;
    }
}

function useCalculator() {
    const calc = new Calculator();
    calc.compute();
}
)";

  auto result = extractor.extract("/tmp/test_member.js", source);
  CHECK(!result.unresolved.empty());

  bool found_add_call = false, found_compute_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "add")
      found_add_call = true;
    if (ref.ref_name == "compute")
      found_compute_call = true;
  }
  CHECK(found_add_call);
  CHECK(found_compute_call);
  std::cout << "  [PASS] js_member_call_extraction\n";
}

void test_dart_extractor() {
  DartExtractor extractor;
  std::string source = R"(
import 'dart:math';

int add(int a, int b) {
  return a + b;
}

class Calculator {
  int multiply(int a, int b) {
    return a * b;
  }

  String greet(String name) {
    return 'Hello, $name';
  }
}

mixin Loggable {
  void log(String msg) {
    print(msg);
  }
}

enum Color { red, green, blue }
)";

  auto result = extractor.extract("/tmp/test.dart", source);
  CHECK(!result.nodes.empty());

  bool found_add = false, found_calc = false, found_multiply = false,
       found_greet = false, found_loggable = false, found_color = false,
       found_log = false;
  for (auto &n : result.nodes) {
    if (n.name == "add")
      found_add = true;
    if (n.name == "Calculator")
      found_calc = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "greet")
      found_greet = true;
    if (n.name == "Loggable")
      found_loggable = true;
    if (n.name == "Color")
      found_color = true;
    if (n.name == "log")
      found_log = true;
  }
  CHECK(found_add);
  CHECK(found_calc);
  CHECK(found_multiply);
  CHECK(found_greet);
  CHECK(found_loggable);
  CHECK(found_color);
  CHECK(found_log);

  bool found_qualified_multiply = false;
  for (auto &n : result.nodes) {
    if (n.qualified_name == "Calculator.multiply")
      found_qualified_multiply = true;
  }
  CHECK(found_qualified_multiply);

  std::cout << "  [PASS] dart_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_dart_call_extraction() {
  DartExtractor extractor;
  std::string source = R"(
void foo() {
  bar();
}

void bar() {
  foo();
  baz(1, 2);
}

int baz(int a, int b) {
  return a + b;
}

class Worker {
  void doWork() {
    foo();
  }
}
)";

  auto result = extractor.extract("/tmp/test_calls.dart", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] dart_call_extraction\n";
}

void test_dart_member_call_extraction() {
  DartExtractor extractor;
  std::string source = R"(
class Calculator {
  int add(int a, int b) {
    return a + b;
  }

  int compute() {
    return add(1, 2) + add(3, 4);
  }
}

void useCalculator() {
  var calc = Calculator();
  calc.compute();
}
)";

  auto result = extractor.extract("/tmp/test_member.dart", source);
  CHECK(!result.unresolved.empty());

  bool found_add_call = false, found_compute_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "add")
      found_add_call = true;
    if (ref.ref_name == "compute")
      found_compute_call = true;
  }
  CHECK(found_add_call);
  CHECK(found_compute_call);
  std::cout << "  [PASS] dart_member_call_extraction\n";
}

void test_ts_extractor() {
  TsExtractor extractor;
  std::string source = R"(
import { helper } from './utils';

function greet(name: string): void {
    console.log(`Hello, ${name}`);
}

class Greeter {
    sayHello(name: string): void {
        greet(name);
        this.log(name);
    }

    private log(msg: string): void {
        console.log(msg);
    }

    async fetchData(url: string): Promise<Response> {
        const response = await fetch(url);
        return response.json();
    }
}

interface Printable {
    print(): void;
}

enum Color {
    Red,
    Green,
    Blue
}

type Point = {
    x: number;
    y: number;
};

abstract class BaseService {
    abstract init(): void;
}

const formatName = (first: string, last: string): string => {
    return `${last}, ${first}`;
};
)";

  auto result = extractor.extract("/tmp/test.ts", source);
  CHECK(!result.nodes.empty());

  bool found_greet = false, found_greeter = false, found_sayHello = false,
       found_log = false, found_fetchData = false, found_printable = false,
       found_color = false, found_base_service = false;
  bool found_qualified_sayHello = false;
  for (auto &n : result.nodes) {
    if (n.name == "greet")
      found_greet = true;
    if (n.name == "Greeter")
      found_greeter = true;
    if (n.name == "sayHello")
      found_sayHello = true;
    if (n.name == "log")
      found_log = true;
    if (n.name == "fetchData")
      found_fetchData = true;
    if (n.name == "Printable")
      found_printable = true;
    if (n.name == "Color")
      found_color = true;
    if (n.name == "BaseService")
      found_base_service = true;
    if (n.qualified_name == "Greeter.sayHello")
      found_qualified_sayHello = true;
  }
  CHECK(found_greet);
  CHECK(found_greeter);
  CHECK(found_sayHello);
  CHECK(found_log);
  CHECK(found_fetchData);
  CHECK(found_printable);
  CHECK(found_color);
  CHECK(found_base_service);
  CHECK(found_qualified_sayHello);

  std::cout << "  [PASS] ts_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_ts_call_extraction() {
  TsExtractor extractor;
  std::string source = R"(
function foo(): void {
    bar();
}

function bar(): void {
    foo();
    baz(1, 2);
}

function baz(a: number, b: number): number {
    return a + b;
}

class Worker {
    doWork(): void {
        foo();
        this.internalWork();
    }

    private internalWork(): void {
        baz(1, 2);
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.ts", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false,
       found_internal_work_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
    if (ref.ref_name == "internalWork")
      found_internal_work_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  CHECK(found_internal_work_call);
  std::cout << "  [PASS] ts_call_extraction\n";
}

void test_ts_member_call_extraction() {
  TsExtractor extractor;
  std::string source = R"(
class Calculator {
    add(a: number, b: number): number {
        return a + b;
    }

    compute(): number {
        this.add(1, 2);
        const result = this.add(3, 4);
        return result;
    }
}

function useCalculator(): void {
    const calc = new Calculator();
    calc.compute();
}
)";

  auto result = extractor.extract("/tmp/test_member.ts", source);
  CHECK(!result.unresolved.empty());

  bool found_add_call = false, found_compute_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "add")
      found_add_call = true;
    if (ref.ref_name == "compute")
      found_compute_call = true;
  }
  CHECK(found_add_call);
  CHECK(found_compute_call);
  std::cout << "  [PASS] ts_member_call_extraction\n";
}

void test_tsx_extractor() {
  TsxExtractor extractor;
  std::string source = R"(
import React from 'react';

interface Props {
    name: string;
    age: number;
}

function Greet(props: Props): JSX.Element {
    return <div>Hello, {props.name}!</div>;
}

class App extends React.Component<Props> {
    render(): JSX.Element {
        return (
            <div>
                <Greet name={this.props.name} age={this.props.age} />
            </div>
        );
    }
}

const StatelessApp: React.FC<Props> = ({ name, age }) => {
    return <Greet name={name} age={age} />;
};
)";

  auto result = extractor.extract("/tmp/test.tsx", source);
  CHECK(!result.nodes.empty());

  bool found_greet = false, found_app = false, found_render = false;
  for (auto &n : result.nodes) {
    if (n.name == "Greet")
      found_greet = true;
    if (n.name == "App")
      found_app = true;
    if (n.name == "render")
      found_render = true;
  }
  CHECK(found_greet);
  CHECK(found_app);
  CHECK(found_render);

  std::cout << "  [PASS] tsx_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_detect_language() {
  CHECK(detect_language("foo.cpp") == "cpp");
  CHECK(detect_language("foo.cc") == "cpp");
  CHECK(detect_language("foo.cxx") == "cpp");
  CHECK(detect_language("foo.c") == "cpp");
  CHECK(detect_language("foo.h") == "cpp");
  CHECK(detect_language("foo.hpp") == "cpp");
  CHECK(detect_language("foo.hxx") == "cpp");
  CHECK(detect_language("foo.hh") == "cpp");
  CHECK(detect_language("foo.py") == "python");
  CHECK(detect_language("foo.pyi") == "python");
  CHECK(detect_language("foo.js") == "javascript");
  CHECK(detect_language("foo.mjs") == "javascript");
  CHECK(detect_language("foo.jsx") == "javascript");
  CHECK(detect_language("foo.cjs") == "javascript");
  CHECK(detect_language("foo.dart") == "dart");
  CHECK(detect_language("foo.ts") == "typescript");
  CHECK(detect_language("foo.mts") == "typescript");
  CHECK(detect_language("foo.cts") == "typescript");
  CHECK(detect_language("foo.tsx") == "tsx");
  CHECK(detect_language("foo.rs") == "rust");
  CHECK(detect_language("foo.txt") == "");
  CHECK(detect_language("foo.md") == "markdown");
  CHECK(detect_language("foo.mdx") == "markdown");
  CHECK(detect_language("foo.markdown") == "markdown");
  CHECK(detect_language("foo.sh") == "bash");
  CHECK(detect_language("foo.bash") == "bash");
  CHECK(detect_language("foo.go") == "go");
  CHECK(detect_language("foo.java") == "java");
  CHECK(detect_language("foo.kt") == "kotlin");
  CHECK(detect_language("foo.kts") == "kotlin");
  CHECK(detect_language("foo.php") == "php");
  CHECK(detect_language("foo.swift") == "swift");
  CHECK(detect_language("foo.m") == "objc");
  CHECK(detect_language("foo.mm") == "objc");
  CHECK(detect_language("foo.cs") == "csharp");
  CHECK(detect_language("foo.sql") == "sql");
  CHECK(detect_language("foo.lua") == "lua");
  CHECK(detect_language("foo.yaml") == "yaml");
  CHECK(detect_language("foo.yml") == "yaml");
  CHECK(detect_language("foo.json") == "json");
  CHECK(detect_language("foo.xml") == "xml");
  CHECK(detect_language("foo.html") == "html");
  CHECK(detect_language("foo.htm") == "html");
  std::cout << "  [PASS] detect_language\n";
}

int run_codegraph_cli(const fs::path &exe, const fs::path &cwd,
                      const std::vector<std::string> &args) {
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    if (chdir(cwd.c_str()) != 0) {
      _exit(127);
    }

    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    storage.push_back(exe.string());
    for (const auto &arg : args) {
      storage.push_back(arg);
    }

    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (auto &arg : storage) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execv(argv[0], argv.data());
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return 1;
    }
  }
  if (!WIFEXITED(status))
    return 1;
  return WEXITSTATUS(status);
}

void test_incremental_reindex() {
  const fs::path exe = find_codegraph_exe();

  const fs::path root = fs::path("/tmp") / ("codegraph_incremental_cli_" +
                                            std::to_string(getpid()));
  fs::remove_all(root);
  fs::create_directories(root);

  const fs::path changed_file = root / "changed.cpp";
  const fs::path caller_file = root / "caller.cpp";
  const fs::path stable_file = root / "stable.cpp";
  {
    std::ofstream out(changed_file);
    out << "void beta() {}\nvoid alpha() { beta(); }\n";
  }
  {
    std::ofstream out(caller_file);
    out << "void external() { alpha(); }\n";
  }
  {
    std::ofstream out(stable_file);
    out << "void stable() {}\n";
  }

  CHECK(run_codegraph_cli(exe, root, {"init", "-i", root.string()}) == 0);

  const fs::path db_path = root / ".codegraph" / "index";
  int64_t stable_id_before = 0;
  {
    Database db(db_path.string());
    auto stable_nodes = db.find_nodes_by_name("stable", 5);
    CHECK(stable_nodes.size() == 1);
    stable_id_before = stable_nodes[0].id;
    CHECK(!db.find_nodes_by_name("alpha", 5).empty());
    CHECK(!db.find_nodes_by_name("beta", 5).empty());
    CHECK(!db.find_nodes_by_name("external", 5).empty());
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  {
    std::ofstream out(changed_file);
    out << "void gamma() {}\n";
  }

  CHECK(run_codegraph_cli(exe, root, {"index", root.string()}) == 0);

  {
    Database db(db_path.string());
    auto gamma_nodes = db.find_nodes_by_name("gamma", 5);
    CHECK(gamma_nodes.size() == 1);
    CHECK(gamma_nodes[0].file_path == changed_file.string());

    auto stable_nodes = db.find_nodes_by_name("stable", 5);
    CHECK(stable_nodes.size() == 1);
    CHECK(stable_nodes[0].id == stable_id_before);

    for (const auto &n : db.find_nodes_by_name("alpha", 5)) {
      CHECK(n.file_path != changed_file.string());
    }
    for (const auto &n : db.find_nodes_by_name("beta", 5)) {
      CHECK(n.file_path != changed_file.string());
    }

    bool found_unresolved_alpha_from_caller = false;
    for (const auto &ref : db.get_unresolved_refs()) {
      if (ref.ref_name != "alpha")
        continue;
      auto source = db.get_node(ref.source_node_id);
      if (source.has_value() && source->file_path == caller_file.string()) {
        found_unresolved_alpha_from_caller = true;
        break;
      }
    }
    CHECK(found_unresolved_alpha_from_caller);
  }

  fs::remove_all(root);
  std::cout << "  [PASS] incremental_reindex\n";
}

void test_context_aware_same_name_resolution() {
  const fs::path exe = find_codegraph_exe();

  const fs::path root =
      fs::path("/tmp") / ("codegraph_same_name_" + std::to_string(getpid()));
  fs::remove_all(root);
  fs::create_directories(root);

  const fs::path local_file = root / "local.cpp";
  const fs::path other_file = root / "other.cpp";
  {
    std::ofstream out(local_file);
    out << "void WaitServerReady() {}\n"
        << "void Use() { WaitServerReady(); }\n";
  }
  {
    std::ofstream out(other_file);
    out << "void WaitServerReady() {}\n";
  }

  CHECK(run_codegraph_cli(exe, root, {"init", "-i", root.string()}) == 0);

  {
    Database db((root / ".codegraph" / "index").string());
    auto uses = db.find_nodes_by_name("Use", 5);
    CHECK(uses.size() == 1);

    auto calls = db.get_edges_from(uses[0].id, EdgeKind::Calls);
    bool found_local_target = false;
    bool found_wrong_target = false;
    for (const auto &edge : calls) {
      auto target = db.get_node(edge.target_id);
      CHECK(target.has_value());
      if (target->name == "WaitServerReady" &&
          target->file_path == local_file.string()) {
        found_local_target = true;
      }
      if (target->name == "WaitServerReady" &&
          target->file_path == other_file.string()) {
        found_wrong_target = true;
      }
    }

    CHECK(found_local_target);
    CHECK(!found_wrong_target);
  }

  fs::remove_all(root);
  std::cout << "  [PASS] context_aware_same_name_resolution\n";
}

void test_tarjan_scc() {
  TempDb temp_db("scc.db");

  Database db(temp_db.path);
  db.init_schema();

  // Register the file so get_all_files() finds it
  FileRecord fr;
  fr.path = "/tmp/scc.cpp";
  fr.language = "cpp";
  fr.mtime = 0;
  fr.size = 100;
  db.insert_file(fr);

  // Create a cycle: A -> B -> C -> A
  Node a, b, c, d;
  a.kind = NodeKind::Function;
  a.name = "a";
  a.file_path = "/tmp/scc.cpp";
  a.language = "cpp";
  b.kind = NodeKind::Function;
  b.name = "b";
  b.file_path = "/tmp/scc.cpp";
  b.language = "cpp";
  c.kind = NodeKind::Function;
  c.name = "c";
  c.file_path = "/tmp/scc.cpp";
  c.language = "cpp";
  d.kind = NodeKind::Function;
  d.name = "d";
  d.file_path = "/tmp/scc.cpp";
  d.language = "cpp";

  int64_t aid = db.insert_node(a);
  int64_t bid = db.insert_node(b);
  int64_t cid = db.insert_node(c);
  int64_t did = db.insert_node(d);

  // A -> B -> C -> A (cycle)
  Edge e1;
  e1.source_id = aid;
  e1.target_id = bid;
  e1.kind = EdgeKind::Calls;
  db.insert_edge(e1);
  Edge e2;
  e2.source_id = bid;
  e2.target_id = cid;
  e2.kind = EdgeKind::Calls;
  db.insert_edge(e2);
  Edge e3;
  e3.source_id = cid;
  e3.target_id = aid;
  e3.kind = EdgeKind::Calls;
  db.insert_edge(e3);

  // D -> A (not part of the cycle, but reachable)
  Edge e4;
  e4.source_id = did;
  e4.target_id = aid;
  e4.kind = EdgeKind::Calls;
  db.insert_edge(e4);

  GraphTraverser traverser(db);

  // Find all SCCs
  auto sccs = traverser.find_sccs();
  // Should have at least 2 SCCs: {A,B,C} and {D}
  bool found_cycle = false;
  for (const auto &scc : sccs) {
    if (scc.size() == 3) {
      std::unordered_set<int64_t> ids(scc.begin(), scc.end());
      if (ids.contains(aid) && ids.contains(bid) && ids.contains(cid)) {
        found_cycle = true;
      }
    }
  }
  CHECK(found_cycle);

  // Find circular dependencies (SCCs with size > 1)
  auto cycles = traverser.find_circular_dependencies();
  CHECK(cycles.size() == 1);
  CHECK(cycles[0].size() == 3);

  std::cout << "  [PASS] tarjan_scc\n";
}

void test_find_path() {
  TempDb temp_db("path.db");

  Database db(temp_db.path);
  db.init_schema();

  // A -> B -> C -> D, 有路径 A→D
  Node a, b, c, d;
  a.kind = NodeKind::Function;
  a.name = "a";
  a.file_path = "/tmp/path.cpp";
  a.language = "cpp";
  b.kind = NodeKind::Function;
  b.name = "b";
  b.file_path = "/tmp/path.cpp";
  b.language = "cpp";
  c.kind = NodeKind::Function;
  c.name = "c";
  c.file_path = "/tmp/path.cpp";
  c.language = "cpp";
  d.kind = NodeKind::Function;
  d.name = "d";
  d.file_path = "/tmp/path.cpp";
  d.language = "cpp";

  int64_t aid = db.insert_node(a);
  int64_t bid = db.insert_node(b);
  int64_t cid = db.insert_node(c);
  int64_t did = db.insert_node(d);

  Edge e1;
  e1.source_id = aid;
  e1.target_id = bid;
  e1.kind = EdgeKind::Calls;
  db.insert_edge(e1);
  Edge e2;
  e2.source_id = bid;
  e2.target_id = cid;
  e2.kind = EdgeKind::Calls;
  db.insert_edge(e2);
  Edge e3;
  e3.source_id = cid;
  e3.target_id = did;
  e3.kind = EdgeKind::Calls;
  db.insert_edge(e3);

  GraphTraverser traverser(db);

  // 有路径：A → B → C → D
  auto p = traverser.find_path(aid, did);
  CHECK(p.size() == 4);
  CHECK(p[0] == aid);
  CHECK(p[3] == did);

  // 无反向路径：D → A
  auto no_path = traverser.find_path(did, aid);
  CHECK(no_path.empty());

  // 自身到自身
  auto self = traverser.find_path(aid, aid);
  CHECK(self.size() == 1);
  CHECK(self[0] == aid);

  std::cout << "  [PASS] find_path\n";
}

void test_compute_metrics() {
  TempDb temp_db("metrics.db");

  Database db(temp_db.path);
  db.init_schema();

  FileRecord fr;
  fr.path = "/tmp/metrics.cpp";
  fr.language = "cpp";
  fr.mtime = 0;
  fr.size = 100;
  db.insert_file(fr);

  // main -> A -> B -> C
  // main -> B (B 被调用 2 次)
  Node main_n, a, b, c;
  main_n.kind = NodeKind::Function;
  main_n.name = "main";
  main_n.file_path = "/tmp/metrics.cpp";
  main_n.language = "cpp";
  a.kind = NodeKind::Function;
  a.name = "a";
  a.file_path = "/tmp/metrics.cpp";
  a.language = "cpp";
  b.kind = NodeKind::Function;
  b.name = "b";
  b.file_path = "/tmp/metrics.cpp";
  b.language = "cpp";
  c.kind = NodeKind::Function;
  c.name = "c";
  c.file_path = "/tmp/metrics.cpp";
  c.language = "cpp";

  int64_t mid = db.insert_node(main_n);
  int64_t aid = db.insert_node(a);
  int64_t bid = db.insert_node(b);
  int64_t cid = db.insert_node(c);

  Edge e1;
  e1.source_id = mid;
  e1.target_id = aid;
  e1.kind = EdgeKind::Calls;
  db.insert_edge(e1);
  Edge e2;
  e2.source_id = mid;
  e2.target_id = bid;
  e2.kind = EdgeKind::Calls;
  db.insert_edge(e2);
  Edge e3;
  e3.source_id = aid;
  e3.target_id = bid;
  e3.kind = EdgeKind::Calls;
  db.insert_edge(e3);
  Edge e4;
  e4.source_id = bid;
  e4.target_id = cid;
  e4.kind = EdgeKind::Calls;
  db.insert_edge(e4);

  GraphTraverser traverser(db);
  auto metrics = traverser.compute_metrics(5);

  CHECK(metrics.total_nodes == 4);
  CHECK(metrics.total_edges == 4);
  CHECK(metrics.circular_deps == 0);
  // BFS 从 main 出发：main(0) → A(1), B(1) → C(2)。B 先被 main 访问，深度 1。
  // 所以最大深度是 2（main → B → C），不是 3（main → A → B → C 不是最短路径）
  CHECK(metrics.max_call_depth == 2);

  // b 被调用 2 次（main 和 a 各调用一次），应该排第一
  CHECK(!metrics.most_called.empty());
  CHECK(metrics.most_called[0].first.name == "b");
  CHECK(metrics.most_called[0].second == 2);

  // main 调用 2 个函数，a 调用 1 个，b 调用 1 个
  CHECK(!metrics.most_calling.empty());
  CHECK(metrics.most_calling[0].first.name == "main");
  CHECK(metrics.most_calling[0].second == 2);

  std::cout << "  [PASS] compute_metrics\n";
}

void test_markdown_extractor() {
  MarkdownExtractor extractor;
  std::string source = R"(
# Project Title

## Introduction

This is a paragraph with **bold** and *italic* text.

## Features

- Feature 1
- Feature 2
- Feature 3

### Code Example

```cpp
int main() {
    return 0;
}
```

```python
def hello():
    print("world")
```

[google]: https://www.google.com
[github]: https://github.com
)";

  auto result = extractor.extract("/tmp/test.md", source);
  CHECK(!result.nodes.empty());

  bool found_title = false, found_intro = false, found_features = false,
       found_code_example = false, found_google = false, found_github = false;
  bool found_cpp_code = false, found_python_code = false;

  for (auto &n : result.nodes) {
    if (n.name == "Project Title")
      found_title = true;
    if (n.name == "Introduction")
      found_intro = true;
    if (n.name == "Features")
      found_features = true;
    if (n.name == "Code Example")
      found_code_example = true;
    if (n.name == "[google] -> https://www.google.com")
      found_google = true;
    if (n.name == "[github] -> https://github.com")
      found_github = true;
    if (n.name == "cpp")
      found_cpp_code = true;
    if (n.name == "python")
      found_python_code = true;
  }
  CHECK(found_title);
  CHECK(found_intro);
  CHECK(found_features);
  CHECK(found_code_example);
  CHECK(found_google);
  CHECK(found_github);
  CHECK(found_cpp_code);
  CHECK(found_python_code);

  std::cout << "  [PASS] markdown_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_markdown_empty() {
  MarkdownExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.md", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] markdown_empty\n";
}

void test_rust_extractor() {
  RustExtractor extractor;
  std::string source = R"(
use std::collections::HashMap;

fn add(a: i32, b: i32) -> i32 {
    a + b
}

struct Calculator {
    value: i32,
}

impl Calculator {
    fn new(value: i32) -> Self {
        Calculator { value }
    }

    fn multiply(&self, a: i32, b: i32) -> i32 {
        self.value * a * b
    }
}

enum Color {
    Red,
    Green,
    Blue,
}

trait Greet {
    fn greet(&self) -> String;
}

mod utils {
    pub fn helper() -> i32 {
        42
    }
}
)";

  auto result = extractor.extract("/tmp/test.rs", source);
  CHECK(!result.nodes.empty());

  bool found_add = false, found_calculator = false, found_multiply = false;
  bool found_new = false, found_color = false, found_greet_trait = false;
  bool found_utils = false, found_helper = false;
  for (auto &n : result.nodes) {
    if (n.name == "add")
      found_add = true;
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "new")
      found_new = true;
    if (n.name == "Color")
      found_color = true;
    if (n.name == "Greet")
      found_greet_trait = true;
    if (n.name == "utils")
      found_utils = true;
    if (n.name == "helper")
      found_helper = true;
  }
  CHECK(found_add);
  CHECK(found_calculator);
  CHECK(found_multiply);
  CHECK(found_new);
  CHECK(found_color);
  CHECK(found_greet_trait);
  CHECK(found_utils);
  CHECK(found_helper);

  std::cout << "  [PASS] rust_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_rust_call_extraction() {
  RustExtractor extractor;
  std::string source = R"(
fn foo() {
    bar();
}

fn bar() {
    foo();
    baz(1, 2);
}

fn baz(a: i32, b: i32) -> i32 {
    a + b
}

struct Worker;

impl Worker {
    fn do_work(&self) {
        foo();
        self.internal_work();
    }

    fn internal_work(&self) {
        baz(1, 2);
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.rs", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  bool found_internal_work_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
    if (ref.ref_name == "internal_work")
      found_internal_work_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  CHECK(found_internal_work_call);
  std::cout << "  [PASS] rust_call_extraction\n";
}

void test_rust_member_call_extraction() {
  RustExtractor extractor;
  std::string source = R"(
struct Calculator;

impl Calculator {
    fn add(&self, a: i32, b: i32) -> i32 {
        a + b
    }

    fn compute(&self) -> i32 {
        self.add(1, 2) + self.add(3, 4)
    }
}

fn use_calculator() {
    let calc = Calculator;
    calc.compute();
}
)";

  auto result = extractor.extract("/tmp/test_member.rs", source);
  CHECK(!result.unresolved.empty());

  bool found_add_call = false, found_compute_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "add")
      found_add_call = true;
    if (ref.ref_name == "compute")
      found_compute_call = true;
  }
  CHECK(found_add_call);
  CHECK(found_compute_call);
  std::cout << "  [PASS] rust_member_call_extraction\n";
}

void test_bash_extractor() {
  BashExtractor extractor;
  std::string source = R"(
#!/usr/bin/env bash

function hello() {
    echo "Hello, world!"
}

greet() {
    local name="$1"
    echo "Hello, $name"
}

setup() {
    echo "Setting up..."
    hello
    greet "user"
}
)";

  auto result = extractor.extract("/tmp/test.sh", source);
  CHECK(!result.nodes.empty());

  bool found_hello = false, found_greet = false, found_setup = false;
  for (auto &n : result.nodes) {
    if (n.name == "hello")
      found_hello = true;
    if (n.name == "greet")
      found_greet = true;
    if (n.name == "setup")
      found_setup = true;
  }
  CHECK(found_hello);
  CHECK(found_greet);
  CHECK(found_setup);

  std::cout << "  [PASS] bash_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_bash_call_extraction() {
  BashExtractor extractor;
  std::string source = R"(
foo() {
    bar
}

bar() {
    foo
    baz "arg1"
}

baz() {
    echo "$1"
}
)";

  auto result = extractor.extract("/tmp/test_calls.sh", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] bash_call_extraction\n";
}

void test_bash_empty() {
  BashExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.sh", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] bash_empty\n";
}

void test_go_extractor() {
  GoExtractor extractor;
  std::string source = R"(
package main

import "fmt"

func add(a int, b int) int {
    return a + b
}

type Calculator struct {
    value int
}

func (c *Calculator) Multiply(a int, b int) int {
    return c.value * a * b
}

func (c Calculator) Greet(name string) string {
    return "Hello, " + name
}

func main() {
    result := add(1, 2)
    fmt.Println(result)

    calc := Calculator{value: 10}
    calc.Multiply(3, 4)
}
)";

  auto result = extractor.extract("/tmp/test.go", source);
  CHECK(!result.nodes.empty());

  bool found_add = false, found_calculator = false;
  bool found_multiply = false, found_greet = false, found_main = false;
  for (auto &n : result.nodes) {
    if (n.name == "add")
      found_add = true;
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "Multiply")
      found_multiply = true;
    if (n.name == "Greet")
      found_greet = true;
    if (n.name == "main")
      found_main = true;
  }
  CHECK(found_add);
  CHECK(found_calculator);
  CHECK(found_multiply);
  CHECK(found_greet);
  CHECK(found_main);

  std::cout << "  [PASS] go_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_go_call_extraction() {
  GoExtractor extractor;
  std::string source = R"(
package main

func foo() {
    bar()
}

func bar() {
    foo()
    baz(1, 2)
}

func baz(a int, b int) int {
    return a + b
}
)";

  auto result = extractor.extract("/tmp/test_calls.go", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] go_call_extraction\n";
}

void test_go_method_call_extraction() {
  GoExtractor extractor;
  std::string source = R"(
package main

type Calculator struct{}

func (c *Calculator) Add(a int, b int) int {
    return a + b
}

func (c *Calculator) Compute() int {
    return c.Add(1, 2) + c.Add(3, 4)
}
)";

  auto result = extractor.extract("/tmp/test_method.go", source);
  CHECK(!result.unresolved.empty());

  bool found_add_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "Add")
      found_add_call = true;
  }
  CHECK(found_add_call);
  std::cout << "  [PASS] go_method_call_extraction\n";
}

void test_go_empty() {
  GoExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.go", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] go_empty\n";
}

void test_java_extractor() {
  JavaExtractor extractor;
  std::string source = R"(
import java.util.List;

class Calculator {
    public int add(int a, int b) {
        return a + b;
    }

    public int multiply(int a, int b) {
        return a * b;
    }

    public void printResult(int value) {
        System.out.println(value);
    }
}

interface MathOperation {
    int operate(int a, int b);
}
)";

  auto result = extractor.extract("/tmp/test.java", source);
  CHECK(!result.nodes.empty());

  bool found_calculator = false, found_add = false, found_multiply = false;
  bool found_print_result = false, found_math_op = false;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "printResult")
      found_print_result = true;
    if (n.name == "MathOperation")
      found_math_op = true;
  }
  CHECK(found_calculator);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print_result);
  CHECK(found_math_op);

  std::cout << "  [PASS] java_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_java_call_extraction() {
  JavaExtractor extractor;
  std::string source = R"(
class Test {
    void foo() {
        bar();
    }

    void bar() {
        foo();
        baz(1, 2);
    }

    void baz(int a, int b) {
        System.out.println(a + b);
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.java", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] java_call_extraction\n";
}

void test_java_empty() {
  JavaExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.java", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] java_empty\n";
}

void test_kotlin_extractor() {
  KotlinExtractor extractor;
  std::string source = R"(
package com.example

import kotlin.math.max

class Calculator {
    fun add(a: Int, b: Int): Int {
        return a + b
    }

    fun multiply(a: Int, b: Int): Int {
        return a * b
    }

    fun printResult(value: Int) {
        println(value)
    }
}

interface MathOperation {
    fun operate(a: Int, b: Int): Int
}
)";

  auto result = extractor.extract("/tmp/test.kt", source);
  CHECK(!result.nodes.empty());

  bool found_calculator = false, found_add = false, found_multiply = false;
  bool found_print_result = false, found_math_op = false;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "printResult")
      found_print_result = true;
    if (n.name == "MathOperation")
      found_math_op = true;
  }
  CHECK(found_calculator);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print_result);
  CHECK(found_math_op);

  std::cout << "  [PASS] kotlin_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_kotlin_call_extraction() {
  KotlinExtractor extractor;
  std::string source = R"(
class Test {
    fun foo() {
        bar()
    }

    fun bar() {
        foo()
        baz(1, 2)
    }

    fun baz(a: Int, b: Int) {
        println(a + b)
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.kt", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] kotlin_call_extraction\n";
}

void test_kotlin_empty() {
  KotlinExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.kt", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] kotlin_empty\n";
}

void test_php_extractor() {
  PhpExtractor extractor;
  std::string source = R"(<?php
namespace App;

use App\Utils\Helper;

class Calculator {
    public function add(int $a, int $b): int {
        return $a + $b;
    }

    public function multiply(int $a, int $b): int {
        return $a * $b;
    }

    public function printResult(int $value): void {
        echo $value;
    }
}

interface MathOperation {
    public function operate(int $a, int $b): int;
}
)";

  auto result = extractor.extract("/tmp/test.php", source);
  CHECK(!result.nodes.empty());

  bool found_calculator = false, found_add = false, found_multiply = false;
  bool found_print_result = false, found_math_op = false;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "printResult")
      found_print_result = true;
    if (n.name == "MathOperation")
      found_math_op = true;
  }
  CHECK(found_calculator);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print_result);
  CHECK(found_math_op);

  std::cout << "  [PASS] php_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_php_call_extraction() {
  PhpExtractor extractor;
  std::string source = R"(<?php
class Test {
    public function foo() {
        $this->bar();
    }

    public function bar() {
        $this->foo();
        $this->baz(1, 2);
    }

    public function baz(int $a, int $b) {
        echo $a + $b;
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.php", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] php_call_extraction\n";
}

void test_php_empty() {
  PhpExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.php", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] php_empty\n";
}

void test_swift_extractor() {
  SwiftExtractor extractor;
  std::string source = R"(
import Foundation

class Calculator {
    func add(_ a: Int, _ b: Int) -> Int {
        return a + b
    }

    func multiply(_ a: Int, _ b: Int) -> Int {
        return a * b
    }

    func printResult(_ value: Int) {
        print(value)
    }
}

protocol MathOperation {
    func operate(_ a: Int, _ b: Int) -> Int
}
)";

  auto result = extractor.extract("/tmp/test.swift", source);
  CHECK(!result.nodes.empty());

  bool found_calculator = false, found_add = false, found_multiply = false;
  bool found_print_result = false, found_math_op = false;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_calculator = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "printResult")
      found_print_result = true;
    if (n.name == "MathOperation")
      found_math_op = true;
  }
  CHECK(found_calculator);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print_result);
  CHECK(found_math_op);

  std::cout << "  [PASS] swift_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_swift_call_extraction() {
  SwiftExtractor extractor;
  std::string source = R"(
class Test {
    func foo() {
        bar()
    }

    func bar() {
        foo()
        baz(1, 2)
    }

    func baz(_ a: Int, _ b: Int) {
        print(a + b)
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.swift", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] swift_call_extraction\n";
}

void test_swift_empty() {
  SwiftExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.swift", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] swift_empty\n";
}

void test_objc_extractor() {
  ObjcExtractor extractor;
  std::string source = R"(
#import <Foundation/Foundation.h>

@interface Calculator : NSObject

- (int)add:(int)a to:(int)b;
- (int)multiply:(int)a with:(int)b;

@end

@implementation Calculator

- (int)add:(int)a to:(int)b {
    return a + b;
}

- (int)multiply:(int)a with:(int)b {
    return a * b;
}

- (void)printResult:(int)value {
    NSLog(@"%d", value);
}

@end

@protocol MathOperation
- (int)operate:(int)a with:(int)b;
@end
)";

  auto result = extractor.extract("/tmp/test.m", source);
  CHECK(!result.nodes.empty());

  bool found_calc_interface = false, found_calc_impl = false;
  bool found_add = false, found_multiply = false;
  bool found_print_result = false, found_math_op = false;
  int calc_count = 0;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator") {
      calc_count++;
      if (n.kind == NodeKind::Class) {
        found_calc_interface = true;
        found_calc_impl = true;
      }
    }
    if (n.name == "add:to:")
      found_add = true;
    if (n.name == "multiply:with:")
      found_multiply = true;
    if (n.name == "printResult:")
      found_print_result = true;
    if (n.name == "MathOperation")
      found_math_op = true;
  }
  CHECK(found_calc_interface);
  CHECK(found_calc_impl);
  CHECK(calc_count >= 2);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print_result);
  CHECK(found_math_op);

  std::cout << "  [PASS] objc_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_objc_call_extraction() {
  ObjcExtractor extractor;
  std::string source = R"(
@implementation Test

- (void)foo {
    [self bar];
}

- (void)bar {
    [self foo];
    [self bazWithA:1 b:2];
}

- (void)bazWithA:(int)a b:(int)b {
    NSLog(@"%d", a + b);
}

@end
)";

  auto result = extractor.extract("/tmp/test_calls.m", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "bar")
      found_bar_call = true;
    if (ref.ref_name == "foo")
      found_foo_call = true;
    if (ref.ref_name == "bazWithA:b:")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] objc_call_extraction\n";
}

void test_objc_empty() {
  ObjcExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.m", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] objc_empty\n";
}

void test_csharp_extractor() {
  CSharpExtractor extractor;
  std::string source = R"(
using System;

namespace CalculatorApp {
    class Calculator {
        public int Add(int a, int b) {
            return a + b;
        }

        public int Multiply(int a, int b) {
            return a * b;
        }

        public Calculator() {
        }

        public void PrintResult(int value) {
            Console.WriteLine(value);
        }
    }

    interface IMathOperation {
        int Operate(int a, int b);
    }
}
)";

  auto result = extractor.extract("/tmp/test.cs", source);
  CHECK(!result.nodes.empty());

  bool found_calc = false, found_add = false, found_multiply = false;
  bool found_ctor = false, found_print = false, found_math_op = false;
  bool found_ns = false;
  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_calc = true;
    if (n.name == "Add")
      found_add = true;
    if (n.name == "Multiply")
      found_multiply = true;
    if (n.name == "PrintResult")
      found_print = true;
    if (n.name == "IMathOperation")
      found_math_op = true;
    if (n.name == "CalculatorApp")
      found_ns = true;
    if (n.name == "Calculator" && n.kind == NodeKind::Method)
      found_ctor = true;
  }
  CHECK(found_calc);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_print);
  CHECK(found_math_op);
  CHECK(found_ns);
  CHECK(found_ctor);

  std::cout << "  [PASS] csharp_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_csharp_call_extraction() {
  CSharpExtractor extractor;
  std::string source = R"(
class Test {
    public void Foo() {
        Bar();
    }

    public void Bar() {
        Foo();
        Baz(1, 2);
    }

    public void Baz(int a, int b) {
        Console.WriteLine(a + b);
    }
}
)";

  auto result = extractor.extract("/tmp/test_calls.cs", source);
  CHECK(!result.unresolved.empty());

  bool found_bar_call = false, found_foo_call = false, found_baz_call = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "Bar")
      found_bar_call = true;
    if (ref.ref_name == "Foo")
      found_foo_call = true;
    if (ref.ref_name == "Baz")
      found_baz_call = true;
  }
  CHECK(found_bar_call);
  CHECK(found_foo_call);
  CHECK(found_baz_call);
  std::cout << "  [PASS] csharp_call_extraction\n";
}

void test_csharp_empty() {
  CSharpExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.cs", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] csharp_empty\n";
}

void test_sql_extractor() {
  SqlExtractor extractor;
  std::string source = R"(
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT UNIQUE
);

CREATE VIEW active_users AS
SELECT * FROM users WHERE active = 1;

CREATE FUNCTION calculate_tax(amount DECIMAL) RETURNS DECIMAL
LANGUAGE SQL
AS $$
    SELECT amount * 0.08;
$$;
)";

  auto result = extractor.extract("/tmp/test.sql", source);
  CHECK(!result.nodes.empty());

  bool found_users = false, found_active_users = false;
  bool found_calc_tax = false;
  for (auto &n : result.nodes) {
    if (n.name == "users")
      found_users = true;
    if (n.name == "active_users")
      found_active_users = true;
    if (n.name == "calculate_tax")
      found_calc_tax = true;
  }
  CHECK(found_users);
  CHECK(found_active_users);
  CHECK(found_calc_tax);

  std::cout << "  [PASS] sql_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_sql_call_extraction() {
  SqlExtractor extractor;
  std::string source = R"(
CREATE FUNCTION my_sum(a INTEGER, b INTEGER) RETURNS INTEGER
LANGUAGE SQL
AS $$
    SELECT a + b;
$$;

CREATE FUNCTION call_others() RETURNS INTEGER
LANGUAGE SQL
AS $$
    SELECT my_sum(1, 2) + my_sum(3, 4);
$$;
)";

  auto result = extractor.extract("/tmp/test_calls.sql", source);
  CHECK(!result.unresolved.empty());

  bool found_my_sum = false;
  for (auto &ref : result.unresolved) {
    if (ref.ref_name == "my_sum")
      found_my_sum = true;
  }
  CHECK(found_my_sum);
  std::cout << "  [PASS] sql_call_extraction\n";
}

void test_sql_empty() {
  SqlExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.sql", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] sql_empty\n";
}

void test_lua_extractor() {
  LuaExtractor extractor;
  std::string source = R"(
function foo()
    local x = 1
end

function bar()
    foo()
end

local function baz()
    bar()
end
)";
  auto result = extractor.extract("/tmp/test.lua", source);
  CHECK(result.nodes.size() == 3);
  if (result.nodes.size() == 3) {
    CHECK(result.nodes[0].name == "foo");
    CHECK(result.nodes[0].kind == NodeKind::Function);
    CHECK(result.nodes[1].name == "bar");
    CHECK(result.nodes[1].kind == NodeKind::Function);
    CHECK(result.nodes[2].name == "baz");
    CHECK(result.nodes[2].kind == NodeKind::Function);
  }
  std::cout << "  [PASS] lua_extractor\n";
}

void test_lua_call_extraction() {
  LuaExtractor extractor;
  std::string source = R"(
function foo()
    bar()
    baz(1, 2)
end

function bar()
end

function baz(x, y)
end
)";
  auto result = extractor.extract("/tmp/test.lua", source);
  CHECK(!result.nodes.empty());
  CHECK(result.unresolved.size() >= 2);
  std::cout << "  [PASS] lua_call_extraction\n";
}

void test_lua_empty() {
  LuaExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.lua", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] lua_empty\n";
}

void test_yaml_extractor() {
  YamlExtractor extractor;
  std::string source = R"(
name: myapp
version: "1.0.0"
description: A sample app
dependencies:
  express: "^4.0.0"
  typescript: "^5.0.0"
scripts:
  build: tsc
  test: jest
)";
  auto result = extractor.extract("/tmp/test.yaml", source);
  CHECK(result.nodes.size() >= 4);
  if (result.nodes.size() >= 4) {
    CHECK(result.nodes[0].name == "name");
    CHECK(result.nodes[1].name == "version");
    CHECK(result.nodes[2].name == "description");
    CHECK(result.nodes[3].name == "dependencies");
  }
  std::cout << "  [PASS] yaml_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_yaml_empty() {
  YamlExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.yaml", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] yaml_empty\n";
}

void test_json_extractor() {
  JsonExtractor extractor;
  std::string source = R"({
  "name": "myapp",
  "version": "1.0.0",
  "scripts": {
    "build": "tsc",
    "test": "jest"
  }
})";
  auto result = extractor.extract("/tmp/test.json", source);
  CHECK(result.nodes.size() >= 3);
  if (result.nodes.size() >= 3) {
    CHECK(result.nodes[0].name == "\"name\"");
    CHECK(result.nodes[1].name == "\"version\"");
    CHECK(result.nodes[2].name == "\"scripts\"");
  }
  std::cout << "  [PASS] json_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_json_empty() {
  JsonExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.json", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] json_empty\n";
}

void test_xml_extractor() {
  XmlExtractor extractor;
  std::string source = R"(<?xml version="1.0"?>
<root>
  <item id="1">hello</item>
  <item id="2">world</item>
  <nested>
    <child>value</child>
  </nested>
</root>)";
  auto result = extractor.extract("/tmp/test.xml", source);
  CHECK(result.nodes.size() >= 3);
  if (result.nodes.size() >= 3) {
    CHECK(result.nodes[0].name == "root");
    CHECK(result.nodes[1].name == "item");
    CHECK(result.nodes[2].name == "item");
  }
  std::cout << "  [PASS] xml_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_xml_empty() {
  XmlExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.xml", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] xml_empty\n";
}

void test_html_extractor() {
  HtmlExtractor extractor;
  std::string source = R"(<!DOCTYPE html>
<html>
<head>
  <title>Test</title>
</head>
<body>
  <div class="main">
    <h1>Hello</h1>
    <p>World</p>
  </div>
</body>
</html>)";
  auto result = extractor.extract("/tmp/test.html", source);
  CHECK(result.nodes.size() >= 5);
  if (result.nodes.size() >= 5) {
    CHECK(result.nodes[0].name == "html");
    CHECK(result.nodes[1].name == "head");
    CHECK(result.nodes[2].name == "title");
    CHECK(result.nodes[3].name == "body");
    CHECK(result.nodes[4].name == "div");
  }
  std::cout << "  [PASS] html_extractor (" << result.nodes.size()
            << " nodes)\n";
}

void test_html_empty() {
  HtmlExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.html", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] html_empty\n";
}

void test_css_extractor() {
  CssExtractor extractor;
  std::string source = R"(
body {
  font-family: sans-serif;
  margin: 0;
  padding: 0;
}

h1, h2, h3 {
  color: #333;
}

.header {
  background: #f5f5f5;
}

#main-content {
  width: 100%;
}

@media (max-width: 768px) {
  .header {
    font-size: 14px;
  }
}

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}
)";
  auto result = extractor.extract("/tmp/test.css", source);
  CHECK(result.nodes.size() >= 6);

  bool found_body = false, found_h1_h2_h3 = false, found_header = false;
  bool found_main_content = false, found_media = false, found_keyframes = false;
  for (auto &n : result.nodes) {
    if (n.name == "body")
      found_body = true;
    if (n.name == "h1, h2, h3")
      found_h1_h2_h3 = true;
    if (n.name == ".header")
      found_header = true;
    if (n.name == "#main-content")
      found_main_content = true;
    if (n.name == "@media")
      found_media = true;
    if (n.name == "@keyframes")
      found_keyframes = true;
  }
  CHECK(found_body);
  CHECK(found_h1_h2_h3);
  CHECK(found_header);
  CHECK(found_main_content);
  CHECK(found_media);
  CHECK(found_keyframes);

  std::cout << "  [PASS] css_extractor (" << result.nodes.size() << " nodes)\n";
}

void test_css_empty() {
  CssExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.css", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] css_empty\n";
}

void test_zig_extractor() {
  ZigExtractor extractor;
  std::string source = R"(
const std = @import("std");

const Point = struct {
    x: f32,
    y: f32,
};

const Color = enum {
    Red,
    Green,
    Blue,
};

const Error = union {
    Io: std.fs.File.OpenError,
    Parse: ParseError,
};

fn add(a: i32, b: i32) i32 {
    return a + b;
}

fn multiply(a: i32, b: i32) i32 {
    const result = add(a, 0);
    return result * b;
}

fn main() void {
    const p = Point{ .x = 1.0, .y = 2.0 };
    const c = Color.Red;
    const sum = add(3, 4);
    const prod = multiply(sum, 2);
    std.debug.print("sum={}, prod={}", .{ sum, prod });
}
)";
  auto result = extractor.extract("/tmp/test.zig", source);
  CHECK(result.nodes.size() >= 6);

  bool found_std = false, found_Point = false, found_Color = false;
  bool found_Error = false, found_add = false, found_multiply = false;
  bool found_main = false, found_p = false, found_sum = false;
  for (auto &n : result.nodes) {
    if (n.name == "std")
      found_std = true;
    if (n.name == "Point")
      found_Point = true;
    if (n.name == "Color")
      found_Color = true;
    if (n.name == "Error")
      found_Error = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "multiply")
      found_multiply = true;
    if (n.name == "main")
      found_main = true;
    if (n.name == "p")
      found_p = true;
    if (n.name == "result")
      found_sum = true;
  }
  CHECK(found_std);
  CHECK(found_Point);
  CHECK(found_Color);
  CHECK(found_Error);
  CHECK(found_add);
  CHECK(found_multiply);
  CHECK(found_main);

  CHECK(result.unresolved.size() >= 2);

  std::cout << "  [PASS] zig_extractor (" << result.nodes.size() << " nodes, "
            << result.unresolved.size() << " refs)\n";
}

void test_zig_empty() {
  ZigExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.zig", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] zig_empty\n";
}

void test_ruby_extractor() {
  RubyExtractor extractor;
  std::string source = R"(
require "json"

module Calculator
  PI = 3.14159

  def add(a, b)
    a + b
  end

  def self.version
    "1.0.0"
  end
end

class Animal
  def speak
    puts "hello"
  end

  def eat(food)
    digest(food)
  end

  def digest(food)
    puts "digesting #{food}"
  end
end

class Dog < Animal
  def speak
    bark
  end

  def bark
    puts "woof"
    super
  end
end

def greet(name)
  puts "Hello, #{name}!"
end

x = 42
name = "Ruby"
)";
  auto result = extractor.extract("/tmp/test.rb", source);
  CHECK(result.nodes.size() >= 10);

  bool found_Calculator = false, found_Animal = false, found_Dog = false;
  bool found_add = false, found_version = false;
  bool found_speak = false, found_eat = false, found_digest = false;
  bool found_bark = false, found_greet = false;
  bool found_require = false, found_x = false, found_name = false;
  bool found_PI = false;

  for (auto &n : result.nodes) {
    if (n.name == "Calculator")
      found_Calculator = true;
    if (n.name == "Animal")
      found_Animal = true;
    if (n.name == "Dog")
      found_Dog = true;
    if (n.name == "add")
      found_add = true;
    if (n.name == "self.version")
      found_version = true;
    if (n.name == "speak")
      found_speak = true;
    if (n.name == "eat")
      found_eat = true;
    if (n.name == "digest")
      found_digest = true;
    if (n.name == "bark")
      found_bark = true;
    if (n.name == "greet")
      found_greet = true;
    if (n.name == "x")
      found_x = true;
    if (n.name == "name")
      found_name = true;
    if (n.name == "PI")
      found_PI = true;
    if (n.kind == NodeKind::Import)
      found_require = true;
  }
  CHECK(found_Calculator);
  CHECK(found_Animal);
  CHECK(found_Dog);
  CHECK(found_add);
  CHECK(found_version);
  CHECK(found_greet);
  CHECK(found_require);
  CHECK(found_x);

  CHECK(result.unresolved.size() >= 3);

  std::cout << "  [PASS] ruby_extractor (" << result.nodes.size() << " nodes, "
            << result.unresolved.size() << " refs)\n";
}

void test_ruby_empty() {
  RubyExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.rb", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] ruby_empty\n";
}

void test_glsl_extractor() {
  GlslExtractor extractor;
  std::string source = R"(
#version 450

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 uv;

uniform mat4 proj;
uniform mat4 view;

struct Light {
    vec3 position;
    float intensity;
};

vec3 lambertian(vec3 normal, vec3 light_dir, vec3 color) {
    float diff = max(dot(normal, light_dir), 0.0);
    return diff * color;
}

void main() {
    uv = in_uv;
    vec4 pos = vec4(in_position, 1.0);
    gl_Position = proj * view * pos;
    calculate_lighting();
}

float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}
)";
  auto result = extractor.extract("/tmp/test.glsl", source);
  CHECK(result.nodes.size() >= 8);

  bool found_Light = false, found_lambertian = false, found_main = false;
  bool found_random = false, found_common = false;
  bool found_proj = false, found_view = false;
  bool found_in_position = false;

  for (auto &n : result.nodes) {
    if (n.name == "Light")
      found_Light = true;
    if (n.name == "lambertian")
      found_lambertian = true;
    if (n.name == "main")
      found_main = true;
    if (n.name == "random")
      found_random = true;
    if (n.name == "common.glsl" || n.name.find("common") != std::string::npos)
      found_common = true;
    if (n.name == "proj")
      found_proj = true;
    if (n.name == "view")
      found_view = true;
    if (n.name == "in_position")
      found_in_position = true;
  }

  CHECK(found_Light);
  CHECK(found_lambertian);
  CHECK(found_main);
  CHECK(found_random);
  CHECK(found_common);
  CHECK(found_proj);
  CHECK(found_view);
  CHECK(found_in_position);

  bool found_calculate_lighting = false;
  for (auto &r : result.unresolved) {
    if (r.ref_name == "calculate_lighting")
      found_calculate_lighting = true;
  }
  CHECK(found_calculate_lighting);

  std::cout << "  [PASS] glsl_extractor (" << result.nodes.size() << " nodes, "
            << result.unresolved.size() << " refs)\n";
}

void test_glsl_empty() {
  GlslExtractor extractor;
  std::string source = "";
  auto result = extractor.extract("/tmp/empty.glsl", source);
  CHECK(result.nodes.empty());
  std::cout << "  [PASS] glsl_empty\n";
}

void test_impact_chain() {
  TempDb temp_db("impact.db");

  Database db(temp_db.path);
  db.init_schema();

  // A -> B -> C (改 C 会影响 A 和 B)
  Node a, b, c;
  a.kind = NodeKind::Function;
  a.name = "a";
  a.file_path = "/tmp/impact.cpp";
  a.language = "cpp";
  b.kind = NodeKind::Function;
  b.name = "b";
  b.file_path = "/tmp/impact.cpp";
  b.language = "cpp";
  c.kind = NodeKind::Function;
  c.name = "c";
  c.file_path = "/tmp/impact.cpp";
  c.language = "cpp";

  int64_t aid = db.insert_node(a);
  int64_t bid = db.insert_node(b);
  int64_t cid = db.insert_node(c);

  Edge e1;
  e1.source_id = aid;
  e1.target_id = bid;
  e1.kind = EdgeKind::Calls;
  db.insert_edge(e1);
  Edge e2;
  e2.source_id = bid;
  e2.target_id = cid;
  e2.kind = EdgeKind::Calls;
  db.insert_edge(e2);

  GraphTraverser traverser(db);

  // 改 C：影响 B 和 A
  auto impact = traverser.get_impact_chain(cid, 5);
  CHECK(impact.size() == 2);

  // 验证每个受影响节点都有反向路径
  // A → B → C，impact(C) = {A, B}
  // 反向路径：C → B（C 被 B 调用），C → B → A（B 被 A 调用）
  bool found_b = false, found_a = false;
  for (const auto &in : impact) {
    if (in.node.name == "b") {
      found_b = true;
      CHECK(in.path.size() == 2);
      CHECK(in.path[0] == cid); // 从 C 开始
      CHECK(in.path[1] == bid); // 到 B（B 调用了 C）
    }
    if (in.node.name == "a") {
      found_a = true;
      CHECK(in.path.size() == 3);
      CHECK(in.path[0] == cid); // 从 C 开始
      CHECK(in.path[2] == aid); // 到 A（A 调用了 B 调用了 C）
    }
  }
  CHECK(found_b);
  CHECK(found_a);

  std::cout << "  [PASS] impact_chain\n";
}

int main() {
  std::cout << "Running tests...\n";
  test_types();
  test_database();
  test_database_lookup_and_errors();
  test_cpp_extractor();
  test_cpp_member_call_extraction();
  test_run_git_diff_does_not_execute_shell();
  test_python_extractor();
  test_python_call_extraction();
  test_js_extractor();
  test_js_call_extraction();
  test_js_member_call_extraction();
  test_ts_extractor();
  test_ts_call_extraction();
  test_ts_member_call_extraction();
  test_tsx_extractor();
  test_detect_language();
  test_markdown_extractor();
  test_markdown_empty();
  test_rust_extractor();
  test_rust_call_extraction();
  test_rust_member_call_extraction();
  test_bash_extractor();
  test_bash_call_extraction();
  test_bash_empty();
  test_go_extractor();
  test_go_call_extraction();
  test_go_method_call_extraction();
  test_go_empty();
  test_java_extractor();
  test_java_call_extraction();
  test_java_empty();
  test_kotlin_extractor();
  test_kotlin_call_extraction();
  test_kotlin_empty();
  test_php_extractor();
  test_php_call_extraction();
  test_php_empty();
  test_swift_extractor();
  test_swift_call_extraction();
  test_swift_empty();
  test_objc_extractor();
  test_objc_call_extraction();
  test_objc_empty();
  test_csharp_extractor();
  test_csharp_call_extraction();
  test_csharp_empty();
  test_sql_extractor();
  test_sql_call_extraction();
  test_sql_empty();
  test_lua_extractor();
  test_lua_call_extraction();
  test_lua_empty();
  test_yaml_extractor();
  test_yaml_empty();
  test_json_extractor();
  test_json_empty();
  test_xml_extractor();
  test_xml_empty();
  test_html_extractor();
  test_html_empty();
  test_css_extractor();
  test_css_empty();
  test_zig_extractor();
  test_zig_empty();
  test_ruby_extractor();
  test_ruby_empty();
  test_glsl_extractor();
  test_glsl_empty();
  test_context_builder_splits_callers_and_callees();
  test_incremental_reindex();
  test_context_aware_same_name_resolution();
  test_tarjan_scc();
  test_find_path();
  test_compute_metrics();
  test_impact_chain();
  std::cout << "All tests passed!\n";
  return 0;
}