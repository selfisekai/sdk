// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <array>

#include "include/dart_api.h"
#include "include/dart_tools_api.h"
#include "platform/assert.h"
#include "vm/debugger_api_impl_test.h"
#include "vm/globals.h"
#include "vm/isolate.h"
#include "vm/kernel_loader.h"
#include "vm/lockers.h"
#include "vm/thread_barrier.h"
#include "vm/thread_pool.h"
#include "vm/unit_test.h"

namespace dart {

#if !defined(PRODUCT) && !defined(DART_PRECOMPILED_RUNTIME)

int64_t SimpleInvoke(Dart_Handle lib, const char* method) {
  Dart_Handle result = Dart_Invoke(lib, NewString(method), 0, NULL);
  EXPECT_VALID(result);
  EXPECT(Dart_IsInteger(result));
  int64_t integer_result = 0;
  result = Dart_IntegerToInt64(result, &integer_result);
  EXPECT_VALID(result);
  return integer_result;
}

const char* SimpleInvokeStr(Dart_Handle lib, const char* method) {
  Dart_Handle result = Dart_Invoke(lib, NewString(method), 0, NULL);
  const char* result_str = NULL;
  EXPECT(Dart_IsString(result));
  EXPECT_VALID(Dart_StringToCString(result, &result_str));
  return result_str;
}

Dart_Handle SimpleInvokeError(Dart_Handle lib, const char* method) {
  Dart_Handle result = Dart_Invoke(lib, NewString(method), 0, NULL);
  EXPECT(Dart_IsError(result));
  return result;
}

TEST_CASE(IsolateReload_FunctionReplacement) {
  const char* kScript =
      "main() {\n"
      "  return 4;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "var _unused;"
      "main() {\n"
      "  return 10;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(10, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_IncrementalCompile) {
  const char* kScriptChars =
      "main() {\n"
      "  return 42;\n"
      "}\n";
  Dart_Handle lib = TestCase::LoadTestScript(kScriptChars, NULL);
  EXPECT_VALID(lib);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  int64_t value = 0;
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(42, value);

  const char* kUpdatedScriptChars =
      "main() {\n"
      "  return 24;\n"
      "}\n"
      "";
  lib = TestCase::ReloadTestScript(kUpdatedScriptChars);
  EXPECT_VALID(lib);
  result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(24, value);
}

TEST_CASE(IsolateReload_KernelIncrementalCompile) {
  // clang-format off
  Dart_SourceFile sourcefiles[] = {
    {
      "file:///test-app",
      "main() {\n"
      "  return 42;\n"
      "}\n",
    }};
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScriptWithDFE(
      sizeof(sourcefiles) / sizeof(Dart_SourceFile), sourcefiles,
      NULL /* resolver */, true /* finalize */, true /* incrementally */);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  int64_t value = 0;
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(42, value);

  // clang-format off
  Dart_SourceFile updated_sourcefiles[] = {
    {
      "file:///test-app",
      "main() {\n"
      "  return 24;\n"
      "}\n"
      ""
    }};
  // clang-format on
  {
    const uint8_t* kernel_buffer = NULL;
    intptr_t kernel_buffer_size = 0;
    char* error = TestCase::CompileTestScriptWithDFE(
        "file:///test-app",
        sizeof(updated_sourcefiles) / sizeof(Dart_SourceFile),
        updated_sourcefiles, &kernel_buffer, &kernel_buffer_size,
        true /* incrementally */);
    EXPECT(error == NULL);
    EXPECT_NOTNULL(kernel_buffer);

    lib = TestCase::ReloadTestKernel(kernel_buffer, kernel_buffer_size);
    EXPECT_VALID(lib);
  }
  result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(24, value);
}

TEST_CASE(IsolateReload_KernelIncrementalCompileAppAndLib) {
  // clang-format off
  Dart_SourceFile sourcefiles[] = {
    {
      "file:///test-app.dart",
      "import 'test-lib.dart';\n"
      "main() {\n"
      "  return WhatsTheMeaningOfAllThis();\n"
      "}\n",
    },
    {
      "file:///test-lib.dart",
      "WhatsTheMeaningOfAllThis() {\n"
      "  return 42;\n"
      "}\n",
    }};
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScriptWithDFE(
      sizeof(sourcefiles) / sizeof(Dart_SourceFile), sourcefiles,
      NULL /* resolver */, true /* finalize */, true /* incrementally */);
  EXPECT_VALID(lib);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  int64_t value = 0;
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(42, value);

  // clang-format off
  Dart_SourceFile updated_sourcefiles[] = {
    {
      "file:///test-lib.dart",
      "WhatsTheMeaningOfAllThis() {\n"
      "  return 24;\n"
      "}\n"
      ""
    }};
  // clang-format on

  {
    const uint8_t* kernel_buffer = NULL;
    intptr_t kernel_buffer_size = 0;
    char* error = TestCase::CompileTestScriptWithDFE(
        "file:///test-app.dart",
        sizeof(updated_sourcefiles) / sizeof(Dart_SourceFile),
        updated_sourcefiles, &kernel_buffer, &kernel_buffer_size,
        true /* incrementally */);
    EXPECT(error == NULL);
    EXPECT_NOTNULL(kernel_buffer);

    lib = TestCase::ReloadTestKernel(kernel_buffer, kernel_buffer_size);
    EXPECT_VALID(lib);
  }
  result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(24, value);
}

TEST_CASE(IsolateReload_KernelIncrementalCompileGenerics) {
  // clang-format off
  Dart_SourceFile sourcefiles[] = {
    {
      "file:///test-app.dart",
      "import 'test-lib.dart';\n"
      "class Account {\n"
      "  int balance() => 42;\n"
      "}\n"
      "class MyAccountState extends State<Account> {\n"
      "  MyAccountState(Account a): super(a) {}\n"
      "}\n"
      "main() {\n"
      "  return (new MyAccountState(new Account()))\n"
      "      .howAreTheThings().balance();\n"
      "}\n",
    },
    {
      "file:///test-lib.dart",
      "class State<T> {\n"
      "  T t;"
      "  State(this.t);\n"
      "  T howAreTheThings() => t;\n"
      "}\n",
    }};
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScriptWithDFE(
      sizeof(sourcefiles) / sizeof(Dart_SourceFile), sourcefiles,
      NULL /* resolver */, true /* finalize */, true /* incrementally */);
  EXPECT_VALID(lib);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  int64_t value = 0;
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(42, value);

  // clang-format off
  Dart_SourceFile updated_sourcefiles[] = {
    {
      "file:///test-app.dart",
      "import 'test-lib.dart';\n"
      "class Account {\n"
      "  int balance() => 24;\n"
      "}\n"
      "class MyAccountState extends State<Account> {\n"
      "  MyAccountState(Account a): super(a) {}\n"
      "}\n"
      "main() {\n"
      "  return (new MyAccountState(new Account()))\n"
      "      .howAreTheThings().balance();\n"
      "}\n",
    }};
  // clang-format on
  {
    const uint8_t* kernel_buffer = NULL;
    intptr_t kernel_buffer_size = 0;
    char* error = TestCase::CompileTestScriptWithDFE(
        "file:///test-app.dart",
        sizeof(updated_sourcefiles) / sizeof(Dart_SourceFile),
        updated_sourcefiles, &kernel_buffer, &kernel_buffer_size,
        true /* incrementally */);
    EXPECT(error == NULL);
    EXPECT_NOTNULL(kernel_buffer);

    lib = TestCase::ReloadTestKernel(kernel_buffer, kernel_buffer_size);
    EXPECT_VALID(lib);
  }
  result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(24, value);
}

TEST_CASE(IsolateReload_KernelIncrementalCompileBaseClass) {
  const char* nullable_tag = TestCase::NullableTag();
  // clang-format off
  auto kSourceFile1 =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class State<T, U> {\n"
                                "  T%s t;\n"
                                "  U%s u;\n"
                                "  State(List l) {\n"
                                "    t = l[0] is T ? l[0] : null;\n"
                                "    u = l[1] is U ? l[1] : null;\n"
                                "  }\n"
                                "}\n",
                                nullable_tag, nullable_tag),
                              std::free);
  Dart_SourceFile sourcefiles[3] = {
      {
          "file:///test-app.dart",
          "import 'test-util.dart';\n"
          "main() {\n"
          "  var v = doWork();"
          "  return v == 42 ? 1 : v == null ? -1 : 0;\n"
          "}\n",
      },
      {
          "file:///test-lib.dart",
          kSourceFile1.get()
      },
      {
        "file:///test-util.dart",
        "import 'test-lib.dart';\n"
        "class MyAccountState extends State<int, String> {\n"
        "  MyAccountState(List l): super(l) {}\n"
        "  first() => t;\n"
        "}\n"
        "doWork() => new MyAccountState(<dynamic>[42, 'abc']).first();\n"
      }
  };
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScriptWithDFE(
      sizeof(sourcefiles) / sizeof(Dart_SourceFile), sourcefiles,
      NULL /* resolver */, true /* finalize */, true /* incrementally */);
  EXPECT_VALID(lib);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  int64_t value = 0;
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(1, value);

  auto kUpdatedSourceFile =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "class State<U, T> {\n"
                                          "  T%s t;\n"
                                          "  U%s u;\n"
                                          "  State(List l) {\n"
                                          "    t = l[0] is T ? l[0] : null;\n"
                                          "    u = l[1] is U ? l[1] : null;\n"
                                          "  }\n"
                                          "}\n",
                                          nullable_tag, nullable_tag),
                              std::free);
  Dart_SourceFile updated_sourcefiles[1] = {{
      "file:///test-lib.dart",
      kUpdatedSourceFile.get(),
  }};
  {
    const uint8_t* kernel_buffer = NULL;
    intptr_t kernel_buffer_size = 0;
    char* error = TestCase::CompileTestScriptWithDFE(
        "file:///test-app.dart",
        sizeof(updated_sourcefiles) / sizeof(Dart_SourceFile),
        updated_sourcefiles, &kernel_buffer, &kernel_buffer_size,
        true /* incrementally */);
    EXPECT(error == NULL);
    EXPECT_NOTNULL(kernel_buffer);

    lib = TestCase::ReloadTestKernel(kernel_buffer, kernel_buffer_size);
    EXPECT_VALID(lib);
  }
  result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  result = Dart_IntegerToInt64(result, &value);
  EXPECT_VALID(result);
  EXPECT_EQ(-1, value);
}

