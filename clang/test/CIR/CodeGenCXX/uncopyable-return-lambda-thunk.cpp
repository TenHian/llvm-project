// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-unknown -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-unknown -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-unknown -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
//

//
//===----------------------------------------------------------------------===//
// Tests that CIRGen does not emit illegal copies for return values of
// non-trivially-copyable struct types in lambda static invoker and vtable
// thunk paths.  These paths (emitForwardingCallToLambda,
// emitCallAndReturnForThunk) use agg.tmp (not __retval) for temporary
// allocas, but the store/load through agg.tmp is equally a semantic error:
// it copies the contents of non-trivially-copyable types bitwise.
//
// The expected CIR pattern is direct SSA return (cir.return %call_result)
// without any alloca/store/load indirection.
//
// emitForwardingCallToLambda and emitCallAndReturnForThunk use
// forNoAggregateStore when returnValue is absent, forwarding the
// SSA value directly via cir.return.
//===----------------------------------------------------------------------===//

// --- Test 22: Lambda static invoker with deleted copy ctor type ---
// When a captureless lambda returning a non-copyable type is converted to a
// function pointer (+), the static invoker route (emitForwardingCallToLambda)
// must avoid any alloca/store/load for the return type.
namespace deleted_lambda {
struct S {
  S();
  S(const S &) = delete;
  S(S &&) = delete;
  ~S();
};

S make_s();

void test() {
  auto fn = +[]() -> S { return make_s(); };
  fn();
}

// CIR-LABEL: cir.func {{.*}} @_ZZN14deleted_lambda4testEvEN3$_08__invokeEv
// CIR-NOT:     __retval
// CIR-NOT:     ["agg.tmp
// CIR:         %[[CALL:.*]] = cir.call @_ZZN14deleted_lambda4testEvENK3$_0clEv
// CIR-NOT:     cir.store {{.*}}%[[CALL]]
// CIR-NOT:     cir.load
// CIR:         cir.return %[[CALL]]

// LLVM-LABEL: define {{.*}} @"_ZZN14deleted_lambda4testEvEN3$_08__invokeEv"
// LLVM-NOT:     alloca %"struct.deleted_lambda
// LLVM:         call %"struct.deleted_lambda::S" @"_ZZN14deleted_lambda4testEvENK3$_0clEv"
// LLVM-NOT:     store %"struct.deleted_lambda
// LLVM:         ret %"struct.deleted_lambda

}

// --- Test 23: Vtable thunk with deleted copy ctor type ---
// When a class overrides a virtual function through multiple inheritance,
// a this-adjusting thunk is emitted. The thunk (emitCallAndReturnForThunk)
// must avoid any alloca/store/load for the return type.
namespace deleted_thunk {
struct S {
  S();
  S(const S &) = delete;
  S(S &&) = delete;
  ~S();
};

struct A {
  virtual S foo();
};

struct B {
  virtual S foo();
};

struct C : A, B {
  S foo() override;
};

S C::foo() { return S(); }

// CIR-LABEL: cir.func {{.*}} @_ZN13deleted_thunk1C3fooEv
// CIR-NOT: __retval
// CIR: cir.func {{.*}} @_ZThn8_N13deleted_thunk1C3fooEv
// CIR-NOT: __retval
// CIR-NOT: ["agg.tmp
// CIR: %[[T23_CALL:.*]] = cir.call @_ZN13deleted_thunk1C3fooEv
// CIR-NOT: cir.store {{.*}}%[[T23_CALL]]
// CIR-NOT: cir.load
// CIR: cir.return %[[T23_CALL]]

// LLVM-LABEL: define {{.*}} @_ZThn8_N13deleted_thunk1C3fooEv
// LLVM-NOT: alloca %"struct.deleted_thunk
// LLVM: call %"struct.deleted_thunk::S" @_ZN13deleted_thunk1C3fooEv
// LLVM-NOT: store %"struct.deleted_thunk
// LLVM: ret %"struct.deleted_thunk

}

// --- Test 24: Lambda invoker with copy deleted via member ---
// TC × P4: deleted via member transitivity, lambda static invoker path.
// The deleted-by-member type must not go through any alloca/store/load.
namespace deleted_by_member_lambda {
struct B {
  B();
  B(const B &) = delete;
  B(B &&) = delete;
  ~B();
};
struct S {
  S();
  B b;
  ~S();
};

S make_s();
void test() {
  auto fn = +[]() -> S { return make_s(); };
  fn();
}

// CIR-LABEL: cir.func {{.*}}__invokeEv
// CIR-NOT:     __retval
// CIR-NOT:     ["agg.tmp
// CIR:         %[[T24_CALL:.*]] = cir.call
// CIR-NOT:     cir.store {{.*}}%[[T24_CALL]]
// CIR-NOT:     cir.load
// CIR:         cir.return %[[T24_CALL]]

// LLVM-LABEL: define {{.*}}__invokeEv
// LLVM-NOT:     alloca %"struct.deleted_by_member_lambda
// LLVM:         call %"struct.deleted_by_member_lambda::S"
// LLVM-NOT:     store %"struct.deleted_by_member_lambda
// LLVM:         ret %"struct.deleted_by_member_lambda

}

// --- Test 25: Vtable thunk with copy deleted via base ---
// TD × P5: deleted via base transitivity, thunk path.
// The deleted-by-base type must not go through any alloca/store/load.
namespace deleted_by_base_thunk {
struct B {
  B();
  B(const B &) = delete;
  B(B &&) = delete;
  ~B();
};
struct S : B {
  S();
  ~S();
};
struct A { virtual S foo(); };
struct B2 { virtual S foo(); };
struct C : A, B2 { S foo() override; };
S C::foo() { return S(); }

// CIR-LABEL: cir.func {{.*}} @_ZN21deleted_by_base_thunk1C3fooEv
// CIR-NOT: __retval
// CIR: cir.func {{.*}} @_ZThn
// CIR-NOT: __retval
// CIR-NOT: ["agg.tmp
// CIR: %[[T25_CALL:.*]] = cir.call @_ZN21deleted_by_base_thunk1C3fooEv
// CIR-NOT: cir.store {{.*}}%[[T25_CALL]]
// CIR-NOT: cir.load
// CIR: cir.return %[[T25_CALL]]

// LLVM-LABEL: define {{.*}} @_ZThn
// LLVM-NOT: alloca %"struct.deleted_by_base_thunk
// LLVM: call %"struct.deleted_by_base_thunk::S" @_ZN21deleted_by_base_thunk1C3fooEv
// LLVM-NOT: store %"struct.deleted_by_base_thunk
// LLVM: ret %"struct.deleted_by_base_thunk
}
// OGCG checks for all tests, ordered to match OGCG function emission order
// (thunks before lambda static invokers).

// Test 23: Vtable thunk with deleted copy ctor type.
// OGCG-LABEL: define {{.*}} void @_ZThn8_N13deleted_thunk1C3fooEv
// OGCG:         ret void

// Test 25: Vtable thunk with copy deleted via base.
// OGCG-LABEL: define {{.*}} void @_ZThn
// OGCG:         ret void

// Test 22: Lambda static invoker with deleted copy ctor type.
// OGCG-LABEL: define {{.*}} void @"_ZZN14deleted_lambda4testEvEN3$_08__invokeEv"
// OGCG:         ret void

// Test 24: Lambda invoker with copy deleted via member.
// OGCG-LABEL: define {{.*}} void @"_ZZN24deleted_by_member_lambda4testEvEN3$_08__invokeEv"
// OGCG:         ret void
