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
// non-trivially-copyable struct types whose copy constructor is non-trivial
// but not deleted (deleted case covered in uncopyable-return.cpp).
//
// Because ABI calling convention lowering is deferred in CIR, we do not use
// sret parameters for functions that return a non-copyable struct. This is
// acceptable as long as CIRGen does not introduce a store/load round-trip
// through __retval.
//
// The expected CIR pattern for such functions is a direct forwarding of the
// call result to cir.return, without any alloca/store/load indirection.
//
// Uses -std=c++17 for guaranteed copy elision (P0135), so that returning a
// prvalue of a non-copyable type is well-formed without requiring an
// accessible copy or move constructor.
//
// emitAndUpdateRetAlloca uses hasNonTrivialCopyConstructor() which checks
// for both deleted and user-defined copy ctors recursively, and
// emitForwardingCallToLambda uses forNoAggregateStore when returnValue
// is absent.
//===----------------------------------------------------------------------===//

// --- Test 13: Forwarding return of a struct with user-defined copy ctor ---
// Copy constructor is user-defined (non-trivial) but NOT deleted. The store/load
// through __retval would bypass the actual copy constructor logic.
namespace non_trivial_user_copy {
struct S {
  S();
  S(const S &) {}
  S(S &&) {}
  ~S();
};

S foo();
S bar() { return foo(); }

// CIR-LABEL: cir.func {{.*}} @_ZN21non_trivial_user_copy3barEv
// CIR-NOT:     __retval
// CIR:         %{{[0-9]+}} = cir.call @_ZN21non_trivial_user_copy3fooEv() : () -> !rec{{.*}}
// CIR-NEXT:    cir.return %{{[0-9]+}} : !rec{{.*}}

// LLVM-LABEL: define {{.*}} @_ZN21non_trivial_user_copy3barEv(

// OGCG-LABEL: define {{.*}} void @_ZN21non_trivial_user_copy3barEv(
}

// --- Test 14: Non-trivial copy ctor with non-trivial destructor ---
// The presence of a non-trivial destructor wraps the call in
// CXXBindTemporaryExpr. The unwrapping must correctly extract the CallExpr.
namespace non_trivial_copy_dtor {
struct S {
  S();
  S(const S &) {}
  S(S &&) {}
  ~S();
};

S foo();
S bar() { return foo(); }

// CIR-LABEL: cir.func {{.*}} @_ZN21non_trivial_copy_dtor3barEv
// CIR-NOT:     __retval
// CIR:         %{{[0-9]+}} = cir.call @_ZN21non_trivial_copy_dtor3fooEv() : () -> !rec{{.*}}
// CIR-NEXT:    cir.return %{{[0-9]+}} : !rec{{.*}}

// LLVM-LABEL: define {{.*}} @_ZN21non_trivial_copy_dtor3barEv(

// OGCG-LABEL: define {{.*}} void @_ZN21non_trivial_copy_dtor3barEv(
}

// --- Test 15: Non-trivial copy ctor via member (transitivity) ---
// A struct whose copy constructor is non-trivial because a member has a
// user-defined copy constructor. The check must recurse into members.
namespace non_trivial_by_member {
struct B {
  B();
  B(const B &) {}
  B(B &&) {}
  ~B();
};
struct A {
  A();
  B b;
  ~A();
};

A foo();
A bar() { return foo(); }

// CIR-LABEL: cir.func {{.*}} @_ZN21non_trivial_by_member3barEv
// CIR-NOT:     __retval
// CIR:         %{{[0-9]+}} = cir.call @_ZN21non_trivial_by_member3fooEv() : () -> !rec{{.*}}
// CIR-NEXT:    cir.return %{{[0-9]+}} : !rec{{.*}}

// LLVM-LABEL: define {{.*}} @_ZN21non_trivial_by_member3barEv(

// OGCG-LABEL: define {{.*}} void @_ZN21non_trivial_by_member3barEv(
}