TEST_CASE(IsolateReload_BadClass) {
  const char* kScript =
      "class Foo {\n"
      "  final a;\n"
      "  Foo(this.a);\n"
      "}\n"
      "main() {\n"
      "  new Foo(5);\n"
      "  return 4;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "var _unused;"
      "class Foo {\n"
      "  final a kjsdf ksjdf ;\n"
      "  Foo(this.a);\n"
      "}\n"
      "main() {\n"
      "  new Foo(5);\n"
      "  return 10;\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(result, "Expected ';' after this");
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_StaticValuePreserved) {
  const char* kScript =
      "init() => 'old value';\n"
      "var value = init();\n"
      "main() {\n"
      "  return 'init()=${init()},value=${value}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("init()=old value,value=old value",
               SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "var _unused;"
      "init() => 'new value';\n"
      "var value = init();\n"
      "main() {\n"
      "  return 'init()=${init()},value=${value}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("init()=new value,value=old value",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_SavedClosure) {
  // Create a closure in main which only exists in the original source.
  const char* kScript =
      "magic() {\n"
      "  var x = 'ante';\n"
      "  return x + 'diluvian';\n"
      "}\n"
      "var closure;\n"
      "main() {\n"
      "  closure = () { return magic().toString() + '!'; };\n"
      "  return closure();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("antediluvian!", SimpleInvokeStr(lib, "main"));

  // Remove the original closure from the source code.  The closure is
  // able to be recompiled because its source is preserved in a
  // special patch class.
  const char* kReloadScript =
      "magic() {\n"
      "  return 'postapocalyptic';\n"
      "}\n"
      "var closure;\n"
      "main() {\n"
      "  return closure();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("postapocalyptic!", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TopLevelFieldAdded) {
  const char* kScript =
      "var value1 = 10;\n"
      "main() {\n"
      "  return 'value1=${value1}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value1=10", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "var value1 = 10;\n"
      "var value2 = 20;\n"
      "main() {\n"
      "  return 'value1=${value1},value2=${value2}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value1=10,value2=20", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ClassFieldAdded) {
  const char* kScript =
      "class Foo {\n"
      "  var x;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var x;\n"
      "  var y;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_ClassFieldAdded2) {
  const char* kScript =
      "class Foo {\n"
      "  var x;\n"
      "  var y;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var x;\n"
      "  var y;\n"
      "  var z;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_ClassFieldRemoved) {
  const char* kScript =
      "class Foo {\n"
      "  var x;\n"
      "  var y;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var x;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 44;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_ClassAdded) {
  const char* kScript =
      "main() {\n"
      "  return 'hello';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("hello", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "var _unused;"
      "class A {\n"
      "  toString() => 'hello from A';\n"
      "}\n"
      "main() {\n"
      "  return new A().toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("hello from A", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ClassRemoved) {
  const char* kScript =
      "class A {\n"
      "  toString() => 'hello from A';\n"
      "}\n"
      "List<dynamic> list = <dynamic>[];"
      "main() {\n"
      "  list.add(new A());\n"
      "  return list[0].toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("hello from A", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "List<dynamic> list = <dynamic>[];\n"
      "main() {\n"
      "  return list[0].toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("hello from A", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_LibraryImportAdded) {
  const char* kScript =
      "main() {\n"
      "  return max(3, 4);\n"
      "}\n";

  const char* kReloadScript =
      "import 'dart:math';\n"
      "main() {\n"
      "  return max(3, 4);\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScriptWithErrors(kScript);
  EXPECT_VALID(lib);
  EXPECT_ERROR(SimpleInvokeError(lib, "main"), "max");

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_LibraryImportRemoved) {
  const char* kScript =
      "import 'dart:math';\n"
      "main() {\n"
      "  return max(3, 4);\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "main() {\n"
      "  return max(3, 4);\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(SimpleInvokeError(lib, "main"), "max");
}

TEST_CASE(IsolateReload_LibraryDebuggable) {
  const char* kScript =
      "main() {\n"
      "  return 1;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  // The library is by default debuggable.  Make it not debuggable.
  intptr_t lib_id = -1;
  bool debuggable = false;
  EXPECT_VALID(Dart_LibraryId(lib, &lib_id));
  EXPECT_VALID(Dart_GetLibraryDebuggable(lib_id, &debuggable));
  EXPECT_EQ(true, debuggable);
  EXPECT_VALID(Dart_SetLibraryDebuggable(lib_id, false));
  EXPECT_VALID(Dart_GetLibraryDebuggable(lib_id, &debuggable));
  EXPECT_EQ(false, debuggable);

  EXPECT_EQ(1, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "main() {\n"
      "  return 2;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);

  EXPECT_EQ(2, SimpleInvoke(lib, "main"));

  // Library debuggability is preserved.
  intptr_t new_lib_id = -1;
  EXPECT_VALID(Dart_LibraryId(lib, &new_lib_id));
  EXPECT_VALID(Dart_GetLibraryDebuggable(new_lib_id, &debuggable));
  EXPECT_EQ(false, debuggable);
}

TEST_CASE(IsolateReload_ImplicitConstructorChanged) {
  // Note that we are checking that the value 20 gets cleared from the
  // compile-time constants cache.  To make this test work, "20" and
  // "10" need to be at the same token position.
  const char* kScript =
      "class A {\n"
      "  int field = 20;\n"
      "}\n"
      "var savedA = new A();\n"
      "main() {\n"
      "  var newA = new A();\n"
      "  return 'saved:${savedA.field} new:${newA.field}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("saved:20 new:20", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class A {\n"
      "  int field = 10;\n"
      "}\n"
      "var savedA = new A();\n"
      "main() {\n"
      "  var newA = new A();\n"
      "  return 'saved:${savedA.field} new:${newA.field}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("saved:20 new:10", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ConstructorChanged) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class A {\n"
                  "  %s int field;\n"
                  "  A() { field = 20; }\n"
                  "}\n"
                  "var savedA = A();\n"
                  "main() {\n"
                  "  var newA = A();\n"
                  "  return 'saved:${savedA.field} new:${newA.field}';\n"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("saved:20 new:20", SimpleInvokeStr(lib, "main"));

  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "var _unused;"
                  "class A {\n"
                  "  %s int field;\n"
                  "  A() { field = 10; }\n"
                  "}\n"
                  "var savedA = A();\n"
                  "main() {\n"
                  "  var newA = A();\n"
                  "  return 'saved:${savedA.field} new:${newA.field}';\n"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ("saved:20 new:10", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_SuperClassChanged) {
  const char* kScript =
      "class A {\n"
      "}\n"
      "class B extends A {\n"
      "}\n"
      "var list = [ new A(), new B() ];\n"
      "main() {\n"
      "  return (list.map((x) => '${x is A}/${x is B}')).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("(true/false, true/true)", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "var _unused;"
      "class B{\n"
      "}\n"
      "class A extends B {\n"
      "}\n"
      "var list = [ new A(), new B() ];\n"
      "main() {\n"
      "  return (list.map((x) => '${x is A}/${x is B}')).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("(true/true, false/true)", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_Generics) {
  // Reload a program with generics without changing the source.  We
  // do this to produce duplication TypeArguments and make sure that
  // the system doesn't die.
  const char* kScript =
      "class A {\n"
      "}\n"
      "class B<T extends A> {\n"
      "}\n"
      "main() {\n"
      "  return new B<A>().toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Instance of 'B<A>'", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class A {\n"
      "}\n"
      "class B<T extends A> {\n"
      "}\n"
      "main() {\n"
      "  return new B<A>().toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Instance of 'B<A>'", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TypeIdentity) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class T { }\n"
      "getType() => T;\n"
      "main() {\n"
      "  var oldType = getType();\n"
      "  reloadTest();\n"
      "  var newType = getType();\n"
      "  return identical(oldType, newType).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class T extends Stopwatch { }\n"
      "getType() => T;\n"
      "main() {\n"
      "  var oldType = getType();\n"
      "  reloadTest();\n"
      "  var newType = getType();\n"
      "  return identical(oldType, newType).toString();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TypeIdentityGeneric) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class T<G> { }\n"
      "getType() => new T<int>().runtimeType;\n"
      "main() {\n"
      "  var oldType = getType();\n"
      "  reloadTest();\n"
      "  var newType = getType();\n"
      "  return identical(oldType, newType).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class T<G> extends Stopwatch { }\n"
      "getType() => new T<int>().runtimeType;\n"
      "main() {\n"
      "  var oldType = getType();\n"
      "  reloadTest();\n"
      "  var newType = getType();\n"
      "  return identical(oldType, newType).toString();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TypeIdentityParameter) {
  const char* kScript =
      "import 'dart:mirrors';\n"
      "import 'file:///test:isolate_reload_helper';\n"
      "class T<G> { }\n"
      "getTypeVar() => reflectType(T).typeVariables[0];\n"
      "main() {\n"
      "  var oldType = getTypeVar();\n"
      "  reloadTest();\n"
      "  var newType = getTypeVar();\n"
      "  return (oldType == newType).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'dart:mirrors';\n"
      "import 'file:///test:isolate_reload_helper';\n"
      "class T<G> extends Stopwatch { }\n"
      "getTypeVar() => reflectType(T).typeVariables[0];\n"
      "main() {\n"
      "  var oldType = getTypeVar();\n"
      "  reloadTest();\n"
      "  var newType = getTypeVar();\n"
      "  return (oldType == newType).toString();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_MixinChanged) {
  const char* kScript =
      "mixin Mixin1 {\n"
      "  var field = 'mixin1';\n"
      "  func() => 'mixin1';\n"
      "}\n"
      "class B extends Object with Mixin1 {\n"
      "}\n"
      "var saved = new B();\n"
      "main() {\n"
      "  return 'saved:field=${saved.field},func=${saved.func()}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("saved:field=mixin1,func=mixin1", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "mixin Mixin2 {\n"
      "  var field = 'mixin2';\n"
      "  func() => 'mixin2';\n"
      "}\n"
      "class B extends Object with Mixin2 {\n"
      "}\n"
      "var saved = new B();\n"
      "main() {\n"
      "  var newer = new B();\n"
      "  return 'saved:field=${saved.field},func=${saved.func()} '\n"
      "         'newer:field=${newer.field},func=${newer.func()}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);

  // The saved instance of B retains its old field value from mixin1,
  // but it gets the new implementation of func from mixin2.
  EXPECT_STREQ(
      "saved:field=mixin1,func=mixin2 "
      "newer:field=mixin2,func=mixin2",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ComplexInheritanceChange) {
  const char* kScript =
      "class A {\n"
      "  String name;\n"
      "  A(this.name);\n"
      "}\n"
      "class B extends A {\n"
      "  B(name) : super(name);\n"
      "}\n"
      "class C extends B {\n"
      "  C(name) : super(name);\n"
      "}\n"
      "var list = <dynamic>[ new A('a'), new B('b'), new C('c') ];\n"
      "main() {\n"
      "  return (list.map((x) {\n"
      "    return '${x.name} is A(${x is A})/ B(${x is B})/ C(${x is C})';\n"
      "  })).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "(a is A(true)/ B(false)/ C(false),"
      " b is A(true)/ B(true)/ C(false),"
      " c is A(true)/ B(true)/ C(true))",
      SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class C {\n"
      "  String name;\n"
      "  C(this.name);\n"
      "}\n"
      "class X extends C {\n"
      "  X(name) : super(name);\n"
      "}\n"
      "class A extends X {\n"
      "  A(name) : super(name);\n"
      "}\n"
      "var list;\n"
      "main() {\n"
      "  list.add(new X('x'));\n"
      "  return (list.map((x) {\n"
      "    return '${x.name} is A(${x is A})/ C(${x is C})/ X(${x is X})';\n"
      "  })).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "(a is A(true)/ C(true)/ X(true),"
      " b is A(true)/ C(true)/ X(true),"  // still extends A...
      " c is A(false)/ C(true)/ X(false),"
      " x is A(false)/ C(true)/ X(true))",
      SimpleInvokeStr(lib, "main"));

  // Revive the class B and make sure all allocated instances take
  // their place in the inheritance hierarchy.
  const char* kReloadScript2 =
      "class X {\n"
      "  String name;\n"
      "  X(this.name);\n"
      "}\n"
      "class A extends X{\n"
      "  A(name) : super(name);\n"
      "}\n"
      "class B extends X {\n"
      "  B(name) : super(name);\n"
      "}\n"
      "class C extends A {\n"
      "  C(name) : super(name);\n"
      "}\n"
      "var list;\n"
      "main() {\n"
      "  return (list.map((x) {\n"
      "    return '${x.name} is '\n"
      "           'A(${x is A})/ B(${x is B})/ C(${x is C})/ X(${x is X})';\n"
      "  })).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript2);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "(a is A(true)/ B(false)/ C(false)/ X(true),"
      " b is A(false)/ B(true)/ C(false)/ X(true),"
      " c is A(true)/ B(false)/ C(true)/ X(true),"
      " x is A(false)/ B(false)/ C(false)/ X(true))",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_LiveStack) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "helper() => 7;\n"
      "alpha() { var x = helper(); reloadTest(); return x + helper(); }\n"
      "foo() => alpha();\n"
      "bar() => foo();\n"
      "main() {\n"
      "  return bar();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "helper() => 100;\n"
      "alpha() => 5 + helper();\n"
      "foo() => alpha();\n"
      "bar() => foo();\n"
      "main() {\n"
      "  return bar();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_EQ(107, SimpleInvoke(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_EQ(105, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_LibraryLookup) {
  const char* kImportScript = "importedFunc() => 'a';\n";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "main() {\n"
      "  return 'b';\n"
      "}\n";
  Dart_Handle result;
  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("b", SimpleInvokeStr(lib, "main"));

  // Fail to find 'test:lib1' in the isolate.
  result = Dart_LookupLibrary(NewString("test:lib1"));
  EXPECT(Dart_IsError(result));

  const char* kReloadScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc();\n"
      "}\n";

  // Reload and add 'test:lib1' to isolate.
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("a", SimpleInvokeStr(lib, "main"));

  // Find 'test:lib1' in the isolate.
  result = Dart_LookupLibrary(NewString("test:lib1"));
  EXPECT(Dart_IsLibrary(result));

  // Reload and remove 'test:lib1' from isolate.
  lib = TestCase::ReloadTestScript(kScript);
  EXPECT_VALID(lib);

  // Fail to find 'test:lib1' in the isolate.
  result = Dart_LookupLibrary(NewString("test:lib1"));
  EXPECT(Dart_IsError(result));
}

TEST_CASE(IsolateReload_LibraryHide) {
  const char* kImportScript = "importedFunc() => 'a';\n";
  TestCase::AddTestLib("test:lib1", kImportScript);

  // Import 'test:lib1' with importedFunc hidden. Will result in an
  // error.
  const char* kScript =
      "import 'test:lib1' hide importedFunc;\n"
      "main() {\n"
      "  return importedFunc();\n"
      "}\n";

  // Dart_Handle result;

  Dart_Handle lib = TestCase::LoadTestScriptWithErrors(kScript);
  EXPECT_VALID(lib);
  EXPECT_ERROR(SimpleInvokeError(lib, "main"), "importedFunc");

  // Import 'test:lib1'.
  const char* kReloadScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("a", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_LibraryShow) {
  const char* kImportScript =
      "importedFunc() => 'a';\n"
      "importedIntFunc() => 4;\n";
  TestCase::AddTestLib("test:lib1", kImportScript);

  // Import 'test:lib1' with importedIntFunc visible. Will result in
  // an error when 'main' is invoked.
  const char* kScript =
      "import 'test:lib1' show importedIntFunc;\n"
      "main() {\n"
      "  return importedFunc();\n"
      "}\n"
      "mainInt() {\n"
      "  return importedIntFunc();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScriptWithErrors(kScript);
  EXPECT_VALID(lib);

  // Works.
  EXPECT_EQ(4, SimpleInvoke(lib, "mainInt"));
  // Results in an error.
  EXPECT_ERROR(SimpleInvokeError(lib, "main"), "importedFunc");

  // Import 'test:lib1' with importedFunc visible. Will result in
  // an error when 'mainInt' is invoked.
  const char* kReloadScript =
      "import 'test:lib1' show importedFunc;\n"
      "main() {\n"
      "  return importedFunc();\n"
      "}\n"
      "mainInt() {\n"
      "  return importedIntFunc();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib, "importedIntFunc");
}

// Verifies that we clear the ICs for the functions live on the stack in a way
// that is compatible with the fast path smi stubs.
TEST_CASE(IsolateReload_SmiFastPathStubs) {
  const char* kImportScript = "importedIntFunc() => 4;\n";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "import 'test:lib1' show importedIntFunc;\n"
      "main() {\n"
      "  var x = importedIntFunc();\n"
      "  var y = importedIntFunc();\n"
      "  reloadTest();\n"
      "  return x + y;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  // Identity reload.
  EXPECT_VALID(TestCase::SetReloadTestScript(kScript));

  EXPECT_EQ(8, SimpleInvoke(lib, "main"));
}

// Verifies that we assign the correct patch classes for imported
// mixins when we reload.
TEST_CASE(IsolateReload_ImportedMixinFunction) {
  const char* kImportScript =
      "mixin ImportedMixin {\n"
      "  mixinFunc() => 'mixin';\n"
      "}\n";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'test:lib1' show ImportedMixin;\n"
      "class A extends Object with ImportedMixin {\n"
      "}"
      "var func = new A().mixinFunc;\n"
      "main() {\n"
      "  return func();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  EXPECT_STREQ("mixin", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "import 'test:lib1' show ImportedMixin;\n"
      "class A extends Object with ImportedMixin {\n"
      "}"
      "var func;\n"
      "main() {\n"
      "  return func();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("mixin", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TopLevelParseError) {
  const char* kScript =
      "main() {\n"
      "  return 4;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "kjsadkfjaksldfjklsadf;\n"
      "main() {\n"
      "  return 4;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib,
               "Variables must be declared using the keywords"
               " 'const', 'final', 'var' or a type name.");
}

TEST_CASE(IsolateReload_PendingUnqualifiedCall_StaticToInstance) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'static';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'instance';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  const char* expected = "instance";
  const char* result = SimpleInvokeStr(lib, "main");
  EXPECT_STREQ(expected, result);

  // Bail out if we've already failed so we don't crash in the tag handler.
  if ((result == NULL) || (strcmp(expected, result) != 0)) {
    return;
  }

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_STREQ(expected, SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PendingUnqualifiedCall_InstanceToStatic) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'instance';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'static';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));
  const char* expected = "static";
  const char* result = SimpleInvokeStr(lib, "main");
  EXPECT_NOTNULL(result);
  // Bail out if we've already failed so we don't crash in StringEquals.
  if (result == NULL) {
    return;
  }
  EXPECT_STREQ(expected, result);

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_STREQ(expected, SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PendingConstructorCall_AbstractToConcrete) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "abstract class Foo {}\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    new C().test();\n"
      "    return 'okay';\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class Foo {}\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return new Foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    new C().test();\n"
      "    return 'okay';\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  const char* expected = "okay";
  const char* result = SimpleInvokeStr(lib, "main");
  EXPECT_STREQ(expected, result);

  // Bail out if we've already failed so we don't crash in the tag handler.
  if ((result == NULL) || (strcmp(expected, result) != 0)) {
    return;
  }

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_STREQ(expected, SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PendingConstructorCall_ConcreteToAbstract) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class Foo {}\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return new Foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    new C().test();\n"
      "    return 'okay';\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "abstract class Foo {}\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return new Foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    new C().test();\n"
      "    return 'okay';\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));
  EXPECT_ERROR(SimpleInvokeError(lib, "main"), "is abstract");
}

TEST_CASE(IsolateReload_PendingStaticCall_DefinedToNSM) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'static';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return C.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    return new C().test();\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return C.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    return new C().test();\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));
  const char* expected = "exception";
  const char* result = SimpleInvokeStr(lib, "main");
  EXPECT_NOTNULL(result);

  // Bail out if we've already failed so we don't crash in StringEquals.
  if (result == NULL) {
    return;
  }
  EXPECT_STREQ(expected, result);

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_STREQ(expected, SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PendingStaticCall_NSMToDefined) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return C.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    return new C().test();\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'static';\n"
      "  test() {\n"
      "    reloadTest();\n"
      "    return C.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  try {\n"
      "    return new C().test();\n"
      "  } catch (e) {\n"
      "    return 'exception';\n"
      "  }\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  const char* expected = "static";
  const char* result = SimpleInvokeStr(lib, "main");

  // Bail out if we've already failed so we don't crash in the tag handler.
  if (result == NULL) {
    return;
  }
  EXPECT_STREQ(expected, result);

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
  EXPECT_STREQ(expected, SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PendingSuperCall) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class S {\n"
      "  foo() => 1;\n"
      "}\n"
      "class C extends S {\n"
      "  foo() => 100;\n"
      "  test() {\n"
      "    var n = super.foo();\n"
      "    reloadTest();\n"
      "    return n + super.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class S {\n"
      "  foo() => 10;\n"
      "}\n"
      "class C extends S {\n"
      "  foo() => 100;\n"
      "  test() {\n"
      "    var n = super.foo();\n"
      "    reloadTest();\n"
      "    return n + super.foo();\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  return new C().test();\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_EQ(11, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_TearOff_Instance_Equality) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'old';\n"
      "}\n"
      "main() {\n"
      "  var c = new C();\n"
      "  var f1 = c.foo;\n"
      "  reloadTest();\n"
      "  var f2 = c.foo;\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'new';\n"
      "}\n"
      "main() {\n"
      "  var c = new C();\n"
      "  var f1 = c.foo;\n"
      "  reloadTest();\n"
      "  var f2 = c.foo;\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("new new true false", SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_Parameter_Count_Mismatch) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'old';\n"
      "}\n"
      "main() {\n"
      "  var f1 = C.foo;\n"
      "  reloadTest();\n"
      "  return f1();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo(i) => 'new:$i';\n"
      "}\n"
      "main() {\n"
      "  var f1 = C.foo;\n"
      "  reloadTest();\n"
      "  return f1();\n"
      "}\n";

  TestCase::SetReloadTestScript(kReloadScript);
  Dart_Handle error_handle = SimpleInvokeError(lib, "main");

  const char* error;
  error =
      "/test-lib:8:12: Error: Too few positional"
      " arguments: 1 required, 0 given.\n"
      "  return f1();";
  EXPECT_ERROR(error_handle, error);
}

TEST_CASE(IsolateReload_TearOff_Remove) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo({String bar: 'bar'}) => 'old';\n"
      "}\n"
      "main() {\n"
      "  var f1 = C.foo;\n"
      "  reloadTest();\n"
      "  try {\n"
      "    return f1();\n"
      "  } catch(e) { return '$e'; }\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "}\n"
      "main() {\n"
      "  var f1;\n"
      "  reloadTest();\n"
      "  try {\n"
      "    return f1();\n"
      "  } catch(e) { return '$e'; }\n"
      "}\n";

  TestCase::SetReloadTestScript(kReloadScript);

  EXPECT_SUBSTRING(
      "NoSuchMethodError: No static method 'foo' declared in class 'C'.",
      SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_Class_Identity) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'old';\n"
      "}\n"
      "getFoo() => C.foo;\n"
      "main() {\n"
      "  var f1 = getFoo();\n"
      "  reloadTest();\n"
      "  var f2 = getFoo();\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo() => 'new';\n"
      "}\n"
      "getFoo() => C.foo;\n"
      "main() {\n"
      "  var f1 = getFoo();\n"
      "  reloadTest();\n"
      "  var f2 = getFoo();\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("new new true true", SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_Library_Identity) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "foo() => 'old';\n"
      "getFoo() => foo;\n"
      "main() {\n"
      "  var f1 = getFoo();\n"
      "  reloadTest();\n"
      "  var f2 = getFoo();\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "foo() => 'new';\n"
      "getFoo() => foo;\n"
      "main() {\n"
      "  var f1 = getFoo();\n"
      "  reloadTest();\n"
      "  var f2 = getFoo();\n"
      "  return '${f1()} ${f2()} ${f1 == f2} ${identical(f1, f2)}';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("new new true true", SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_List_Set) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'old';\n"
      "}\n"
      "List list = List<dynamic>.filled(2, null);\n"
      "Set set = Set();\n"
      "main() {\n"
      "  var c = C();\n"
      "  list[0] = c.foo;\n"
      "  list[1] = c.foo;\n"
      "  set.add(c.foo);\n"
      "  set.add(c.foo);\n"
      "  int countBefore = set.length;\n"
      "  reloadTest();\n"
      "  list[1] = c.foo;\n"
      "  set.add(c.foo);\n"
      "  set.add(c.foo);\n"
      "  int countAfter = set.length;\n"
      "  return '${list[0]()} ${list[1]()} ${list[0] == list[1]} '\n"
      "         '${countBefore == 1} ${countAfter == 1} ${(set.first)()} '\n"
      "         '${set.first == c.foo} ${set.first == c.foo} '\n"
      "         '${set.remove(c.foo)}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo() => 'new';\n"
      "}\n"
      "List list = List<dynamic>.filled(2, null);\n"
      "Set set = Set();\n"
      "main() {\n"
      "  var c = C();\n"
      "  list[0] = c.foo;\n"
      "  list[1] = c.foo;\n"
      "  set.add(c.foo);\n"
      "  set.add(c.foo);\n"
      "  int countBefore = set.length;\n"
      "  reloadTest();\n"
      "  list[1] = c.foo;\n"
      "  set.add(c.foo);\n"
      "  set.add(c.foo);\n"
      "  int countAfter = set.length;\n"
      "  return '${list[0]()} ${list[1]()} ${list[0] == list[1]} '\n"
      "         '${countBefore == 1} ${countAfter == 1} ${(set.first)()} '\n"
      "         '${set.first == c.foo} ${set.first == c.foo} '\n"
      "         '${set.remove(c.foo)}';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("new new true true true new true true true",
               SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_AddArguments) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo(x) => x;\n"
      "}\n"
      "invoke(f, a) {\n"
      "  try {\n"
      "    return f(a);\n"
      "  } catch (e) {\n"
      "    return e.toString().split('\\n').first;\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  var c = new C();\n"
      "  var f = c.foo;\n"
      "  var r1 = invoke(f, 1);\n"
      "  reloadTest();\n"
      "  var r2 = invoke(f, 1);\n"
      "  return '$r1 $r2';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  foo(x, y, z) => x + y + z;\n"
      "}\n"
      "invoke(f, a) {\n"
      "  try {\n"
      "    return f(a);\n"
      "  } catch (e) {\n"
      "    return e.toString().split('\\n').first;\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  var c = new C();\n"
      "  var f = c.foo;\n"
      "  var r1 = invoke(f, 1);\n"
      "  reloadTest();\n"
      "  var r2 = invoke(f, 1);\n"
      "  return '$r1 $r2';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ(
      "1 NoSuchMethodError: Class 'C' has no instance method "
      "'foo' with matching arguments.",
      SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_TearOff_AddArguments2) {
  const char* kScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo(x) => x;\n"
      "}\n"
      "invoke(f, a) {\n"
      "  try {\n"
      "    return f(a);\n"
      "  } catch (e) {\n"
      "    return e.toString().split('\\n').first;\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  var f = C.foo;\n"
      "  var r1 = invoke(f, 1);\n"
      "  reloadTest();\n"
      "  var r2 = invoke(f, 1);\n"
      "  return '$r1 $r2';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "import 'file:///test:isolate_reload_helper';\n"
      "class C {\n"
      "  static foo(x, y, z) => x + y + z;\n"
      "}\n"
      "invoke(f, a) {\n"
      "  try {\n"
      "    return f(a);\n"
      "  } catch (e) {\n"
      "    return e.toString().split('\\n').first;\n"
      "  }\n"
      "}\n"
      "main() {\n"
      "  var f = C.foo;\n"
      "  var r1 = invoke(f, 1);\n"
      "  reloadTest();\n"
      "  var r2 = invoke(f, 1);\n"
      "  return '$r1 $r2';\n"
      "}\n";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ(
      "1 NoSuchMethodError: Closure call with mismatched arguments: "
      "function 'C.foo'",
      SimpleInvokeStr(lib, "main"));

  lib = Dart_RootLibrary();
  EXPECT_NON_NULL(lib);
}

