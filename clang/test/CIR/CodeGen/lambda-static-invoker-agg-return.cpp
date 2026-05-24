// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
//

//===----------------------------------------------------------------------===//
// Tests that CIRGen does not emit illegal copies for return values of
// non-trivially-copyable struct types through the lambda static invoker
// path (emitForwardingCallToLambda).
//
// The expected CIR pattern is direct SSA return (cir.return %call_result)
// without any alloca/store/load indirection.
//
// emitForwardingCallToLambda uses forNoAggregateStore
// when returnValue is absent, forwarding the SSA value directly via
// cir.return without any alloca/store/load.
//===----------------------------------------------------------------------===//

struct S {
  int x;
  S();
  S(const S &);
  S &operator=(const S &);
};

S make_s();

S agg_invoker() {
  auto *fn = +[](int i) -> S { return make_s(); };
  return fn(3);
}

// CIR-LABEL: cir.func no_inline internal private dso_local @_ZZ11agg_invokervEN3$_08__invokeEi
// CIR-SAME:    (%[[I_ARG:.*]]: !s32i {{.*}}) -> !rec_S
// CIR:         %[[I_ALLOCA:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["i", init]
// CIR-NOT:     __retval
// CIR:         %[[UNUSED:.*]] = cir.alloca !rec_anon{{[^,]*}}, {{.*}} ["unused.capture"]
// CIR-NOT:     cir.alloca {{.*}} ["agg.tmp
// CIR:         cir.store %[[I_ARG]], %[[I_ALLOCA]]
// CIR:         %[[I:.*]] = cir.load{{.*}} %[[I_ALLOCA]]
// CIR:         %[[CALL:.*]] = cir.call @_ZZ11agg_invokervENK3$_0clEi(%[[UNUSED]], %[[I]]){{.*}} -> !rec_S
// CIR-NOT:     cir.copy
// CIR-NOT:     cir.store{{.*}}%[[CALL]]
// CIR:         cir.return %[[CALL]] : !rec_S

// LLVM-LABEL: define internal %struct.S @"_ZZ11agg_invokervEN3$_08__invokeEi"
// LLVM-SAME:    (i32 {{[^,)]*}} %[[I_ARG:[^,)]+]])
// LLVM:         %[[I_ALLOCA:.*]] = alloca i32
// LLVM-NOT:     alloca %struct.S
// LLVM:         %[[UNUSED:.*]] = alloca %class.anon
// LLVM:         store i32 %[[I_ARG]], ptr %[[I_ALLOCA]]
// LLVM:         %[[I:.*]] = load i32, ptr %[[I_ALLOCA]]
// LLVM:         %[[CALL:.*]] = call %struct.S @"_ZZ11agg_invokervENK3$_0clEi"(ptr {{.*}} %[[UNUSED]], i32 {{.*}} %[[I]])
// LLVM-NOT:     store %struct.S
// LLVM:         ret %struct.S %[[CALL]]

// OGCG-LABEL: define internal void @"_ZZ11agg_invokervEN3$_08__invokeEi"
// OGCG-SAME:    (ptr {{.*}} sret(%struct.S) {{[^,]*}} %[[AGG_RESULT:[^,]+]], i32 {{[^,)]*}} %[[I_ARG:[^,)]+]])
// OGCG:         call void @"_ZZ11agg_invokervENK3$_0clEi"(ptr {{.*}} sret(%struct.S) {{[^,]*}} %[[AGG_RESULT]], ptr {{.*}} %[[UNUSED:.*]], i32 {{.*}})
// OGCG:         ret void
