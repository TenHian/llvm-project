// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fno-rtti -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fno-rtti -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fno-rtti -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
//

//===----------------------------------------------------------------------===//
// Tests that CIRGen does not emit illegal copies for return values of
// non-trivially-copyable struct types through the vtable this-adjusting
// thunk path (emitCallAndReturnForThunk).
//
// The expected CIR pattern is direct SSA return (cir.return %call_result)
// without any alloca/store/load indirection.
//
// emitCallAndReturnForThunk uses forNoAggregateStore
// when returnValue is absent, forwarding the SSA value directly via
// cir.return without any alloca/store/load.
//===----------------------------------------------------------------------===//

namespace Test5 {
// Non-virtual this-adjusting thunk for a method that returns an aggregate
// with no trivial copy/move.

struct NonTrivial {
  int x;
  NonTrivial();
  NonTrivial(const NonTrivial &);
  NonTrivial &operator=(const NonTrivial &);
};

struct A {
  virtual NonTrivial h();
};

struct B {
  virtual NonTrivial h();
};

struct C : A, B {
  NonTrivial h() override;
};

NonTrivial C::h() { return NonTrivial(); }

} // namespace Test5

// --- Test5: aggregate non-trivial return type thunk ---

// CIR: cir.func {{.*}} @_ZN5Test51C1hEv

// CIR: cir.func {{.*}} @_ZThn8_N5Test51C1hEv(%arg0: !cir.ptr<
// CIR:   %[[T5_THIS_ADDR:.*]] = cir.alloca {{.*}} ["this", init]
// CIR-NOT: __retval
// CIR:   cir.store %arg0, %[[T5_THIS_ADDR]]
// CIR:   %[[T5_THIS:.*]] = cir.load %[[T5_THIS_ADDR]]
// CIR:   %[[T5_CAST:.*]] = cir.cast bitcast %[[T5_THIS]] : !cir.ptr<{{.*}}> -> !cir.ptr<!u8i>
// CIR:   %[[T5_OFFSET:.*]] = cir.const #cir.int<-8> : !s64i
// CIR:   %[[T5_ADJUSTED:.*]] = cir.ptr_stride %[[T5_CAST]], %[[T5_OFFSET]]
// CIR:   %[[T5_RESULT:.*]] = cir.cast bitcast %[[T5_ADJUSTED]] : !cir.ptr<!u8i> -> !cir.ptr<
// CIR:   %[[T5_CALL:.*]] = cir.call @_ZN5Test51C1hEv(%[[T5_RESULT]]){{.*}} -> !rec_Test5{{.*}}NonTrivial
// CIR:   cir.return %[[T5_CALL]]
// CIR-NOT: cir.trap
// CIR-NOT: cir.unreachable

// LLVM: define {{.*}} %"struct.Test5::NonTrivial" @_ZThn8_N5Test51C1hEv(ptr{{.*}})
// LLVM:   %[[L5_THIS:.*]] = load ptr, ptr
// LLVM:   %[[L5_ADJ:.*]] = getelementptr i8, ptr %[[L5_THIS]], i64 -8
// LLVM:   %[[L5_RET:.*]] = call{{.*}} %"struct.Test5::NonTrivial" @_ZN5Test51C1hEv(ptr{{.*}} %[[L5_ADJ]])
// LLVM-NOT: store %"struct.Test5::NonTrivial"
// LLVM:   ret %"struct.Test5::NonTrivial" %[[L5_RET]]

// OGCG: define {{.*}} void @_ZThn8_N5Test51C1hEv(ptr {{[^,]*}}sret(%"struct.Test5::NonTrivial"){{[^,]*}}, ptr{{.*}})
// OGCG:   %[[O5_THIS:.*]] = load ptr, ptr
// OGCG:   %[[O5_ADJ:.*]] = getelementptr inbounds i8, ptr %[[O5_THIS]], i64 -8
// OGCG:   {{.*}}call void @_ZN5Test51C1hEv(ptr {{.*}}sret(%"struct.Test5::NonTrivial"){{.*}}, ptr{{.*}} %[[O5_ADJ]])