TEST_CASE(IsolateReload_EnumEquality) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  x = Fruit.Banana;\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  if (x == Fruit.Banana) {\n"
      "    return 'yes';\n"
      "  } else {\n"
      "    return 'no';\n"
      "  }\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("yes", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumIdentical) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  x = Fruit.Banana;\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  if (identical(x, Fruit.Banana)) {\n"
      "    return 'yes';\n"
      "  } else {\n"
      "    return 'no';\n"
      "  }\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("yes", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumReorderIdentical) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  x = Fruit.Banana;\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Banana,\n"
      "  Apple,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  if (identical(x, Fruit.Banana)) {\n"
      "    return 'yes';\n"
      "  } else {\n"
      "    return 'no';\n"
      "  }\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("yes", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumAddition) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Cantaloupe,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  String r = '${Fruit.Apple.index}/${Fruit.Apple} ';\n"
      "  r += '${Fruit.Cantaloupe.index}/${Fruit.Cantaloupe} ';\n"
      "  r += '${Fruit.Banana.index}/${Fruit.Banana}';\n"
      "  return r;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("0/Fruit.Apple 1/Fruit.Cantaloupe 2/Fruit.Banana",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumToNotEnum) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple\n"
      "}\n"
      "main() {\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class Fruit {\n"
      "  final int zero = 0;\n"
      "}\n"
      "main() {\n"
      "  return new Fruit().zero.toString();\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(result, "Enum class cannot be redefined to be a non-enum class");
}