// --- Test 16: Non-trivial copy ctor via base class (inheritance chain) ---
// A struct whose copy constructor is non-trivial because a base class has a
// user-defined copy constructor. The check must recurse into bases.
namespace non_trivial_by_base {
struct B {
  B();
  B(const B &) {}
  B(B &&) {}
  ~B();
};
struct A : B {
  A();
  ~A();
};

A foo();
A bar() { return foo(); }

// CIR-LABEL: cir.func {{.*}} @_ZN19non_trivial_by_base3barEv
// CIR-NOT:     __retval
// CIR:         %{{[0-9]+}} = cir.call @_ZN19non_trivial_by_base3fooEv() : () -> !rec{{.*}}
// CIR-NEXT:    cir.return %{{[0-9]+}} : !rec{{.*}}

// LLVM-LABEL: define {{.*}} @_ZN19non_trivial_by_base3barEv(

// OGCG-LABEL: define {{.*}} void @_ZN19non_trivial_by_base3barEv(
}

// --- Test 17: Constructor expression return with non-trivial type ---
// return S(42) is a CXXTemporaryObjectExpr, not a CallExpr. The constructor
// returns void; no SSA value available. Must use agg.tmp + load.
namespace non_trivial_construct {
struct S {
  int x;
  S(int v) : x(v) {}
  S(const S &) {}
  S(S &&) {}
  ~S();
};

S make() { return S(42); }

// CIR-LABEL: cir.func {{.*}} @_ZN21non_trivial_construct4makeEv
// CIR-NOT:     __retval
// CIR:         %{{[0-9]+}} = cir.alloca {{.*}} ["agg.tmp"
// CIR:         cir.call @_ZN21non_trivial_construct1SC1Ei(
// CIR:         %{{[0-9]+}} = cir.load{{.*}} %{{[0-9]+}} : !cir.ptr<!rec{{.*}}>, !rec{{.*}}
// CIR-NEXT:    cir.return %{{[0-9]+}} : !rec{{.*}}

// LLVM-LABEL: define {{.*}} @_ZN21non_trivial_construct4makeEv(

// OGCG-LABEL: define {{.*}} void @_ZN21non_trivial_construct4makeEv(
}

// --- Test 18: Conditional return with non-trivial type ---
// return cond ? a() : b() — each branch must independently avoid __retval.
namespace non_trivial_conditional {
struct S {
  S();
  S(const S &) {}
  S(S &&) {}
  ~S();
};

S a();
S b();
S pick(bool c) { return c ? a() : b(); }

// CIR-LABEL: cir.func {{.*}} @_ZN23non_trivial_conditional4pickEb
// CIR-NOT:     __retval
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_ZN23non_trivial_conditional4pickEb(

// OGCG-LABEL: define {{.*}} void @_ZN23non_trivial_conditional4pickEb(
}

// --- Test 19: Virtual function returning non-trivial type ---
// Virtual dispatch should not introduce __retval copies for non-trivial types.
namespace non_trivial_virtual {
struct S {
  S();
  S(const S &) {}
  S(S &&) {}
  ~S();
};

struct Base {
  virtual S foo();
};

struct Derived : Base {
  S foo() override;
};

S caller(Base &b) { return b.foo(); }

// CIR-LABEL: cir.func {{.*}} @_ZN19non_trivial_virtual6callerERNS_4BaseE
// CIR-NOT:     __retval
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_ZN19non_trivial_virtual6callerERNS_4BaseE(

// OGCG-LABEL: define {{.*}} void @_ZN19non_trivial_virtual6callerERNS_4BaseE(
}

// --- Test 20: Template instantiation with non-trivial type ---
// Template parameter substitution must also avoid __retval for non-trivial types.
namespace non_trivial_template {
struct S {
  S();
  S(const S &) {}
  S(S &&) {}
  ~S();
};

template <typename T>
T make();

S bar() { return make<S>(); }

// CIR-LABEL: cir.func {{.*}} @_ZN20non_trivial_template3barEv
// CIR-NOT:     __retval
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_ZN20non_trivial_template3barEv(

// OGCG-LABEL: define {{.*}} void @_ZN20non_trivial_template3barEv(
}
// --- Test 27: Lambda invoker with non-trivial copy ctor via member ---
// TF × P4: non-trivial via member transitivity, lambda static invoker path.
// The invoker must not go through __retval.  emitForwardingCallToLambda
// uses forNoAggregateStore when returnValue is absent, forwarding the
// SSA value directly.
namespace non_trivial_by_member_lambda {
struct B {
  B();
  B(const B &) {}
  B(B &&) {}
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
// CIR:         cir.return

// LLVM-LABEL: define {{.*}}__invokeEv
// LLVM-NOT:     __retval

// OGCG-LABEL: define {{.*}} void {{.*}}__invokeEv
// OGCG:         ret void
}