TEST_CASE(IsolateReload_NotEnumToEnum) {
  const char* kScript =
      "class Fruit {\n"
      "  final int zero = 0;\n"
      "}\n"
      "main() {\n"
      "  return new Fruit().zero.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("0", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple\n"
      "}\n"
      "main() {\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(result, "Class cannot be redefined to be a enum class");
}

TEST_CASE(IsolateReload_EnumDelete) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "  Cantaloupe,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  x = Fruit.Cantaloupe;\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  // Delete 'Cantaloupe' but make sure that we can still invoke toString,
  // and access the hashCode and index properties.

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  String r = '$x ${x.hashCode is int} ${x.index}';\n"
      "  return r;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Deleted enum value from Fruit true -1",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumIdentityReload) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "  Cantaloupe,\n"
      "}\n"
      "var x;\n"
      "var y;\n"
      "var z;\n"
      "var w;\n"
      "main() {\n"
      "  x = { Fruit.Apple: Fruit.Apple.index,\n"
      "        Fruit.Banana: Fruit.Banana.index,\n"
      "        Fruit.Cantaloupe: Fruit.Cantaloupe.index};\n"
      "  y = Fruit.Apple;\n"
      "  z = Fruit.Banana;\n"
      "  w = Fruit.Cantaloupe;\n"
      "  return Fruit.Apple.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "  Cantaloupe,\n"
      "}\n"
      "var x;\n"
      "var y;\n"
      "var z;\n"
      "var w;\n"
      "bool identityCheck(Fruit f, int index) {\n"
      "  return identical(Fruit.values[index], f);\n"
      "}\n"
      "main() {\n"
      "  String r = '';\n"
      "  x.forEach((key, value) {\n"
      "    r += '${identityCheck(key, value)} ';\n"
      "  });\n"
      "  r += '${x[Fruit.Apple] == Fruit.Apple.index} ';\n"
      "  r += '${x[Fruit.Banana] == Fruit.Banana.index} ';\n"
      "  r += '${x[Fruit.Cantaloupe] == Fruit.Cantaloupe.index} ';\n"
      "  r += '${identical(y, Fruit.values[x[Fruit.Apple]])} ';\n"
      "  r += '${identical(z, Fruit.values[x[Fruit.Banana]])} ';\n"
      "  r += '${identical(w, Fruit.values[x[Fruit.Cantaloupe]])} ';\n"
      "  return r;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("true true true true true true true true true ",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ConstantIdentical) {
  const char* kScript =
      "class Fruit {\n"
      "  final String name;\n"
      "  const Fruit(this.name);\n"
      "  String toString() => name;\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  x = const Fruit('Pear');\n"
      "  return x.toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Pear", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class Fruit {\n"
      "  final String name;\n"
      "  const Fruit(this.name);\n"
      "  String toString() => name;\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  if (identical(x, const Fruit('Pear'))) {\n"
      "    return 'yes';\n"
      "  } else {\n"
      "    return 'no';\n"
      "  }\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("yes", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_EnumValuesToString) {
  const char* kScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Banana,\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  String r = '';\n"
      "  r += Fruit.Apple.toString();\n"
      "  r += ' ';\n"
      "  r += Fruit.Banana.toString();\n"
      "  return r;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple Fruit.Banana", SimpleInvokeStr(lib, "main"));

  // Insert 'Cantaloupe'.

  const char* kReloadScript =
      "enum Fruit {\n"
      "  Apple,\n"
      "  Cantaloupe,\n"
      "  Banana\n"
      "}\n"
      "var x;\n"
      "main() {\n"
      "  String r = '';\n"
      "  r += Fruit.Apple.toString();\n"
      "  r += ' ';\n"
      "  r += Fruit.Cantaloupe.toString();\n"
      "  r += ' ';\n"
      "  r += Fruit.Banana.toString();\n"
      "  return r;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Fruit.Apple Fruit.Cantaloupe Fruit.Banana",
               SimpleInvokeStr(lib, "main"));
}

ISOLATE_UNIT_TEST_CASE(IsolateReload_DirectSubclasses_Success) {
  Object& new_subclass = Object::Handle();
  String& name = String::Handle();

  // Lookup the Stopwatch class by name from the dart core library.
  ObjectStore* object_store = IsolateGroup::Current()->object_store();
  const Library& core_lib = Library::Handle(object_store->core_library());
  name = String::New("Stopwatch");
  const Class& stopwatch_cls = Class::Handle(core_lib.LookupClass(name));

  // Keep track of how many subclasses an Stopwatch has.
  auto& subclasses =
      GrowableObjectArray::Handle(stopwatch_cls.direct_subclasses_unsafe());
  intptr_t saved_subclass_count = subclasses.IsNull() ? 0 : subclasses.Length();

  const char* kScript =
      "class AStopwatch extends Stopwatch {\n"
      "}\n"
      "main() {\n"
      "  new AStopwatch();\n"  // Force finalization.
      "  return 1;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
    EXPECT_VALID(lib);
    EXPECT_EQ(1, SimpleInvoke(lib, "main"));
  }

  // Stopwatch has one non-core subclass.
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 1, subclasses.Length());

  // The new subclass is named AStopwatch.
  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("AStopwatch", name.ToCString());

  const char* kReloadScript =
      "class AStopwatch {\n"
      "}\n"
      "class BStopwatch extends Stopwatch {\n"
      "}\n"
      "main() {\n"
      "  new AStopwatch();\n"  // Force finalization.
      "  new BStopwatch();\n"  // Force finalization.
      "  return 2;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::ReloadTestScript(kReloadScript);
    EXPECT_VALID(lib);
    EXPECT_EQ(2, SimpleInvoke(lib, "main"));
  }

  // Stopwatch still has only one non-core subclass (AStopwatch is gone).
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 1, subclasses.Length());

  // The new subclass is named BStopwatch.
  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("BStopwatch", name.ToCString());
}

ISOLATE_UNIT_TEST_CASE(IsolateReload_DirectSubclasses_GhostSubclass) {
  Object& new_subclass = Object::Handle();
  String& name = String::Handle();

  // Lookup the Stopwatch class by name from the dart core library.
  ObjectStore* object_store = IsolateGroup::Current()->object_store();
  const Library& core_lib = Library::Handle(object_store->core_library());
  name = String::New("Stopwatch");
  const Class& stopwatch_cls = Class::Handle(core_lib.LookupClass(name));

  // Keep track of how many subclasses an Stopwatch has.
  auto& subclasses =
      GrowableObjectArray::Handle(stopwatch_cls.direct_subclasses_unsafe());
  intptr_t saved_subclass_count = subclasses.IsNull() ? 0 : subclasses.Length();

  const char* kScript =
      "class AStopwatch extends Stopwatch {\n"
      "}\n"
      "main() {\n"
      "  new AStopwatch();\n"  // Force finalization.
      "  return 1;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
    EXPECT_VALID(lib);
    EXPECT_EQ(1, SimpleInvoke(lib, "main"));
  }

  // Stopwatch has one new subclass.
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 1, subclasses.Length());

  // The new subclass is named AStopwatch.
  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("AStopwatch", name.ToCString());

  const char* kReloadScript =
      "class BStopwatch extends Stopwatch {\n"
      "}\n"
      "main() {\n"
      "  new BStopwatch();\n"  // Force finalization.
      "  return 2;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::ReloadTestScript(kReloadScript);
    EXPECT_VALID(lib);
    EXPECT_EQ(2, SimpleInvoke(lib, "main"));
  }

  // Stopwatch has two non-core subclasses.
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 2, subclasses.Length());

  // The non-core subclasses are AStopwatch and BStopwatch.
  new_subclass = subclasses.At(subclasses.Length() - 2);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("AStopwatch", name.ToCString());

  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("BStopwatch", name.ToCString());
}

// Make sure that we restore the direct subclass info when we revert.
ISOLATE_UNIT_TEST_CASE(IsolateReload_DirectSubclasses_Failure) {
  Object& new_subclass = Object::Handle();
  String& name = String::Handle();

  // Lookup the Stopwatch class by name from the dart core library.
  ObjectStore* object_store = IsolateGroup::Current()->object_store();
  const Library& core_lib = Library::Handle(object_store->core_library());
  name = String::New("Stopwatch");
  const Class& stopwatch_cls = Class::Handle(core_lib.LookupClass(name));

  // Keep track of how many subclasses an Stopwatch has.
  auto& subclasses =
      GrowableObjectArray::Handle(stopwatch_cls.direct_subclasses_unsafe());
  intptr_t saved_subclass_count = subclasses.IsNull() ? 0 : subclasses.Length();

  const char* kScript =
      "class AStopwatch extends Stopwatch {\n"
      "}\n"
      "class Foo {\n"
      "  final a;\n"
      "  Foo(this.a);\n"
      "}\n"
      "main() {\n"
      "  new AStopwatch();\n"  // Force finalization.
      "  new Foo(5);\n"        // Force finalization.
      "  return 1;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
    EXPECT_VALID(lib);
    EXPECT_EQ(1, SimpleInvoke(lib, "main"));
  }

  // Stopwatch has one non-core subclass...
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 1, subclasses.Length());

  // ... and the non-core subclass is named AStopwatch.
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("AStopwatch", name.ToCString());

  // Attempt to reload with a bogus script.
  const char* kReloadScript =
      "class BStopwatch extends Stopwatch {\n"
      "}\n"
      "class Foo {\n"
      "  final a kjsdf ksjdf ;\n"  // When we refinalize, we get an error.
      "  Foo(this.a);\n"
      "}\n"
      "main() {\n"
      "  new BStopwatch();\n"  // Force finalization.
      "  new Foo(5);\n"        // Force finalization.
      "  return 2;\n"
      "}\n";

  {
    TransitionVMToNative transition(thread);
    Dart_Handle lib = TestCase::ReloadTestScript(kReloadScript);
    EXPECT_ERROR(lib, "Expected ';' after this");
  }

  // If we don't clean up the subclasses, we would find BStopwatch in
  // the list of subclasses, which would be bad.  Make sure that
  // Stopwatch still has only one non-core subclass...
  subclasses = stopwatch_cls.direct_subclasses_unsafe();
  EXPECT_EQ(saved_subclass_count + 1, subclasses.Length());

  // ...and the non-core subclass is still named AStopwatch.
  new_subclass = subclasses.At(subclasses.Length() - 1);
  name = Class::Cast(new_subclass).Name();
  EXPECT_STREQ("AStopwatch", name.ToCString());
}

// Tests reload succeeds when instance format changes.
// Change: Foo {a, b, c:42}  -> Foo {c:42}
// Validate: c keeps the value in the retained Foo object.
TEST_CASE(IsolateReload_ChangeInstanceFormat0) {
  const char* kScript =
      "class Foo {\n"
      "  var a;\n"
      "  var b;\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  f.c = 42;\n"
      "  return f.c;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(42, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  return f.c;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(42, SimpleInvoke(lib, "main"));
}

// Tests reload succeeds when instance format changes.
// Change: Foo {}  -> Foo {c:null}
// Validate: c is initialized to null the retained Foo object.
TEST_CASE(IsolateReload_ChangeInstanceFormat1) {
  const char* kScript =
      "class Foo {\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  return 42;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(42, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  return (f.c == null) ? 42: 21;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(42, SimpleInvoke(lib, "main"));
}

// Tests reload succeeds when instance format changes.
// Change: Foo {c:42}  -> Foo {}
// Validate: running the after script fails.
TEST_CASE(IsolateReload_ChangeInstanceFormat2) {
  const char* kScript =
      "class Foo {\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  f.c = 42;\n"
      "  return f.c;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(42, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  try {\n"
      "    return f.c;\n"
      "  } catch (e) {\n"
      "    return 24;\n"
      "  }\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(24, SimpleInvoke(lib, "main"));
}

// Tests reload succeeds when instance format changes.
// Change: Foo {a, b, c:42, d}  -> Foo {c:42, g}
// Validate: c keeps the value in the retained Foo object.
TEST_CASE(IsolateReload_ChangeInstanceFormat3) {
  const char* kScript =
      "class Foo<A,B> {\n"
      "  var a;\n"
      "  var b;\n"
      "  var c;\n"
      "  var d;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  f.a = 1;\n"
      "  f.b = 2;\n"
      "  f.c = 3;\n"
      "  f.d = 4;\n"
      "  return f.c;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(3, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo<A,B> {\n"
      "  var c;\n"
      "  var g;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  return f.c;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(3, SimpleInvoke(lib, "main"));
}

// Tests reload succeeds when instance format changes.
// Change: Bar {c:42}, Foo : Bar {d, e} -> Foo {c:42}
// Validate: c keeps the value in the retained Foo object.
TEST_CASE(IsolateReload_ChangeInstanceFormat4) {
  const char* kScript =
      "class Bar{\n"
      "  var c;\n"
      "}\n"
      "class Foo extends Bar{\n"
      "  var d;\n"
      "  var e;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  f.c = 44;\n"
      "  return f.c;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo {\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  return f.c;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));
}

// Tests reload succeeds when instance format changes.
// Change: Bar {a, b}, Foo : Bar {c:42} -> Bar {c:42}, Foo : Bar {}
// Validate: c keeps the value in the retained Foo object.
TEST_CASE(IsolateReload_ChangeInstanceFormat5) {
  const char* kScript =
      "class Bar{\n"
      "  var a;\n"
      "  var b;\n"
      "}\n"
      "class Foo extends Bar{\n"
      "  var c;\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  f = new Foo();\n"
      "  f.c = 44;\n"
      "  return f.c;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Bar{\n"
      "  var c;\n"
      "}\n"
      "class Foo extends Bar {\n"
      "}\n"
      "var f;\n"
      "main() {\n"
      "  return f.c;\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_EQ(44, SimpleInvoke(lib, "main"));
}

// Tests reload fails when type parameters change.
// Change: Foo<A,B> {a, b}  -> Foo<A> {a}
// Validate: the right error message is returned.
TEST_CASE(IsolateReload_ChangeInstanceFormat6) {
  const char* kScript =
      "class Foo<A, B> {\n"
      "  var a;\n"
      "  var b;\n"
      "}\n"
      "main() {\n"
      "  new Foo();\n"
      "  return 43;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(43, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo<A> {\n"
      "  var a;\n"
      "}\n";
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib, "type parameters have changed");
}

// Tests reload succeeds when type parameters are changed for allocated class.
// Change: Foo<A,B> {a, b} -> Foo<A> {a}
// Validate: return value from main is correct.
// Please note: This test works because no instances are created from Foo.
TEST_CASE(IsolateReload_ChangeInstanceFormat7) {
  const char* kScript =
      "class Foo<A, B> {\n"
      "  var a;\n"
      "  var b;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript =
      "class Foo<A> {\n"
      "  var a;\n"
      "}\n";
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
}

// Regression for handle sharing bug: Change the shape of two classes and see
// that their instances don't change class.
TEST_CASE(IsolateReload_ChangeInstanceFormat8) {
  const char* kScript =
      "class A{\n"
      "  var x;\n"
      "}\n"
      "class B {\n"
      "  var x, y, z, w;\n"
      "}\n"
      "var a, b;\n"
      "main() {\n"
      "  a = new A();\n"
      "  b = new B();\n"
      "  return '$a $b';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Instance of 'A' Instance of 'B'", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class A{\n"
      "  var x, y;\n"
      "}\n"
      "class B {\n"
      "  var x, y, z, w, v;\n"
      "}\n"
      "var a, b;\n"
      "main() {\n"
      "  return '$a $b';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Instance of 'A' Instance of 'B'", SimpleInvokeStr(lib, "main"));
}

// Tests reload fails when type arguments change.
// Change: Baz extends Foo<String> -> Baz extends Bar<String, double>
// Validate: the right error message is returned.
TEST_CASE(IsolateReload_ChangeInstanceFormat9) {
  const char* kScript =
      "class Foo<A> {\n"
      "  var a;\n"
      "}\n"
      "class Bar<B, C> extends Foo<B> {}\n"
      "class Baz extends Foo<String> {}"
      "main() {\n"
      "  new Baz();\n"
      "  return 43;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(43, SimpleInvoke(lib, "main"));

  const char* kReloadScript =
      "class Foo<A> {\n"
      "  var a;\n"
      "}\n"
      "class Bar<B, C> extends Foo<B> {}\n"
      "class Baz extends Bar<String, double> {}"
      "main() {\n"
      "  new Baz();\n"
      "  return 43;\n"
      "}\n";
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib, "type parameters have changed");
}

TEST_CASE(IsolateReload_ShapeChangeRetainsHash) {
  const char* kScript =
      "class A{\n"
      "  var x;\n"
      "}\n"
      "var a, hash1, hash2;\n"
      "main() {\n"
      "  a = new A();\n"
      "  hash1 = a.hashCode;\n"
      "  return 'okay';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class A{\n"
      "  var x, y, z;\n"
      "}\n"
      "var a, hash1, hash2;\n"
      "main() {\n"
      "  hash2 = a.hashCode;\n"
      "  return (hash1 == hash2).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ShapeChangeRetainsHash_Const) {
  const char* kScript =
      "class A {\n"
      "  final x;\n"
      "  const A(this.x);\n"
      "}\n"
      "var a, hash1, hash2;\n"
      "main() {\n"
      "  a = const A(1);\n"
      "  hash1 = a.hashCode;\n"
      "  return 'okay';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class A {\n"
      "  final x, y, z;\n"
      "  const A(this.x, this.y, this.z);\n"
      "}\n"
      "var a, hash1, hash2;\n"
      "main() {\n"
      "  hash2 = a.hashCode;\n"
      "  return (hash1 == hash2).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ShapeChange_Const_AddSlot) {
  // On IA32, instructions can contain direct pointers to const objects. We need
  // to be careful that if the const objects are reallocated because of a shape
  // change, they are allocated old. Because instructions normally contain
  // pointers only to old objects, the scavenger does not bother to ensure code
  // pages are writable when visiting the remembered set. Visiting the
  // remembered involves writing to update the pointer for any target that gets
  // promoted.
  const char* kScript = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x;
      const A(this.x);
    }
    var a;
    main() {
      a = const A(1);
      collectNewSpace();
      return 'okay';
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x, y, z;
      const A(this.x, this.y, this.z);
    }
    var a;
    main() {
      a = const A(1, null, null);
      collectNewSpace();
      return 'okay';
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript2 = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x, y, z, w, u;
      const A(this.x, this.y, this.z, this.w, this.u);
    }
    var a;
    main() {
      a = const A(1, null, null, null, null);
      collectNewSpace();
      return 'okay';
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript2);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ShapeChange_Const_RemoveSlot) {
  const char* kScript = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x, y, z;
      const A(this.x, this.y, this.z);
    }
    var a;
    main() {
      a = const A(1, 2, 3);
      collectNewSpace();
      return 'okay';
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x, y;
      const A(this.x, this.y);
    }
    var a;
    main() {
      a = const A(1, null);
      collectNewSpace();
      return 'okay';
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib,
               "Const class cannot remove fields: "
               "Library:'file:///test-lib' Class: A");

  // Rename is seen by the VM is unrelated add and remove.
  const char* kReloadScript2 = R"(
    import 'file:///test:isolate_reload_helper';
    class A {
      final x, y, w;
      const A(this.x, this.y, this.w);
    }
    var a;
    main() {
      a = const A(1, null, null);
      collectNewSpace();
      return 'okay';
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript2);
  EXPECT_ERROR(lib,
               "Const class cannot remove fields: "
               "Library:'file:///test-lib' Class: A");
}

TEST_CASE(IsolateReload_ConstToNonConstClass) {
  const char* kScript = R"(
    class A {
      final dynamic x;
      const A(this.x);
    }
    dynamic a;
    main() {
      a = const A(1);
      return 'okay';
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    class A {
      dynamic x;
      A(this.x);
    }
    dynamic a;
    main() {
      a.x = 10;
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib,
               "Const class cannot become non-const: "
               "Library:'file:///test-lib' Class: A");
}

TEST_CASE(IsolateReload_ConstToNonConstClass_Empty) {
  const char* kScript = R"(
    class A {
      const A();
    }
    dynamic a;
    main() {
      a = const A();
      return 'okay';
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    class A {
      dynamic x;
      A(this.x);
    }
    dynamic a;
    main() {
      a.x = 10;
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_ERROR(lib,
               "Const class cannot become non-const: "
               "Library:'file:///test-lib' Class: A");
}

TEST_CASE(IsolateReload_StaticTearOffRetainsHash) {
  const char* kScript =
      "foo() {}\n"
      "var hash1, hash2;\n"
      "main() {\n"
      "  hash1 = foo.hashCode;\n"
      "  return 'okay';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "foo() {}\n"
      "var hash1, hash2;\n"
      "main() {\n"
      "  hash2 = foo.hashCode;\n"
      "  return (hash1 == hash2).toString();\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));
}

static bool NothingModifiedCallback(const char* url, int64_t since) {
  return false;
}

TEST_CASE(IsolateReload_NoLibsModified) {
  const char* kImportScript = "importedFunc() => 'fancy';";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' feast';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));

  const char* kReloadImportScript = "importedFunc() => 'bossy';";
  TestCase::AddTestLib("test:lib1", kReloadImportScript);

  const char* kReloadScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' pants';\n"
      "}\n";

  Dart_SetFileModifiedCallback(&NothingModifiedCallback);
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_SetFileModifiedCallback(NULL);

  // No reload occurred because no files were "modified".
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));
}

static bool MainModifiedCallback(const char* url, int64_t since) {
  if ((strcmp(url, "test-lib") == 0) ||
      (strcmp(url, "file:///test-lib") == 0)) {
    return true;
  }
  return false;
}

TEST_CASE(IsolateReload_MainLibModified) {
  const char* kImportScript = "importedFunc() => 'fancy';";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' feast';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));

  const char* kReloadImportScript = "importedFunc() => 'bossy';";
  TestCase::AddTestLib("test:lib1", kReloadImportScript);

  const char* kReloadScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' pants';\n"
      "}\n";

  Dart_SetFileModifiedCallback(&MainModifiedCallback);
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_SetFileModifiedCallback(NULL);

  // Imported library is not reloaded.
  EXPECT_STREQ("fancy pants", SimpleInvokeStr(lib, "main"));
}

static bool ImportModifiedCallback(const char* url, int64_t since) {
  if (strcmp(url, "test:lib1") == 0) {
    return true;
  }
  return false;
}

TEST_CASE(IsolateReload_ImportedLibModified) {
  const char* kImportScript = "importedFunc() => 'fancy';";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' feast';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));

  const char* kReloadImportScript = "importedFunc() => 'bossy';";
  TestCase::AddTestLib("test:lib1", kReloadImportScript);

  const char* kReloadScript =
      "import 'test:lib1';\n"
      "main() {\n"
      "  return importedFunc() + ' pants';\n"
      "}\n";

  Dart_SetFileModifiedCallback(&ImportModifiedCallback);
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_SetFileModifiedCallback(NULL);

  // Modification of an imported library propagates to the importing library.
  EXPECT_STREQ("bossy pants", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PrefixImportedLibModified) {
  const char* kImportScript = "importedFunc() => 'fancy';";
  TestCase::AddTestLib("test:lib1", kImportScript);

  const char* kScript =
      "import 'test:lib1' as cobra;\n"
      "main() {\n"
      "  return cobra.importedFunc() + ' feast';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));

  const char* kReloadImportScript = "importedFunc() => 'bossy';";
  TestCase::AddTestLib("test:lib1", kReloadImportScript);

  const char* kReloadScript =
      "import 'test:lib1' as cobra;\n"
      "main() {\n"
      "  return cobra.importedFunc() + ' pants';\n"
      "}\n";

  Dart_SetFileModifiedCallback(&ImportModifiedCallback);
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_SetFileModifiedCallback(NULL);

  // Modification of an prefix-imported library propagates to the
  // importing library.
  EXPECT_STREQ("bossy pants", SimpleInvokeStr(lib, "main"));
}

static bool ExportModifiedCallback(const char* url, int64_t since) {
  if (strcmp(url, "test:exportlib") == 0) {
    return true;
  }
  return false;
}

TEST_CASE(IsolateReload_ExportedLibModified) {
  const char* kImportScript = "export 'test:exportlib';";
  TestCase::AddTestLib("test:importlib", kImportScript);

  const char* kExportScript = "exportedFunc() => 'fancy';";
  TestCase::AddTestLib("test:exportlib", kExportScript);

  const char* kScript =
      "import 'test:importlib';\n"
      "main() {\n"
      "  return exportedFunc() + ' feast';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("fancy feast", SimpleInvokeStr(lib, "main"));

  const char* kReloadExportScript = "exportedFunc() => 'bossy';";
  TestCase::AddTestLib("test:exportlib", kReloadExportScript);

  const char* kReloadScript =
      "import 'test:importlib';\n"
      "main() {\n"
      "  return exportedFunc() + ' pants';\n"
      "}\n";

  Dart_SetFileModifiedCallback(&ExportModifiedCallback);
  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_SetFileModifiedCallback(NULL);

  // Modification of an exported library propagates.
  EXPECT_STREQ("bossy pants", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_SimpleConstFieldUpdate) {
  const char* kScript =
      "const value = 'a';\n"
      "main() {\n"
      "  return 'value=${value}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value=a", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "const value = 'b';\n"
      "main() {\n"
      "  return 'value=${value}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value=b", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ConstFieldUpdate) {
  const char* kScript =
      "const value = const Duration(seconds: 1);\n"
      "main() {\n"
      "  return 'value=${value}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value=0:00:01.000000", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "const value = const Duration(seconds: 2);\n"
      "main() {\n"
      "  return 'value=${value}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("value=0:00:02.000000", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializers) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                            "class Foo {\n"
                                            "  int x = 4;\n"
                                            "}\n"
                                            "%s Foo value;\n"
                                            "main() {\n"
                                            "  value = Foo();\n"
                                            "  return value.x;\n"
                                            "}\n",
                                            late_tag),
                                         std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                                  "class Foo {\n"
                                                  "  int x = 4;\n"
                                                  "  int y = 7;\n"
                                                  "}\n"
                                                  "%s Foo value;\n"
                                                  "main() {\n"
                                                  "  return value.y;\n"
                                                  "}\n",
                                                  late_tag),
                                               std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances.
  EXPECT_EQ(7, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersReferenceStaticField) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "int myInitialValue = 8 * 7;\n"
                                          "class Foo {\n"
                                          "  int x = 4;\n"
                                          "}\n"
                                          "%s Foo value;\n"
                                          "main() {\n"
                                          "  value = Foo();\n"
                                          "  return value.x;\n"
                                          "}\n",
                                          late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y.
  // clang-format off
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "int myInitialValue = 8 * 7;\n"
                                          "class Foo {\n"
                                          "  int x = 4;\n"
                                          "  int y = myInitialValue;\n"
                                          "}\n"
                                          "%s Foo value;\n"
                                          "main() {\n"
                                          "  return value.y;\n"
                                          "}\n",
                                          late_tag),
                              std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances.
  EXPECT_EQ(56, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersLazy) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "int myInitialValue = 8 * 7;\n"
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "}\n"
                                "%s Foo value;\n"
                                "%s Foo value1;\n"
                                "main() {\n"
                                "  value = Foo();\n"
                                "  value1 = Foo();\n"
                                "  return value.x;\n"
                                "}\n",
                                late_tag, late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "int myInitialValue = 8 * 7;\n"
                  "class Foo {\n"
                  "  int x = 4;\n"
                  "  int y = myInitialValue++;\n"
                  "}\n"
                  "%s Foo value;\n"
                  "%s Foo value1;\n"
                  "main() {\n"
                  "  return '${myInitialValue} ${value.y} ${value1.y} "
                  "${myInitialValue}';\n"
                  "}\n",
                  late_tag, late_tag),
      std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that field initializers ran lazily.
  EXPECT_STREQ("56 56 57 58", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersLazyConst) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                            "class Foo {\n"
                                            "  int x = 4;\n"
                                            "}\n"
                                            "%s Foo value;\n"
                                            "main() {\n"
                                            "  value = Foo();\n"
                                            "  return value.x;\n"
                                            "}\n",
                                            late_tag),
                                         std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y. Do not read it. Note field y does not get an initializer
  // function in the VM because the initializer is a literal, but we should not
  // eagerly initialize with the literal so that the behavior doesn't depend on
  // this optimization.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                                  "class Foo {\n"
                                                  "  int x = 4;\n"
                                                  "  int y = 5;\n"
                                                  "}\n"
                                                  "%s Foo value;\n"
                                                  "main() {\n"
                                                  "  return 0;\n"
                                                  "}\n",
                                                  late_tag),
                                               std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_EQ(0, SimpleInvoke(lib, "main"));

  // Change y's initializer and check this new initializer is used.
  auto kReloadScript2 =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "class Foo {\n"
                                          "  int x = 4;\n"
                                          "  int y = 6;\n"
                                          "}\n"
                                          "%s Foo value;\n"
                                          "main() {\n"
                                          "  return value.y;\n"
                                          "}\n",
                                          late_tag),
                              std::free);

  lib = TestCase::ReloadTestScript(kReloadScript2.get());
  EXPECT_VALID(lib);
  EXPECT_EQ(6, SimpleInvoke(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersLazyTransitive) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "int myInitialValue = 8 * 7;\n"
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "}\n"
                                "%s Foo value;\n"
                                "%s Foo value1;\n"
                                "main() {\n"
                                "  value = Foo();\n"
                                "  value1 = Foo();\n"
                                "  return value.x;\n"
                                "}\n",
                                late_tag, late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y. Do not touch y.
  // clang-format off
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "int myInitialValue = 8 * 7;\n"
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "  int y = myInitialValue++;\n"
                                "}\n"
                                "%s Foo value;\n"
                                "%s Foo value1;\n"
                                "main() {\n"
                                "  return '${myInitialValue}';\n"
                                "}\n",
                                late_tag, late_tag),
                              std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ("56", SimpleInvokeStr(lib, "main"));

  // Reload again. Field y's getter still needs to keep for initialization even
  // though it is no longer new.
  // clang-format off
  auto kReloadScript2 = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "int myInitialValue = 8 * 7;\n"
                  "class Foo {\n"
                  "  int x = 4;\n"
                  "  int y = myInitialValue++;\n"
                  "}\n"
                  "%s Foo value;\n"
                  "%s Foo value1;\n"
                  "main() {\n"
                  "  return '${myInitialValue} ${value.y} ${value1.y} "
                  "${myInitialValue}';\n"
                  "}\n",
                  late_tag, late_tag),
      std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript2.get());
  EXPECT_VALID(lib);
  // Verify that field initializers ran lazily.
  EXPECT_STREQ("56 56 57 58", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersThrows) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                            "class Foo {\n"
                                            "  int x = 4;\n"
                                            "}\n"
                                            "%s Foo value;\n"
                                            "main() {\n"
                                            "  value = Foo();\n"
                                            "  return value.x;\n"
                                            "}\n",
                                            late_tag),
                                         std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y.
  // clang-format off
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "  int y = throw 'exception';\n"
                                "}\n"
                                "%s Foo value;\n"
                                "main() {\n"
                                "  try {\n"
                                "    return value.y.toString();\n"
                                "  } catch (e) {\n"
                                "    return e.toString();\n"
                                "  }\n"
                                "}\n",
                                late_tag),
                              std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances.
  EXPECT_STREQ("exception", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersCyclicInitialization) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "class Foo {\n"
                                            "  int x = 4;\n"
                                            "}\n"
                                            "%s Foo value;\n"
                                            "main() {\n"
                                            "  value = Foo();\n"
                                            "  return value.x;\n"
                                            "}\n",
                                            late_tag),
                                         std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y.
  // clang-format off
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "  int y = value.y;\n"
                                "}\n"
                                "%s Foo value;\n"
                                "main() {\n"
                                "  try {\n"
                                "    return value.y.toString();\n"
                                "  } catch (e) {\n"
                                "    return e.toString();\n"
                                "  }\n"
                                "}\n",
                                late_tag),
                              std::free);
  // clang-format on
  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ("Stack Overflow", SimpleInvokeStr(lib, "main"));
}

// When an initializer expression has a syntax error, we detect it at reload
// time.
TEST_CASE(IsolateReload_RunNewFieldInitializersSyntaxError) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                            "class Foo {\n"
                                            "  int x = 4;\n"
                                            "}\n"
                                            "%s Foo value;\n"
                                            "main() {\n"
                                            "  value = Foo();\n"
                                            "  return value.x;\n"
                                            "}\n",
                                            late_tag),
                                         std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y with a syntax error in the initializing expression.
  // clang-format off
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class Foo {\n"
                                "  int x = 4;\n"
                                "  int y = ......;\n"
                                "}\n"
                                "%s Foo value;\n"
                                "main() {\n"
                                "  return '${value.y == null}';"
                                "}\n",
                                late_tag),
                              std::free);
  // clang-format on

  // The reload fails because the initializing expression is parsed at
  // class finalization time.
  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_ERROR(lib, "...");
}

// When an initializer expression has a syntax error, we detect it at reload
// time.
TEST_CASE(IsolateReload_RunNewFieldInitializersSyntaxError2) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class Foo {\n"
                  "  Foo() { /* default constructor */ }\n"
                  "  int x = 4;\n"
                  "}\n"
                  "%s Foo value;\n"
                  "main() {\n"
                  "  value = Foo();\n"
                  "  return value.x;\n"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y with a syntax error in the initializing expression.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class Foo {\n"
                  "  Foo() { /* default constructor */ }\n"
                  "  int x = 4;\n"
                  "  int y = ......;\n"
                  "}\n"
                  "%s Foo value;\n"
                  "main() {\n"
                  "  return '${value.y == null}';"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  // The reload fails because the initializing expression is parsed at
  // class finalization time.
  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_ERROR(lib, "...");
}

// When an initializer expression has a syntax error, we detect it at reload
// time.
TEST_CASE(IsolateReload_RunNewFieldInitializersSyntaxError3) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class Foo {\n"
                  "  Foo() { /* default constructor */ }\n"
                  "  int x = 4;\n"
                  "}\n"
                  "%s Foo value;\n"
                  "main() {\n"
                  "  value = Foo();\n"
                  "  return value.x;\n"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(4, SimpleInvoke(lib, "main"));

  // Add the field y with a syntax error in the initializing expression.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class Foo {\n"
                  "  Foo() { /* default constructor */ }\n"
                  "  int x = 4;\n"
                  "  int y = ......\n"
                  "}\n"
                  "%s Foo value;\n"
                  "main() {\n"
                  "  return '${value.y == null}';"
                  "}\n",
                  late_tag),
      std::free);
  // clang-format on

  // The reload fails because the initializing expression is parsed at
  // class finalization time.
  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_ERROR(lib, "......");
}

TEST_CASE(IsolateReload_RunNewFieldInitializersSuperClass) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class Super {\n"
                                "  static var foo = 'right';\n"
                                "}\n"
                                "class Foo extends Super {\n"
                                "  static var foo = 'wrong';\n"
                                "}\n"
                                "%s Foo value;\n"
                                "main() {\n"
                                "  Super.foo;\n"
                                "  Foo.foo;\n"
                                "  value = Foo();\n"
                                "  return 0;\n"
                                "}\n",
                                late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_EQ(0, SimpleInvoke(lib, "main"));

  // clang-format on
  auto kReloadScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                          "class Super {\n"
                                          "  static var foo = 'right';\n"
                                          "  var newField = foo;\n"
                                          "}\n"
                                          "class Foo extends Super {\n"
                                          "  static var foo = 'wrong';\n"
                                          "}\n"
                                          "%s Foo value;\n"
                                          "main() {\n"
                                          "  return value.newField;\n"
                                          "}\n",
                                          late_tag),
                              std::free);
  // clang-format off

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances in the
  // correct scope.
  const char* actual = SimpleInvokeStr(lib, "main");
  EXPECT(actual != nullptr);
  if (actual != nullptr) {
    EXPECT_STREQ("right", actual);
  }
}

TEST_CASE(IsolateReload_RunNewFieldInitializersWithConsts) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class C {\n"
                                "  final x;\n"
                                "  const C(this.x);\n"
                                "}\n"
                                "var a = const C(const C(1));\n"
                                "var b = const C(const C(2));\n"
                                "var c = const C(const C(3));\n"
                                "var d = const C(const C(4));\n"
                                "class Foo {\n"
                                "}\n"
                                "%s Foo value;\n"
                                "main() {\n"
                                "  value = Foo();\n"
                                "  a; b; c; d;\n"
                                "  return 'Okay';\n"
                                "}\n",
                                late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), nullptr);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(
          nullptr,
          "class C {\n"
          "  final x;\n"
          "  const C(this.x);\n"
          "}\n"
          "var a = const C(const C(1));\n"
          "var b = const C(const C(2));\n"
          "var c = const C(const C(3));\n"
          "var d = const C(const C(4));\n"
          "class Foo {\n"
          "  var d = const C(const C(4));\n"
          "  var c = const C(const C(3));\n"
          "  var b = const C(const C(2));\n"
          "  var a = const C(const C(1));\n"
          "}\n"
          "%s Foo value;\n"
          "main() {\n"
          "  return '${identical(a, value.a)} ${identical(b, value.b)}'"
          "      ' ${identical(c, value.c)} ${identical(d, value.d)}';\n"
          "}\n",
          late_tag),
      std::free);
  // clang-format on
  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances and the const
  // expressions were properly canonicalized.
  EXPECT_STREQ("true true true true", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_RunNewFieldInitializersWithGenerics) {
  const char* nullable_tag = TestCase::NullableTag();
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript =
      Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                "class Foo<T> {\n"
                                "  T%s x;\n"
                                "}\n"
                                "%s Foo value1;\n"
                                "%s Foo value2;\n"
                                "main() {\n"
                                "  value1 = Foo<String>();\n"
                                "  value2 = Foo<int>();\n"
                                "  return 'Okay';\n"
                                "}\n",
                                nullable_tag, late_tag, late_tag),
                              std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(
      OS::SCreate(nullptr,
                  "class Foo<T> {\n"
                  "  T%s x;\n"
                  "  List<T> y = List<T>.empty();"
                  "  dynamic z = <T,T>{};"
                  "}\n"
                  "%s Foo value1;\n"
                  "%s Foo value2;\n"
                  "main() {\n"
                  "  return '${value1.y.runtimeType} ${value1.z.runtimeType}'"
                  "      ' ${value2.y.runtimeType} ${value2.z.runtimeType}';\n"
                  "}\n",
                  nullable_tag, late_tag, late_tag),
      std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  // Verify that we ran field initializers on existing instances and
  // correct type arguments were used.
  EXPECT_STREQ("List<String> _Map<String, String> List<int> _Map<int, int>",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_AddNewStaticField) {
  const char* kScript =
      "class C {\n"
      "}\n"
      "main() {\n"
      "  return 'Okay';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class C {\n"
      "  static var x = 42;\n"
      "}\n"
      "main() {\n"
      "  return '${C.x}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("42", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_StaticFieldInitialValueDoesnotChange) {
  const char* kScript =
      "class C {\n"
      "  static var x = 42;\n"
      "}\n"
      "main() {\n"
      "  return '${C.x}';\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("42", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class C {\n"
      "  static var x = 13;\n"
      "}\n"
      "main() {\n"
      "  return '${C.x}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  // Newly loaded field maintained old static value
  EXPECT_STREQ("42", SimpleInvokeStr(lib, "main"));
}

class FindNoInstancesOfClass : public FindObjectVisitor {
 public:
  explicit FindNoInstancesOfClass(intptr_t cid) : cid_(cid) {
#if defined(DEBUG)
    EXPECT_GT(Thread::Current()->no_safepoint_scope_depth(), 0);
#endif
  }
  virtual ~FindNoInstancesOfClass() {}

  virtual bool FindObject(ObjectPtr obj) const {
    return obj->GetClassId() == cid_;
  }

 private:
  intptr_t cid_;
};

TEST_CASE(IsolateReload_DeleteStaticField) {
  const char* kScript =
      "class C {\n"
      "}\n"
      "class Foo {\n"
      "static var x = C();\n"
      "}\n"
      "main() {\n"
      "  return Foo.x;\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  intptr_t cid = 1118;
  {
    Dart_EnterScope();
    Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
    EXPECT_VALID(result);
    {
      TransitionNativeToVM transition(thread);
      cid = Api::ClassId(result);
    }
    Dart_ExitScope();
  }

  const char* kReloadScript =
      "class C {\n"
      "}\n"
      "class Foo {\n"
      "}\n"
      "main() {\n"
      "  return '${Foo()}';\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  Dart_Handle result = Dart_Invoke(lib, NewString("main"), 0, NULL);
  EXPECT_VALID(result);
  {
    TransitionNativeToVM transition(thread);
    GCTestHelper::CollectAllGarbage();

    {
      HeapIterationScope iteration(thread);
      NoSafepointScope no_safepoint;
      FindNoInstancesOfClass find_only(cid);
      Heap* heap = IsolateGroup::Current()->heap();
      // We still expect to find references to static field values
      // because they are not deleted after hot reload.
      EXPECT_NE(heap->FindObject(&find_only), Object::null());
    }
  }
}

static void TestReloadWithFieldChange(const char* prefix,
                                      const char* suffix,
                                      const char* verify,
                                      const char* from_type,
                                      const char* from_init,
                                      const char* to_type,
                                      const char* to_init) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr,
                                                     R"(
    import 'dart:typed_data';

    void doubleEq(double got, double expected) {
      if (got != expected) throw 'expected $expected got $got';
    }

    void float32x4Eq(Float32x4 got, Float32x4 expected) {
      if (got.equal(expected).signMask != 0xf) throw 'expected $expected got $got';
    }

    class Foo {
      %s
      %s x = %s;
      %s
    }
    %s Foo value;
    main() {
      value = Foo();
      %s
      return 'Okay';
    }
  )",
  prefix,
  from_type,
  from_init,
  suffix,
  late_tag,
  verify), std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    import 'dart:typed_data';

    void doubleEq(double got, double expected) {
      if (got != expected) throw 'expected $expected got $got';
    }

    void float32x4Eq(Float32x4 got, Float32x4 expected) {
      if (got.equal(expected).signMask != 0xf) throw 'expected $expected got $got';
    }

    class Foo {
      %s
      %s x = %s;
      %s
    }
    %s Foo value;
    main() {
      try {
        %s
        return value.x.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )", prefix, to_type, to_init, suffix,
  late_tag, verify), std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      OS::SCreate(
          Thread::Current()->zone(),
          "type '%s' is not a subtype of type '%s' of 'function result'",
          from_type, to_type),
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingFieldChangesType) {
  TestReloadWithFieldChange(/*prefix=*/"", /*suffix=*/"", /*verify=*/"",
                            /*from_type=*/"int", /*from_init=*/"42",
                            /*to_type=*/"double", /*to_init=*/"42.0");
}

TEST_CASE(IsolateReload_ExistingFieldChangesTypeWithOtherUnboxedFields) {
  TestReloadWithFieldChange(
      /*prefix=*/"double a = 1.5;",
      /*suffix=*/"Float32x4 b = Float32x4(1.0, 2.0, 3.0, 4.0);", /*verify=*/
      "doubleEq(value.a, 1.5); float32x4Eq(value.b, Float32x4(1.0, 2.0, 3.0, "
      "4.0));",
      /*from_type=*/"int", /*from_init=*/"42", /*to_type=*/"double",
      /*to_init=*/"42.0");
}

TEST_CASE(IsolateReload_ExistingFieldUnboxedToBoxed) {
  TestReloadWithFieldChange(
      /*prefix=*/"double a = 1.5;",
      /*suffix=*/"Float32x4 b = Float32x4(1.0, 2.0, 3.0, 4.0);", /*verify=*/
      "doubleEq(value.a, 1.5); float32x4Eq(value.b, Float32x4(1.0, 2.0, 3.0, "
      "4.0));",
      /*from_type=*/"double", /*from_init=*/"42.0", /*to_type=*/"String",
      /*to_init=*/"'42'");
}

TEST_CASE(IsolateReload_ExistingFieldBoxedToUnboxed) {
  // Note: underlying field will not actually be unboxed.
  TestReloadWithFieldChange(
      /*prefix=*/"double a = 1.5;",
      /*suffix=*/"Float32x4 b = Float32x4(1.0, 2.0, 3.0, 4.0);", /*verify=*/
      "doubleEq(value.a, 1.5); float32x4Eq(value.b, Float32x4(1.0, 2.0, 3.0, "
      "4.0));",
      /*from_type=*/"String", /*from_init=*/"'42.0'", /*to_type=*/"double",
      /*to_init=*/"42.0");
}

TEST_CASE(IsolateReload_ExistingFieldUnboxedToUnboxed) {
  // Note: underlying field will not actually be unboxed.
  TestReloadWithFieldChange(
      /*prefix=*/"double a = 1.5;",
      /*suffix=*/"Float32x4 b = Float32x4(1.0, 2.0, 3.0, 4.0);", /*verify=*/
      "doubleEq(value.a, 1.5); float32x4Eq(value.b, Float32x4(1.0, 2.0, 3.0, "
      "4.0));",
      /*from_type=*/"double", /*from_init=*/"42.0", /*to_type=*/"Float32x4",
      /*to_init=*/"Float32x4(1.0, 2.0, 3.0, 4.0)");
}

TEST_CASE(IsolateReload_ExistingStaticFieldChangesType) {
  const char* kScript = R"(
    int value = init();
    init() => 42;
    main() {
      return value.toString();
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("42", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    double value = init();
    init() => 42.0;
    main() {
      try {
        return value.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "type 'int' is not a subtype of type 'double' of 'function result'",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingFieldChangesTypeIndirect) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B extends A {}
    class Foo {
      A x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      value = Foo(B());
      return 'Okay';
    }
  )", late_tag), std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B {}
    class Foo {
      A x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      try {
        return value.x.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )", late_tag), std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ("type 'B' is not a subtype of type 'A' of 'function result'",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingStaticFieldChangesTypeIndirect) {
  const char* kScript = R"(
    class A {}
    class B extends A {}
    A value = init();
    init() => new B();
    main() {
      return value.toString();
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Instance of 'B'", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  const char* kReloadScript = R"(
    class A {}
    class B {}
    A value = init();
    init() => new A();
    main() {
      try {
        return value.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("type 'B' is not a subtype of type 'A' of 'function result'",
               SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingFieldChangesTypeIndirectGeneric) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B extends A {}
    class Foo {
      List<A> x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      value = Foo(List<B>.empty());
      return 'Okay';
    }
  )", late_tag), std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B {}
    class Foo {
      List<A> x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      try {
        return value.x.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )", late_tag), std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "type 'List<B>' is not a subtype of type 'List<A>' of 'function result'",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingStaticFieldChangesTypeIndirectGeneric) {
  const char* kScript = R"(
    class A {}
    class B extends A {}
    List<A> value = init();
    init() => List<B>.empty();
    main() {
      return value.toString();
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("[]", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  const char* kReloadScript = R"(
    class A {}
    class B {}
    List<A> value = init();
    init() => List<A>.empty();
    main() {
      try {
        return value.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "type 'List<B>' is not a subtype of type 'List<A>' of 'function result'",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingFieldChangesTypeIndirectFunction) {
  const char* late_tag = TestCase::LateTag();
  // clang-format off
  auto kScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B extends A {}
    typedef bool Predicate(B b);
    class Foo {
      Predicate x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      value = Foo((A a) => true);
      return 'Okay';
    }
  )", late_tag), std::free);
  // clang-format on

  Dart_Handle lib = TestCase::LoadTestScript(kScript.get(), NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Okay", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  // clang-format off
  auto kReloadScript = Utils::CStringUniquePtr(OS::SCreate(nullptr, R"(
    class A {}
    class B {}
    typedef bool Predicate(B b);
    class Foo {
      Predicate x;
      Foo(this.x);
    }
    %s Foo value;
    main() {
      try {
        return value.x.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )", late_tag), std::free);
  // clang-format on

  lib = TestCase::ReloadTestScript(kReloadScript.get());
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "type '(A) => bool' is not a subtype of type '(B) => bool' of 'function "
      "result'",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_ExistingStaticFieldChangesTypeIndirectFunction) {
  const char* kScript = R"(
    class A {}
    class B extends A {}
    typedef bool Predicate(B b);
    Predicate value = init();
    init() => (A a) => true;
    main() {
      return value.toString();
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("Closure: (A) => bool", SimpleInvokeStr(lib, "main"));

  // B is no longer a subtype of A.
  const char* kReloadScript = R"(
    class A {}
    class B {}
    typedef bool Predicate(B b);
    Predicate value = init();
    init() => (B a) => true;
    main() {
      try {
        return value.toString();
      } catch (e) {
        return e.toString();
      }
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ(
      "type '(A) => bool' is not a subtype of type '(B) => bool' of 'function "
      "result'",
      SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_TypedefToNotTypedef) {
  // The CFE lowers typedefs to function types and as such the VM will not see
  // any name collision between a class and a typedef class (which doesn't exist
  // anymore).
  const char* kScript =
      "typedef bool Predicate(dynamic x);\n"
      "main() {\n"
      "  return (42 is Predicate).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("false", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "class Predicate {\n"
      "  bool call(dynamic x) { return false; }\n"
      "}\n"
      "main() {\n"
      "  return (42 is Predicate).toString();\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(result);
}

TEST_CASE(IsolateReload_NotTypedefToTypedef) {
  const char* kScript =
      "class Predicate {\n"
      "  bool call(dynamic x) { return false; }\n"
      "}\n"
      "main() {\n"
      "  return (42 is Predicate).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("false", SimpleInvokeStr(lib, "main"));

  // The CFE lowers typedefs to function types and as such the VM will not see
  // any name collision between a class and a typedef class (which doesn't exist
  // anymore).
  const char* kReloadScript =
      "typedef bool Predicate(dynamic x);\n"
      "main() {\n"
      "  return (42 is Predicate).toString();\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(result);
}

TEST_CASE(IsolateReload_TypedefAddParameter) {
  const char* kScript =
      "typedef bool Predicate(dynamic x);\n"
      "main() {\n"
      "  bool foo(x) => true;\n"
      "  return (foo is Predicate).toString();\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("true", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "typedef bool Predicate(dynamic x, dynamic y);\n"
      "main() {\n"
      "  bool foo(x) => true;\n"
      "  return (foo is Predicate).toString();\n"
      "}\n";

  Dart_Handle result = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(result);
  EXPECT_STREQ("false", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_PatchStaticInitializerWithClosure) {
  const char* kScript =
      "dynamic field = (a) => 'a$a';\n"
      "main() {\n"
      "  dynamic f = field;\n"
      "  return f('b');\n"
      "}\n";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("ab", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript =
      "extraFunction() => 'Just here to change kernel offsets';\n"
      "dynamic field = (_, __) => 'Not executed';\n"
      "main() {\n"
      "  dynamic f = field;\n"
      "  return f('c');\n"
      "}\n";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("ac", SimpleInvokeStr(lib, "main"));
}

TEST_CASE(IsolateReload_StaticTargetArityChange) {
  const char* kScript = R"(
    class A {
      final x;
      final y;
      const A(this.x, this.y);
    }

    dynamic closure;

    main() {
      closure = () => A(1, 2);
      return "okay";
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  const char* kReloadScript = R"(
    class A {
      final x;
      const A(this.x);
    }

    dynamic closure;

    main() {
      // Call the old closure, which will try to call A(1, 2).
      closure();

      return "okay";
    }
  )";

  lib = TestCase::ReloadTestScript(kReloadScript);
  EXPECT_VALID(lib);
  EXPECT_ERROR(SimpleInvokeError(lib, "main"),
               "Unhandled exception:\n"
               "NoSuchMethodError: No constructor 'A.' "
               "with matching arguments declared in class 'A'.");
}

TEST_CASE(IsolateReload_SuperGetterReboundToMethod) {
  const char* kScript = R"(
    import 'file:///test:isolate_reload_helper';

    class A {
      get x => "123";
    }

    class B extends A {
      f() {
        var old_x = super.x;
        reloadTest();
        var new_x = super.x;
        return "$old_x:$new_x";
      }
    }

    main() {
      return B().f().toString();
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);

  const char* kReloadScript = R"(
    import 'file:///test:isolate_reload_helper';

    class A {
      x() => "123";
    }

    class B extends A {
      f() {
        var old_x = super.x;
        reloadTest();
        var new_x = super.x;
        return "$old_x:$new_x";
      }
    }

    main() {
      return B().f();
    }
  )";

  EXPECT_VALID(TestCase::SetReloadTestScript(kReloadScript));

  EXPECT_STREQ("123:Closure: () => dynamic from Function 'x':.",
               SimpleInvokeStr(lib, "main"));
}

// Regression test for b/179030011: incorrect lifetime management when reloading
// with multicomponent Kernel binary. When loading kernel blobs through tag
// handler (Dart_kKernelTag) we need to make sure to preserve a link between
// KernelProgramInfo objects and original typed data, because it might be
// coming with a finalizer, which otherwise might end up being called
// prematurely.
namespace {

// Compile the given |source| to Kernel binary.
static void CompileToKernel(Dart_SourceFile source,
                            const uint8_t** kernel_buffer,
                            intptr_t* kernel_buffer_size) {
  Dart_SourceFile sources[] = {source};
  char* error = TestCase::CompileTestScriptWithDFE(
      sources[0].uri, ARRAY_SIZE(sources), sources, kernel_buffer,
      kernel_buffer_size,
      /*incrementally=*/false);
  EXPECT(error == NULL);
  EXPECT_NOTNULL(kernel_buffer);
}

// LibraryTagHandler which returns a fixed Kernel binary back every time it
// receives a Dart_kKernelTag request. The binary is wrapped in an external
// typed data with a finalizer attached to it. If this finalizer is called
// it will set |was_finalized_| to true.
class KernelTagHandler {
 public:
  KernelTagHandler(uint8_t* kernel_buffer, intptr_t kernel_buffer_size)
      : kernel_buffer_(kernel_buffer), kernel_buffer_size_(kernel_buffer_size) {
    Dart_SetLibraryTagHandler(&LibraryTagHandler);
    instance_ = this;
  }

  ~KernelTagHandler() {
    Dart_SetLibraryTagHandler(nullptr);
    instance_ = nullptr;
  }

  static KernelTagHandler* Current() { return instance_; }

  bool was_called() const { return was_called_; }
  bool was_finalized() const { return was_finalized_; }

 private:
  static void Finalizer(void* isolate_callback_data, void* peer) {
    if (auto handler = KernelTagHandler::Current()) {
      handler->was_finalized_ = true;
    }
  }

  static Dart_Handle LibraryTagHandler(Dart_LibraryTag tag,
                                       Dart_Handle library,
                                       Dart_Handle url) {
    if (tag == Dart_kKernelTag) {
      auto handler = KernelTagHandler::Current();
      handler->was_called_ = true;

      Dart_Handle result = Dart_NewExternalTypedData(
          Dart_TypedData_kUint8, handler->kernel_buffer_,
          handler->kernel_buffer_size_);
      Dart_NewFinalizableHandle(result, handler->kernel_buffer_,
                                handler->kernel_buffer_size_, &Finalizer);
      return result;
    }
    UNREACHABLE();
    return Dart_Null();
  }

  static KernelTagHandler* instance_;
  uint8_t* kernel_buffer_;
  intptr_t kernel_buffer_size_;
  bool was_finalized_ = false;
  bool was_called_ = false;
};

KernelTagHandler* KernelTagHandler::instance_ = nullptr;
}  // namespace

TEST_CASE(IsolateReload_RegressB179030011) {
  struct Component {
    Dart_SourceFile source;
    const uint8_t* kernel_buffer;
    intptr_t kernel_buffer_size;
  };

  // clang-format off
  std::array<Component, 2> components = {{
    {{
      "file:///test-app",
      R"(
        class A {}
        void main() {
          A();
        }
      )"
    }, nullptr, 0},
    {{
      "file:///library",
      R"(
        class B {}
      )"
    }, nullptr, 0}
  }};
  // clang-format on

  for (auto& component : components) {
    CompileToKernel(component.source, &component.kernel_buffer,
                    &component.kernel_buffer_size);
    TestCaseBase::AddToKernelBuffers(component.kernel_buffer);
  }

  // Concatenate all components.
  intptr_t kernel_buffer_size = 0;
  for (auto component : components) {
    kernel_buffer_size += component.kernel_buffer_size;
  }
  uint8_t* kernel_buffer = static_cast<uint8_t*>(malloc(kernel_buffer_size));
  TestCaseBase::AddToKernelBuffers(kernel_buffer);
  intptr_t pos = 0;
  for (auto component : components) {
    memcpy(kernel_buffer + pos, component.kernel_buffer,  // NOLINT
           component.kernel_buffer_size);
    pos += component.kernel_buffer_size;
  }

  // Load the first component into the isolate (to have something set as
  // root library).
  Dart_Handle lib = Dart_LoadLibraryFromKernel(
      components[0].kernel_buffer, components[0].kernel_buffer_size);
  EXPECT_VALID(lib);
  EXPECT_VALID(Dart_SetRootLibrary(lib));

  {
    KernelTagHandler handler(kernel_buffer, kernel_buffer_size);
    {
      // Additional API scope to prevent handles leaking into outer scope.
      Dart_EnterScope();
      // root_script_url does not really matter.
      TestCase::TriggerReload(/*root_script_url=*/"something.dill");
      Dart_ExitScope();
    }
    EXPECT(handler.was_called());

    // Check that triggering GC does not cause finalizer registered by
    // tag handler to fire - meaning that kernel binary continues to live.
    TransitionNativeToVM transition(thread);
    GCTestHelper::CollectAllGarbage();
    EXPECT(!handler.was_finalized());
  }
}

// Regression test for https://github.com/dart-lang/sdk/issues/50148.
TEST_CASE(IsolateReload_GenericConstructorTearOff) {
  const char* kScript = R"(
    typedef Create<T, R> = T Function(R ref);

    class Base<Input> {
      Base(void Function(Create<void, Input> create) factory) : _factory = factory;

      final void Function(Create<void, Input> create) _factory;

      void fn() => _factory((ref) {});
    }

    class Check<T> {
      Check(Create<Object?, List<T>> create);
    }

    final f = Base<List<int>>(Check<int>.new);

    main() {
      f.fn();
      return "okay";
    }
  )";

  Dart_Handle lib = TestCase::LoadTestScript(kScript, NULL);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));

  lib = TestCase::ReloadTestScript(kScript);
  EXPECT_VALID(lib);
  EXPECT_STREQ("okay", SimpleInvokeStr(lib, "main"));
}

// Regression test for https://github.com/dart-lang/sdk/issues/51215.
TEST_CASE(IsolateReload_ImplicitGetterWithLoadGuard) {
  const char* kLibScript = R"(
    import 'file:///test:isolate_reload_helper';

    class A {
      int x;
      A(this.x);
      A.withUinitializedObject(int Function() callback) : x = callback();
    }

    A a = A(3);

    main() {
      int sum = 0;
      // Trigger OSR and optimize this function.
      for (int i = 0; i < 30000; ++i) {
        sum += i;
      }
      // Make sure A.get:x is compiled.
      int y = a.x;
      // Reload while having an uninitialized
      // object A on the stack. This should result in
      // a load guard for A.x.
      A.withUinitializedObject(() {
         reloadTest();
         return 4;
      });
      // Trigger OSR and optimize this function once again.
      for (int i = 0; i < 30000; ++i) {
        sum += i;
      }
      // Trigger deoptimization in A.get:x.
      // It should correctly deoptimize into an implicit
      // getter with a load guard.
      a.x = 0x8070605040302010;
      int z = a.x & 0xffff;
      return "y: $y, z: $z";
    }
  )";

  Dart_Handle lib1 =
      TestCase::LoadTestLibrary("test_lib1.dart", kLibScript, nullptr);
  EXPECT_VALID(lib1);

  const char* kMainScript = R"(
    main() {}
  )";

  // Trigger hot reload during execution of 'main' from test_lib1
  // without reloading test_lib1, so its unoptimized code is retained.
  EXPECT_VALID(TestCase::LoadTestScript(kMainScript, nullptr));
  EXPECT_VALID(TestCase::SetReloadTestScript(kMainScript));

  EXPECT_STREQ("y: 3, z: 8208", SimpleInvokeStr(lib1, "main"));
}

#endif  // !defined(PRODUCT) && !defined(DART_PRECOMPILED_RUNTIME)

}  // namespace dart
