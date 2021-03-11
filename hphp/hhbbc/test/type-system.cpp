/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-present Facebook, Inc. (http://www.facebook.com)  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#include "hphp/hhbbc/type-system.h"

#include <gtest/gtest.h>
#include <boost/range/join.hpp>
#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <folly/Lazy.h>

#include "hphp/runtime/base/array-init.h"

#include "hphp/hhbbc/context.h"
#include "hphp/hhbbc/hhbbc.h"
#include "hphp/hhbbc/misc.h"
#include "hphp/hhbbc/representation.h"
#include "hphp/hhbbc/parse.h"
#include "hphp/hhbbc/index.h"
#include "hphp/runtime/base/tv-comparisons.h"
#include "hphp/runtime/vm/as.h"
#include "hphp/runtime/vm/native.h"
#include "hphp/runtime/vm/unit-emitter.h"

namespace HPHP { namespace HHBBC {

void PrintTo(const Type& t, ::std::ostream* os) { *os << show(t); }
void PrintTo(Emptiness e, ::std::ostream* os) {
  switch (e) {
    case Emptiness::Empty:    *os << "empty"; break;
    case Emptiness::NonEmpty: *os << "non-empty"; break;
    case Emptiness::Maybe:    *os << "maybe"; break;
    default: always_assert(false);
  }
}

Type make_obj_for_testing(trep, res::Class, DObj::Tag, bool);
Type make_cls_for_testing(trep, res::Class, DCls::Tag, bool);
Type make_record_for_testing(trep, res::Record, DRecord::Tag);
Type make_arrval_for_testing(trep, SArray);
Type make_arrpacked_for_testing(trep, std::vector<Type>);
Type make_arrpackedn_for_testing(trep, Type);
Type make_arrmap_for_testing(trep, MapElems, Type, Type);
Type make_arrmapn_for_testing(trep, Type, Type);

namespace {

//////////////////////////////////////////////////////////////////////

struct TypeTest : ::testing::Test {
 protected:
  void SetUp() override {
    RO::EvalHackArrDVArrs = false;
  }
  void TearDown() override {}
};

//////////////////////////////////////////////////////////////////////

const StaticString s_test("test");
const StaticString s_TestClass("TestClass");
const StaticString s_Base("Base");
const StaticString s_A("A");
const StaticString s_AA("AA");
const StaticString s_AB("AB");
const StaticString s_B("B");
const StaticString s_BA("BA");
const StaticString s_BB("BB");
const StaticString s_BAA("BAA");
const StaticString s_C("C");
const StaticString s_IBase("IBase");
const StaticString s_IA("IA");
const StaticString s_IAA("IAA");
const StaticString s_IB("IB");
const StaticString s_UniqueRecBase("UniqueRecBase");
const StaticString s_UniqueRec("UniqueRec");
const StaticString s_UniqueRecA("UniqueRecA");
const StaticString s_Awaitable("HH\\Awaitable");

// A test program so we can actually test things involving object or
// class or record types.
void add_test_unit(php::Program& program) {
  assertx(SystemLib::s_inited);
  std::string const hhas = R"(
    # Technically this should be provided by systemlib, but it's the
    # only one we have to make sure the type system can see for unit
    # test purposes, so we can just define it here.  We don't need to
    # give it any of its functions currently.
    .class [abstract unique builtin] HH\Awaitable {
    }

    .class [unique builtin] HH\AwaitableChild extends HH\Awaitable {
    }

    .class [interface unique] IBase {
    }

    .class [interface unique] IA implements (IBase) {
    }

    .class [interface unique] IB implements (IBase) {
    }

    .class [interface unique] IAA implements (IA) {
    }

    .class [unique] Base {
      .default_ctor;
    }

    .class [unique] A extends Base implements (IA) {
      .default_ctor;
    }

    .class [no_override unique] AA extends A implements (IAA) {
      .default_ctor;
    }

    .class [no_override unique] AB extends A {
      .default_ctor;
    }

    .class [unique] B extends Base {
      .default_ctor;
    }

    .class [unique] BA extends B {
      .default_ctor;
    }

    .class [no_override unique] BB extends B {
      .default_ctor;
    }

    .class [unique] BAA extends BA {
      .default_ctor;
    }

    # Make sure BAA doesn't get AttrNoOverride:
    .class [unique] BAADeriver extends BAA {
      .default_ctor;
    }

    .class [unique] TestClass {
      .default_ctor;
    }

    # Make sure TestClass doesn't get AttrNoOverride:
    .class [unique] TestClassDeriver extends TestClass {
      .default_ctor;
    }

    .record [abstract persistent unique] UniqueRecBase {}
    .record [final unique] UniqueRec extends UniqueRecBase {}
    .record [final unique] UniqueRecA extends UniqueRecBase {}

    .function test() {
      Int 1
      RetC
    }
  )";
  std::unique_ptr<UnitEmitter> ue(assemble_string(
    hhas.c_str(), hhas.size(),
    "ignore.php",
    SHA1("1234543212345432123454321234543212345432"),
    Native::s_noNativeFuncs
  ));
  parse_unit(program, ue.get());
}

php::ProgramPtr make_test_program() {
  RuntimeOption::EvalHackRecords = true;
  auto program = make_program();
  add_test_unit(*program);
  return program;
}

//////////////////////////////////////////////////////////////////////

Type make_specialized_string(trep bits, SString s) {
  return set_trep_for_testing(sval(s), bits);
}

Type make_specialized_int(trep bits, int64_t i) {
  return set_trep_for_testing(ival(i), bits);
}

Type make_specialized_double(trep bits, double d) {
  return set_trep_for_testing(dval(d), bits);
}

Type make_specialized_wait_handle(trep bits, Type inner, const Index& index) {
  return set_trep_for_testing(wait_handle(index, std::move(inner)), bits);
}

Type make_specialized_exact_object(trep bits, res::Class cls,
                                   bool isCtx = false) {
  return make_obj_for_testing(bits, cls, DObj::Exact, isCtx);
}

Type make_specialized_sub_object(trep bits, res::Class cls,
                                 bool isCtx = false) {
  return make_obj_for_testing(bits, cls, DObj::Sub, isCtx);
}

Type make_specialized_exact_class(trep bits, res::Class cls,
                                  bool isCtx = false) {
  return make_cls_for_testing(bits, cls, DCls::Exact, isCtx);
}

Type make_specialized_sub_class(trep bits, res::Class cls,
                                bool isCtx = false) {
  return make_cls_for_testing(bits, cls, DCls::Sub, isCtx);
}

Type make_specialized_exact_record(trep bits, res::Record rec) {
  return make_record_for_testing(bits, rec, DRecord::Exact);
}

Type make_specialized_sub_record(trep bits, res::Record rec) {
  return make_record_for_testing(bits, rec, DRecord::Sub);
}

Type make_specialized_arrval(trep bits, SArray ar) {
  return make_arrval_for_testing(bits, ar);
}

Type make_specialized_arrpacked(trep bits,
                                std::vector<Type> elems,
                                folly::Optional<LegacyMark> mark = folly::none) {
  return make_arrpacked_for_testing(bits, std::move(elems), mark);
}

Type make_specialized_arrpackedn(trep bits, Type type) {
  return make_arrpackedn_for_testing(bits, std::move(type));
}

Type make_specialized_arrmap(trep bits, MapElems elems,
                             Type optKey = TBottom, Type optVal = TBottom,
                             folly::Optional<LegacyMark> mark = folly::none) {
  return make_arrmap_for_testing(
    bits, std::move(elems), std::move(optKey), std::move(optVal), mark
  );
}

Type make_specialized_arrmapn(trep bits, Type key, Type val) {
  return make_arrmapn_for_testing(bits, std::move(key), std::move(val));
}

trep get_bits(const Type& t) { return get_trep_for_testing(t); }

Type make_unmarked(Type t) {
  if (!t.couldBe(BVecish|BDictish)) return t;
  return set_mark_for_testing(t, LegacyMark::Unmarked);
}

void make_unmarked(std::vector<Type>& types) {
  for (auto& t : types) t = make_unmarked(std::move(t));
}

//////////////////////////////////////////////////////////////////////

Type sval(const StaticString& s) { return HPHP::HHBBC::sval(s.get()); }
Type sval_nonstatic(const StaticString& s) {
  return HPHP::HHBBC::sval_nonstatic(s.get());
}
Type sval_counted(const StaticString& s) {
  return HPHP::HHBBC::sval_counted(s.get());
}

TypedValue tv(SString s) { return make_tv<KindOfPersistentString>(s); }
TypedValue tv(const StaticString& s) { return tv(s.get()); }
TypedValue tv(int64_t i) { return make_tv<KindOfInt64>(i); }

std::pair<TypedValue, MapElem> map_elem(int64_t i, Type t) {
  return {tv(i), MapElem::IntKey(std::move(t))};
}
std::pair<TypedValue, MapElem> map_elem(SString s, Type t) {
  return {tv(s), MapElem::SStrKey(std::move(t))};
}
std::pair<TypedValue, MapElem> map_elem(const StaticString& s, Type t) {
  return {tv(s), MapElem::SStrKey(std::move(t))};
}

std::pair<TypedValue, MapElem> map_elem_nonstatic(const StaticString& s,
                                                  Type t) {
  return {tv(s), MapElem::StrKey(std::move(t))};
}
std::pair<TypedValue, MapElem> map_elem_counted(const StaticString& s,
                                                Type t) {
  return {tv(s), MapElem::CStrKey(std::move(t))};
}

template<typename... Args>
SArray static_vec(Args&&... args) {
  auto ar = make_vec_array(std::forward<Args>(args)...);
  return ArrayData::GetScalarArray(std::move(ar));
}

template<typename... Args>
SArray static_varray(Args&&... args) {
  auto ar = make_varray(std::forward<Args>(args)...);
  return ArrayData::GetScalarArray(std::move(ar));
}

template<typename... Args>
SArray static_dict(Args&&... args) {
  auto ar = make_dict_array(std::forward<Args>(args)...);
  return ArrayData::GetScalarArray(std::move(ar));
}

template<typename... Args>
SArray static_darray(Args&&... args) {
  auto ar = make_darray(std::forward<Args>(args)...);
  return ArrayData::GetScalarArray(std::move(ar));
}

template<typename... Args>
SArray static_keyset(Args&&... args) {
  auto ar = make_keyset_array(std::forward<Args>(args)...);
  return ArrayData::GetScalarArray(std::move(ar));
}

//////////////////////////////////////////////////////////////////////

auto const predefined = folly::lazy([]{
  std::vector<std::pair<trep, Type>> types{
#define X(y, ...) { B##y, T##y },
    HHBBC_TYPE_PREDEFINED(X)
#undef X
  };
  types.emplace_back(BInt|BObj, Type{BInt|BObj});
  types.emplace_back(BKeysetN|BVecishN, Type{BKeysetN|BVecishN});
  types.emplace_back(BKeysetN|BDictishN, Type{BKeysetN|BDictishN});
  types.emplace_back(BKeysetE|BVecishE, Type{BKeysetE|BVecishE});
  types.emplace_back(BKeysetE|BDictishE, Type{BKeysetE|BDictishE});
  return types;
});

auto const optionals = folly::lazy([]{
  std::vector<Type> opts;
  for (auto const& p : predefined()) {
    if (p.first == BTop) continue;
    if (!couldBe(p.first, BInitNull) || subtypeOf(p.first, BInitNull)) continue;
    opts.emplace_back(p.second);
  }
  return opts;
});

// In the sense of "non-union type", not the sense of TPrim.
auto const primitives = folly::lazy([]{
  return std::vector<Type>{
#define X(y) T##y,
    HHBBC_TYPE_SINGLE(X)
#undef X
  };
});

std::vector<Type> withData(const Index& index) {
  std::vector<Type> types;

  auto const clsA = index.resolve_class(Context{}, s_A.get());
  if (!clsA || !clsA->resolved()) ADD_FAILURE();
  auto const clsAA = index.resolve_class(Context{}, s_AA.get());
  if (!clsAA || !clsAA->resolved()) ADD_FAILURE();
  auto const clsAB = index.resolve_class(Context{}, s_AB.get());
  if (!clsAB || !clsAB->resolved()) ADD_FAILURE();

  auto const clsIBase = index.resolve_class(Context{}, s_IBase.get());
  if (!clsIBase || !clsIBase->resolved()) ADD_FAILURE();
  auto const clsIA = index.resolve_class(Context{}, s_IA.get());
  if (!clsIA || !clsIA->resolved()) ADD_FAILURE();
  auto const clsIAA = index.resolve_class(Context{}, s_IAA.get());
  if (!clsIAA || !clsIAA->resolved()) ADD_FAILURE();
  auto const clsIB = index.resolve_class(Context{}, s_IB.get());
  if (!clsIB || !clsIB->resolved()) ADD_FAILURE();

  auto const recA = index.resolve_record(s_UniqueRecBase.get());
  if (!recA || !recA->resolved()) ADD_FAILURE();
  auto const recB = index.resolve_record(s_UniqueRec.get());
  if (!recB || !recB->resolved()) ADD_FAILURE();
  auto const recC = index.resolve_record(s_UniqueRecA.get());
  if (!recC || !recC->resolved()) ADD_FAILURE();

  auto const svec1 = static_vec(s_A.get(), s_B.get());
  auto const svec2 = static_vec(123, 456);
  auto const svarr1 = static_varray(s_A.get(), s_B.get());
  auto const svarr2 = static_varray(123, 456);
  auto const sdict1 = static_dict(s_A.get(), s_B.get(), s_C.get(), 123);
  auto const sdict2 = static_dict(100, s_A.get(), 200, s_C.get());
  auto const sdarr1 = static_darray(s_A.get(), s_B.get(), s_C.get(), 123);
  auto const sdarr2 = static_darray(100, s_A.get(), 200, s_C.get());
  auto const skeyset1 = static_keyset(s_A.get(), s_B.get());
  auto const skeyset2 = static_keyset(123, 456);

  auto const support = BStr | BDbl | BInt | BCls | BObj | BRecord | BArrLikeN;
  auto const nonSupport = BCell & ~support;

  auto const add = [&] (trep b) {
    types.emplace_back(Type{b});

    if (b == BTop) return;

    if (couldBe(b, BStr) && subtypeOf(b, BStr | nonSupport)) {
      types.emplace_back(make_specialized_string(b, s_A.get()));
      types.emplace_back(make_specialized_string(b, s_B.get()));
    }
    if (couldBe(b, BInt) && subtypeOf(b, BInt | nonSupport)) {
      types.emplace_back(make_specialized_int(b, 123));
      types.emplace_back(make_specialized_int(b, 456));
    }
    if (couldBe(b, BDbl) && subtypeOf(b, BDbl | nonSupport)) {
      types.emplace_back(make_specialized_double(b, 3.141));
      types.emplace_back(make_specialized_double(b, 2.718));
    }
    if (couldBe(b, BObj) && subtypeOf(b, BObj | nonSupport)) {
      types.emplace_back(make_specialized_wait_handle(b, TInt, index));
      types.emplace_back(make_specialized_wait_handle(b, TStr, index));
      types.emplace_back(make_specialized_wait_handle(b, TArrKey, index));

      types.emplace_back(make_specialized_exact_object(b, *clsA));
      types.emplace_back(make_specialized_exact_object(b, *clsAA));
      types.emplace_back(make_specialized_exact_object(b, *clsAB));

      types.emplace_back(make_specialized_exact_object(b, *clsIBase));
      types.emplace_back(make_specialized_exact_object(b, *clsIA));
      types.emplace_back(make_specialized_exact_object(b, *clsIAA));
      types.emplace_back(make_specialized_exact_object(b, *clsIB));

      types.emplace_back(make_specialized_sub_object(b, *clsA));
      types.emplace_back(make_specialized_sub_object(b, *clsAA));
      types.emplace_back(make_specialized_sub_object(b, *clsAB));

      auto const subIBase = make_specialized_sub_object(b, *clsIBase);
      auto const subIA = make_specialized_sub_object(b, *clsIA);
      auto const subIAA = make_specialized_sub_object(b, *clsIAA);
      auto const subIB = make_specialized_sub_object(b, *clsIB);
      types.emplace_back(subIBase);
      types.emplace_back(subIA);
      types.emplace_back(subIAA);
      types.emplace_back(subIB);

      if (subtypeOf(b, BInitCell)) {
        types.emplace_back(make_specialized_wait_handle(b, subIBase, index));
        types.emplace_back(make_specialized_wait_handle(b, subIA, index));
        types.emplace_back(make_specialized_wait_handle(b, subIAA, index));
        types.emplace_back(make_specialized_wait_handle(b, subIB, index));
      }

      types.emplace_back(make_specialized_exact_object(b, *clsA, true));
      types.emplace_back(make_specialized_exact_object(b, *clsAA, true));
      types.emplace_back(make_specialized_exact_object(b, *clsAB, true));

      types.emplace_back(make_specialized_sub_object(b, *clsA, true));
      types.emplace_back(make_specialized_sub_object(b, *clsAA, true));
      types.emplace_back(make_specialized_sub_object(b, *clsAB, true));

      auto const dobj =
        dobj_of(make_specialized_wait_handle(b, TArrKey, index));
      if (!dobj.cls.resolved()) ADD_FAILURE();
      types.emplace_back(make_specialized_sub_object(b, dobj.cls));
      types.emplace_back(make_specialized_exact_object(b, dobj.cls));
    }
    if (couldBe(b, BCls) && subtypeOf(b, BCls | nonSupport)) {
      types.emplace_back(make_specialized_exact_class(b, *clsA));
      types.emplace_back(make_specialized_exact_class(b, *clsAA));
      types.emplace_back(make_specialized_exact_class(b, *clsAB));

      types.emplace_back(make_specialized_sub_class(b, *clsA));
      types.emplace_back(make_specialized_sub_class(b, *clsAA));
      types.emplace_back(make_specialized_sub_class(b, *clsAB));

      types.emplace_back(make_specialized_exact_class(b, *clsA, true));
      types.emplace_back(make_specialized_exact_class(b, *clsAA, true));
      types.emplace_back(make_specialized_exact_class(b, *clsAB, true));

      types.emplace_back(make_specialized_sub_class(b, *clsA, true));
      types.emplace_back(make_specialized_sub_class(b, *clsAA, true));
      types.emplace_back(make_specialized_sub_class(b, *clsAB, true));
    }
    if (couldBe(b, BRecord) && subtypeOf(b, BRecord | nonSupport)) {
      types.emplace_back(make_specialized_exact_record(b, *recA));
      types.emplace_back(make_specialized_exact_record(b, *recB));
      types.emplace_back(make_specialized_exact_record(b, *recC));

      types.emplace_back(make_specialized_sub_record(b, *recA));
      types.emplace_back(make_specialized_sub_record(b, *recB));
      types.emplace_back(make_specialized_sub_record(b, *recC));
    }

    if (couldBe(b, BArrLikeN) && subtypeOf(b, BArrLikeN | nonSupport)) {
      if (subtypeAmong(b, BKeysetN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrpacked(b, {ival(0), ival(1)}));
        types.emplace_back(make_specialized_arrpackedn(b, TInt));
        types.emplace_back(make_specialized_arrmapn(b, TInt, TInt));
        types.emplace_back(make_specialized_arrmap(b, {map_elem(1, ival(1))}));
        types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, sval(s_A))}));

        if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
          types.emplace_back(make_specialized_arrmapn(b, TSStr, TSStr));
        } else {
          types.emplace_back(make_specialized_arrmapn(b, TStr, TStr));
        }
      } else if (couldBe(b, BKeysetN)) {
        types.emplace_back(make_specialized_arrpacked(b, {TInt, TInt}));
        types.emplace_back(make_specialized_arrpacked(b, {TInitPrim, TInitPrim}));
        types.emplace_back(make_specialized_arrpackedn(b, TInt));
        types.emplace_back(make_specialized_arrpackedn(b, TInitPrim));

        if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
          types.emplace_back(make_specialized_arrpacked(b, {TInitUnc, TInitUnc}));
          types.emplace_back(make_specialized_arrpackedn(b, TUncArrKey));
          types.emplace_back(make_specialized_arrmapn(b, TInt, TUncArrKey));
          types.emplace_back(make_specialized_arrmapn(b, TUncArrKey, TSStr));
          types.emplace_back(make_specialized_arrmapn(b, TUncArrKey, TUncArrKey));
        } else {
          types.emplace_back(make_specialized_arrpacked(b, {TInitCell, TInitCell}));
          types.emplace_back(make_specialized_arrpackedn(b, TArrKey));
          types.emplace_back(make_specialized_arrpackedn(b, union_of(TObj, TInt)));
          types.emplace_back(make_specialized_arrmapn(b, TInt, TArrKey));
          types.emplace_back(make_specialized_arrmapn(b, TArrKey, TStr));
          types.emplace_back(make_specialized_arrmapn(b, TArrKey, TArrKey));
        }

        if (!couldBe(b, BVecishN)) {
          types.emplace_back(make_specialized_arrmap(b, {map_elem(1, TInt)}));
          if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TSStr)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TInt, TSStr));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TSStr, TInt));
          } else {
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TStr)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitCell)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TInt, TStr));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TStr, TInt));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TStr, TObj));
          }
        }
      } else {
        types.emplace_back(make_specialized_arrpacked(b, {TInt, TInt}));
        types.emplace_back(make_specialized_arrpackedn(b, TInt));

        if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
          types.emplace_back(make_specialized_arrpacked(b, {TSStr, TSStr}));
          types.emplace_back(make_specialized_arrpacked(b, {TInitUnc, TInitUnc}));
          types.emplace_back(make_specialized_arrpackedn(b, TSStr));
          types.emplace_back(make_specialized_arrpackedn(b, TUncArrKey));
        } else {
          types.emplace_back(make_specialized_arrpacked(b, {TStr, TStr}));
          types.emplace_back(make_specialized_arrpacked(b, {TInitCell, TInitCell}));
          types.emplace_back(make_specialized_arrpackedn(b, TStr));
          types.emplace_back(make_specialized_arrpackedn(b, TArrKey));
          types.emplace_back(make_specialized_arrpackedn(b, TObj));
        }

        if (!subtypeAmong(b, BVecishN, BArrLikeN)) {
          types.emplace_back(make_specialized_arrmapn(b, TInt, TInt));
          if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
            if (!couldBe(b, BVecishN)) {
              types.emplace_back(make_specialized_arrmapn(b, TSStr, TSStr));
            }
            types.emplace_back(make_specialized_arrmapn(b, TUncArrKey, TUncArrKey));
          } else {
            if (!couldBe(b, BVecishN)) {
              types.emplace_back(make_specialized_arrmapn(b, TStr, TStr));
            }
            types.emplace_back(make_specialized_arrmapn(b, TArrKey, TArrKey));
            types.emplace_back(make_specialized_arrmapn(b, TArrKey, TObj));
          }
        }

        if (!couldBe(b, BVecishN)) {
          if (subtypeAmong(b, BSArrLikeN, BArrLikeN)) {
            types.emplace_back(make_specialized_arrmap(b, {map_elem(1, TSStr)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TInt, TSStr));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TSStr, TInt));
          } else {
            types.emplace_back(make_specialized_arrmap(b, {map_elem(1, TStr)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitCell)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TObj)}));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TInt, TStr));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TStr, TInt));
            types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInitUnc)}, TStr, TObj));
          }
          types.emplace_back(make_specialized_arrmap(b, {map_elem(s_A, TInt)}));
        }
      }

      if (subtypeAmong(b, BSVecN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrval(b, svec1));
        types.emplace_back(make_specialized_arrval(b, svec2));
      }
      if (subtypeAmong(b, BSVArrN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrval(b, svarr1));
        types.emplace_back(make_specialized_arrval(b, svarr2));
      }
      if (subtypeAmong(b, BSDictN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrval(b, sdict1));
        types.emplace_back(make_specialized_arrval(b, sdict2));
      }
      if (subtypeAmong(b, BSDArrN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrval(b, sdarr1));
        types.emplace_back(make_specialized_arrval(b, sdarr2));
      }
      if (subtypeAmong(b, BSKeysetN, BArrLikeN)) {
        types.emplace_back(make_specialized_arrval(b, skeyset1));
        types.emplace_back(make_specialized_arrval(b, skeyset2));
      }
    }
  };

  for (auto const& t : predefined()) add(t.first);

  make_unmarked(types);
  return types;
}

auto const specialized_arrays = folly::lazy([]{
  std::vector<Type> types;

  auto const add = [&] (trep b) {
    if (!b) return;

    types.emplace_back(Type{b});

    auto const containsUncounted = [] (trep bits) {
      if ((bits & BVecN)    == BSVecN)    return true;
      if ((bits & BVArrN)   == BSVArrN)   return true;
      if ((bits & BDictN)   == BSDictN)   return true;
      if ((bits & BDArrN)   == BSDArrN)   return true;
      if ((bits & BKeysetN) == BSKeysetN) return true;
      return false;
    };

    auto const keyBits = [&containsUncounted] (trep bits) {
      auto upper = BBottom;
      auto lower = BArrKey;

      if (couldBe(bits, BVecish)) {
        upper |= BInt;
        lower &= BInt;
        bits &= ~BVecish;
      }
      if (couldBe(bits, BArrLikeN)) {
        if (subtypeOf(bits, BSArrLikeN)) {
          upper |= BUncArrKey;
          lower &= BUncArrKey;
        } else {
          upper |= BArrKey;
          lower &= containsUncounted(bits) ? BUncArrKey : BArrKey;
        }
      }
      return std::make_pair(upper, lower);
    }(b);

    auto const calcValBits = [&containsUncounted] (trep bits, bool packed) {
      auto upper = BBottom;
      auto lower = BInitCell;

      if (couldBe(bits, BKeysetN)) {
        if (packed) {
          upper |= BInt;
          lower &= BInt;
        } else if (subtypeAmong(bits, BSKeysetN, BKeysetN)) {
          upper |= BUncArrKey;
          lower |= BUncArrKey;
        } else {
          upper |= BArrKey;
          lower &= BArrKey;
        }
        bits &= ~BKeysetN;
      }
      if (couldBe(bits, BArrLikeN)) {
        if (subtypeOf(bits, BSArrLikeN)) {
          upper |= BInitUnc;
          lower &= BInitUnc;
        } else {
          upper |= BInitCell;
          lower &= containsUncounted(bits) ? BInitUnc : BInitCell;
        }
      }
      return std::make_pair(upper, lower);
    };
    auto const packedValBits = calcValBits(b, true);
    auto const valBits = calcValBits(b, false);

    auto const packedn = [&] (const Type& t) {
      if (!t.subtypeOf(packedValBits.first)) return;
      if (!t.couldBe(packedValBits.second)) return;
      if (subtypeOf(b, BVecishN) && !t.strictSubtypeOf(packedValBits.first)) {
        return;
      }
      types.emplace_back(make_specialized_arrpackedn(b, t));
    };
    packedn(TInt);
    packedn(TSStr);
    packedn(TStr);
    packedn(TUncArrKey);
    packedn(TArrKey);
    packedn(TObj);
    packedn(TInitUnc);
    packedn(TInitCell);
    packedn(Type{BInt|BObj});
    packedn(Type{BInitCell & ~BObj});
    packedn(Type{BInitUnc & ~BSStr});

    auto const packed = [&] (const std::vector<Type>& elems) {
      for (size_t i = 0; i < elems.size(); ++i) {
        auto const& t = elems[i];
        if (!t.subtypeOf(packedValBits.first)) return;
        if (!t.couldBe(packedValBits.second)) return;
        if (couldBe(b, BKeysetN) && !t.couldBe(ival(i))) return;
        if (subtypeOf(b, BKeysetN) && !t.subtypeOf(ival(i))) return;
      }
      types.emplace_back(make_specialized_arrpacked(b, elems));
    };
    packed({TInt, TInt});
    packed({TSStr, TSStr});
    packed({TStr, TStr});
    packed({TUncArrKey, TUncArrKey});
    packed({TArrKey, TArrKey});
    packed({TObj, TObj});
    packed({TInitUnc, TInitUnc});
    packed({TInitCell, TInitCell});
    packed({Type{BInt|BObj}, Type{BInt|BObj}});
    packed({TInt, TObj});
    packed({TSStr, TStr});
    packed({ival(0)});
    packed({ival(0), ival(1)});
    packed({ival(100), ival(200)});
    packed({union_of(TObj,ival(0)), union_of(TObj,ival(1))});
    packed({TInt, TInt, TInt});
    packed({TObj, TObj, TObj});
    packed({TArrKey, TArrKey, TArrKey});

    auto const mapn = [&] (const Type& key, const Type& val) {
      if (subtypeOf(b, BVecishN)) return;
      if (!key.subtypeOf(keyBits.first)) return;
      if (!key.couldBe(keyBits.second)) return;
      if (!val.subtypeOf(valBits.first)) return;
      if (!val.couldBe(valBits.second)) return;
      if (!key.strictSubtypeOf(keyBits.first) &&
          !val.strictSubtypeOf(valBits.first)) return;
      if (couldBe(b, BKeysetN) && !key.couldBe(val)) return;
      if (subtypeOf(b, BKeysetN) && key != val) return;
      types.emplace_back(make_specialized_arrmapn(b, key, val));
    };
    mapn(TInt, TInt);
    mapn(TSStr, TSStr);
    mapn(TStr, TStr);
    mapn(TUncArrKey, TUncArrKey);
    mapn(TArrKey, TArrKey);
    mapn(TUncArrKey, TInt);
    mapn(TUncArrKey, TSStr);
    mapn(TInt, TUncArrKey);
    mapn(TSStr, TUncArrKey);
    mapn(TInt, TSStr);
    mapn(TSStr, TInt);
    mapn(TUncArrKey, TObj);
    mapn(TArrKey, TObj);
    mapn(TUncArrKey, TInitUnc);
    mapn(TArrKey, TInitUnc);
    mapn(TUncArrKey, TInitCell);
    mapn(TArrKey, TInitCell);
    mapn(TInt, Type{BInt|BObj});
    mapn(TSStr, Type{BInt|BObj});
    mapn(TInt, TInitUnc);
    mapn(TSStr, TInitUnc);
    mapn(TArrKey, Type{BInitCell & ~BObj});
    mapn(TUncArrKey, Type{BInitUnc & ~BSStr});
    mapn(TInt, TInitCell);
    mapn(TStr, TInitCell);

    auto const map = [&] (const MapElems& elems,
                          const Type& optKey = TBottom,
                          const Type& optVal = TBottom) {
      if (couldBe(b, BVecishN)) return;

      for (auto const& e : elems) {
        if (!e.second.val.subtypeOf(valBits.first)) return;
        if (!e.second.val.couldBe(valBits.second)) return;

        auto const key = [&] {
          if (isIntType(e.first.m_type)) return ival(e.first.m_data.num);
          switch (e.second.keyStaticness) {
            case TriBool::Yes:   return HPHP::HHBBC::sval(e.first.m_data.pstr);
            case TriBool::Maybe: return HPHP::HHBBC::sval_nonstatic(e.first.m_data.pstr);
            case TriBool::No:    return HPHP::HHBBC::sval_counted(e.first.m_data.pstr);
          }
          always_assert(false);
        }();

        if (!key.subtypeOf(keyBits.first)) return;
        if (!key.couldBe(keyBits.second)) return;

        if (couldBe(b, BKeysetN) && !key.couldBe(e.second.val)) return;
        if (subtypeOf(b, BKeysetN) && key != e.second.val) return;
      }
      if (!optKey.is(BBottom)) {
        if (!optKey.subtypeOf(keyBits.first)) return;
        if (!optVal.subtypeOf(valBits.first)) return;
        if (subtypeOf(b, BKeysetN) && optKey != optVal) return;
      }
      types.emplace_back(make_specialized_arrmap(b, elems, optKey, optVal));
    };
    map({map_elem(s_A, TInt)});
    map({map_elem(s_A, TSStr)});
    map({map_elem(s_A, TStr)});
    map({map_elem(s_A, sval(s_A))});
    map({map_elem(s_A, sval_nonstatic(s_A))});
    map({map_elem(s_A, TUncArrKey)});
    map({map_elem(s_A, TArrKey)});
    map({map_elem(s_A, TObj)});
    map({map_elem(s_A, TInitUnc)});
    map({map_elem(s_A, TInitCell)});
    map({map_elem(s_A, Type{BInt|BObj})});
    map({map_elem(s_A, TInt)}, TInt, TInt);
    map({map_elem(s_A, TInt)}, TInt, TSStr);
    map({map_elem(s_A, TInt)}, TInt, TStr);
    map({map_elem(s_A, TInt)}, TInt, TUncArrKey);
    map({map_elem(s_A, TInt)}, TInt, TArrKey);
    map({map_elem(s_A, TInt)}, TInt, TObj);
    map({map_elem(s_A, TInt)}, TInt, TInitUnc);
    map({map_elem(s_A, TInt)}, TInt, TInitCell);
    map({map_elem(s_A, TInt)}, TSStr, TInt);
    map({map_elem(s_A, TInt)}, TSStr, TSStr);
    map({map_elem(s_A, TInt)}, TSStr, TStr);
    map({map_elem(s_A, TInt)}, TSStr, TUncArrKey);
    map({map_elem(s_A, TInt)}, TSStr, TArrKey);
    map({map_elem(s_A, TInt)}, TSStr, TObj);
    map({map_elem(s_A, TInt)}, TSStr, TInitUnc);
    map({map_elem(s_A, TInt)}, TSStr, TInitCell);
    map({map_elem(s_A, sval(s_A))}, TInt, Type{BInt|BObj});
    map({map_elem(s_B, TInt)});
    map({map_elem(s_B, sval(s_B))});
    map({map_elem(123, TInt)});
    map({map_elem(123, TSStr)});
    map({map_elem(123, TStr)});
    map({map_elem(123, ival(123))});
    map({map_elem(123, TUncArrKey)});
    map({map_elem(123, TArrKey)});
    map({map_elem(123, TObj)});
    map({map_elem(123, TInitUnc)});
    map({map_elem(123, TInitCell)});
    map({map_elem(123, Type{BInt|BObj})});
    map({map_elem(std::numeric_limits<int64_t>::max(), TStr)});
    map({map_elem_nonstatic(s_A, TInt)});
    map({map_elem_nonstatic(s_A, TSStr)});
    map({map_elem_nonstatic(s_A, TStr)});
    map({map_elem_nonstatic(s_A, sval(s_A))});
    map({map_elem_nonstatic(s_A, sval_nonstatic(s_A))});
    map({map_elem_nonstatic(s_A, Type{BStr|BObj})});
  };

  auto const bits = std::vector<trep>{
    BCVecishN,
    BSVecishN,
    BCDictishN,
    BSDictishN,
    BCKeysetN,
    BSKeysetN,
  };

  auto const subsetSize = 1ULL << bits.size();
  for (size_t i = 0; i < subsetSize; ++i) {
    auto b = BBottom;
    for (size_t j = 0; j < bits.size(); ++j) {
      if (i & (1ULL << j)) b |= bits[j];
    }
    add(b);
  }

  auto const svec1 = static_vec(s_A.get(), s_B.get());
  auto const svec2 = static_vec(123, 456);
  auto const svarr1 = static_varray(s_A.get(), s_B.get());
  auto const svarr2 = static_varray(123, 456);
  auto const sdict1 = static_dict(s_A.get(), s_B.get(), s_C.get(), 123);
  auto const sdict2 = static_dict(100, s_A.get(), 200, s_C.get());
  auto const sdarr1 = static_darray(s_A.get(), s_B.get(), s_C.get(), 123);
  auto const sdarr2 = static_darray(100, s_A.get(), 200, s_C.get());
  auto const skeyset1 = static_keyset(s_A.get(), s_B.get());
  auto const skeyset2 = static_keyset(123, 456);

  types.emplace_back(make_specialized_arrval(BSVecN, svec1));
  types.emplace_back(make_specialized_arrval(BSVecN, svec2));
  types.emplace_back(make_specialized_arrval(BSVArrN, svarr1));
  types.emplace_back(make_specialized_arrval(BSVArrN, svarr2));
  types.emplace_back(make_specialized_arrval(BSDictN, sdict1));
  types.emplace_back(make_specialized_arrval(BSDictN, sdict2));
  types.emplace_back(make_specialized_arrval(BSDArrN, sdarr1));
  types.emplace_back(make_specialized_arrval(BSDArrN, sdarr2));
  types.emplace_back(make_specialized_arrval(BSKeysetN, skeyset1));
  types.emplace_back(make_specialized_arrval(BSKeysetN, skeyset2));

  types.emplace_back(make_specialized_arrpackedn(BVecishN|BKeysetN, ival(0)));

  make_unmarked(types);
  return types;
});

std::vector<Type> allCases(const Index& index) {
  auto types = withData(index);
  auto const specialized = specialized_arrays();
  types.insert(types.end(), specialized.begin(), specialized.end());
  return types;
}

auto const allBits = folly::lazy([]{
  std::vector<trep> bits;

  for (auto const& p : predefined()) {
    bits.emplace_back(p.first);
    if (!(p.first & BInitNull)) bits.emplace_back(p.first | BInitNull);
  }

  auto const arrbits = std::vector<trep>{
    BCVecN,
    BSVecN,
    BCDictN,
    BSDictN,
    BCVArrN,
    BSVArrN,
    BCDArrN,
    BSDArrN,
    BCKeysetN,
    BSKeysetN,
  };

  auto const subsetSize = 1ULL << arrbits.size();
  for (size_t i = 0; i < subsetSize; ++i) {
    auto b = BBottom;
    for (size_t j = 0; j < arrbits.size(); ++j) {
      if (i & (1ULL << j)) b |= arrbits[j];
    }
    bits.emplace_back(b);
    bits.emplace_back(b | BInitNull);
  }

  bits.emplace_back(BObj | BInt);
  bits.emplace_back(BObj | BInt | BInitNull);

  return bits;
});

//////////////////////////////////////////////////////////////////////

}

TEST_F(TypeTest, Bits) {
  auto const program = make_test_program();
  Index index { program.get() };

  for (auto const& t : predefined()) {
    EXPECT_EQ(Type{t.first}, t.second);
    EXPECT_TRUE(t.second.is(t.first));
    EXPECT_TRUE(t.second.subtypeOf(t.first));
    EXPECT_FALSE(t.second.strictSubtypeOf(t.first));
    if (t.first != BBottom) {
      EXPECT_TRUE(t.second.couldBe(t.first));
    }
  }

  for (auto const& t : allCases(index)) {
    for (auto const b : allBits()) {
      EXPECT_EQ(t.subtypeOf(b), t.subtypeOf(Type{b}));
      EXPECT_EQ(loosen_mark_for_testing(t).strictSubtypeOf(b),
                loosen_mark_for_testing(t).strictSubtypeOf(Type{b}));
      if (!is_specialized_array_like(t)) {
        EXPECT_EQ(t.couldBe(b), t.couldBe(Type{b}));
      } else if (!t.couldBe(b)) {
        EXPECT_FALSE(t.couldBe(Type{b}));
      }
    }
  }
}

TEST_F(TypeTest, Top) {
  auto const program = make_test_program();
  Index index { program.get() };

  // Everything is a subtype of Top, couldBe Top, and the union of Top
  // with anything is Top.
  for (auto const& t : allCases(index)) {
    if (!t.is(BTop)) {
      EXPECT_FALSE(TTop.subtypeOf(t));
    }
    EXPECT_FALSE(TTop.strictSubtypeOf(t));
    EXPECT_TRUE(t.subtypeOf(BTop));
    if (!t.is(BBottom)) {
      EXPECT_TRUE(TTop.couldBe(t));
      EXPECT_TRUE(t.couldBe(BTop));
    }
    EXPECT_EQ(union_of(t, TTop), TTop);
    EXPECT_EQ(union_of(TTop, t), TTop);
    EXPECT_EQ(intersection_of(TTop, t), t);
    EXPECT_EQ(intersection_of(t, TTop), t);
  }
}

TEST_F(TypeTest, Bottom) {
  auto const program = make_test_program();
  Index index { program.get() };

  // Bottom is a subtype of everything, nothing couldBe Bottom, and
  // the union_of anything with Bottom is itself.
  for (auto const& t : allCases(index)) {
    EXPECT_TRUE(TBottom.subtypeOf(t));
    EXPECT_FALSE(TBottom.couldBe(t));
    if (!t.is(BBottom)) {
      EXPECT_FALSE(t.subtypeOf(BBottom));
    }
    EXPECT_FALSE(t.strictSubtypeOf(BBottom));
    EXPECT_FALSE(t.couldBe(BBottom));
    EXPECT_EQ(union_of(t, TBottom), t);
    EXPECT_EQ(union_of(TBottom, t), t);
    EXPECT_EQ(intersection_of(TBottom, t), TBottom);
    EXPECT_EQ(intersection_of(t, TBottom), TBottom);
  }
}

TEST_F(TypeTest, Prims) {
  auto const program = make_test_program();
  Index index { program.get() };

  // All pairs of non-equivalent primitives are not related by either
  // subtypeOf or couldBe, including if you wrap them in wait handles.
  for (auto const& t1 : primitives()) {
    for (auto const& t2 : primitives()) {
      if (t1 == t2) continue;

      auto const test = [] (const Type& a, const Type& b) {
        EXPECT_TRUE(!a.subtypeOf(b));
        EXPECT_TRUE(!a.strictSubtypeOf(b));
        EXPECT_TRUE(!b.subtypeOf(a));
        EXPECT_TRUE(!b.strictSubtypeOf(a));
        EXPECT_TRUE(!a.couldBe(b));
        EXPECT_TRUE(!b.couldBe(a));
        EXPECT_EQ(intersection_of(a, b), TBottom);
      };

      test(t1, t2);

      if (t1.is(BUninit) || t2.is(BUninit)) continue;

      test(wait_handle(index, t1), wait_handle(index, t2));

      const std::vector<Type> arrays1{
        dict_packed({t1, t1}),
        dict_packedn(t1),
        dict_map({map_elem(5, t1)}),
        dict_n(TInt, t1)
      };
      const std::vector<Type> arrays2{
        dict_packed({t2, t2}),
        dict_packedn(t2),
        dict_map({map_elem(5, t2)}),
        dict_n(TInt, t2)
      };
      for (auto const& a1 : arrays1) {
        for (auto const& a2 : arrays2) test(a1, a2);
      }
    }
  }
}

namespace {

void test_basic_operators(const std::vector<Type>& types) {
  for (auto const& t : types) {
    EXPECT_EQ(t, t);
    EXPECT_TRUE(t.equivalentlyRefined(t));
    EXPECT_TRUE(t.subtypeOf(t));
    EXPECT_TRUE(t.moreRefined(t));
    EXPECT_FALSE(t.strictSubtypeOf(t));
    EXPECT_FALSE(t.strictlyMoreRefined(t));
    if (!t.is(BBottom)) {
      EXPECT_TRUE(t.couldBe(t));
    }
    EXPECT_EQ(union_of(t, t), t);
    EXPECT_EQ(intersection_of(t, t), t);
    if (!t.is(BBottom)) {
      EXPECT_TRUE(opt(t).couldBe(t));
    }
    EXPECT_TRUE(t.subtypeOf(opt(t)));
    EXPECT_EQ(opt(t), union_of(t, TInitNull));
    if (t.subtypeOf(BCell)) {
      EXPECT_EQ(unopt(t), remove_bits(t, BInitNull));
      EXPECT_FALSE(unopt(t).couldBe(BInitNull));
    }
    if (!t.couldBe(BInitNull)) {
      EXPECT_EQ(t, unopt(opt(t)));
    }
  }

  auto const isCtxful = [] (const Type& t1, const Type& t2) {
    if (is_specialized_obj(t1) && is_specialized_obj(t2)) {
      if (is_specialized_wait_handle(t1) || is_specialized_wait_handle(t2)) {
        return false;
      }
      return dobj_of(t1).isCtx != dobj_of(t2).isCtx;
    }
    if (!is_specialized_cls(t1) || !is_specialized_cls(t2)) return false;
    return dcls_of(t1).isCtx != dcls_of(t2).isCtx;
  };

  auto const matchingData = [] (const Type& t1, const Type& t2, auto const& self) {
    if (!t1.hasData()) return true;
    if (is_specialized_array_like(t1)) {
      return is_specialized_array_like(t2) || !t2.hasData();
    }
    if (is_specialized_string(t1)) {
      return is_specialized_string(t2) || !t2.hasData();
    }
    if (is_specialized_int(t1)) {
      return is_specialized_int(t2) || !t2.hasData();
    }
    if (is_specialized_double(t1)) {
      return is_specialized_double(t2) || !t2.hasData();
    }
    if (is_specialized_record(t1)) {
      return is_specialized_record(t2) || !t2.hasData();
    }
    if (is_specialized_cls(t1)) {
      return is_specialized_cls(t2) || !t2.hasData();
    }
    if (is_specialized_wait_handle(t1) && is_specialized_wait_handle(t2)) {
      return self(wait_handle_inner(t1), wait_handle_inner(t2), self);
    }
    if (is_specialized_obj(t1)) {
      if (dobj_of(t1).cls.couldBeInterface()) return false;
      if (!t2.hasData()) return true;
      if (!is_specialized_obj(t2)) return false;
      return !dobj_of(t2).cls.couldBeInterface();
    }
    return true;
  };

  auto const isNotInterface = [] (Type t1, Type t2) {
    if (is_specialized_wait_handle(t1) && is_specialized_wait_handle(t2)) {
      t1 = wait_handle_inner(t1);
      t2 = wait_handle_inner(t2);
    }
    return
      !is_specialized_obj(t1) ||
      !is_specialized_obj(t2) ||
      !dobj_of(t1).cls.couldBeInterface() ||
      !dobj_of(t2).cls.couldBeInterface();
  };

  for (size_t i1 = 0; i1 < types.size(); ++i1) {
    for (size_t i2 = 0; i2 < types.size(); ++i2) {
      auto const& t1 = types[i1];
      auto const& t2 = types[i2];

      auto const ctxful = isCtxful(t1, t2);

      auto const equivRefined = t1.equivalentlyRefined(t2);
      auto const moreRefined = t1.moreRefined(t2);
      auto const couldBe = t1.couldBe(t2);

      if (t1.strictlyMoreRefined(t2)) {
        EXPECT_FALSE(equivRefined);
        EXPECT_TRUE(moreRefined);
      }
      if (t1.strictSubtypeOf(t2)) {
        EXPECT_NE(t1, t2);
        EXPECT_TRUE(t1.subtypeOf(t2));
      }

      if (equivRefined) {
        EXPECT_TRUE(t1 == t2);
      }
      if (moreRefined) {
        EXPECT_TRUE(t1.subtypeOf(t2));
      }
      if (t1.strictlyMoreRefined(t2)) {
        EXPECT_TRUE(t1.strictSubtypeOf(t2) || t1 == t2);
      }

      if (!ctxful) {
        EXPECT_EQ(t1 == t2, equivRefined);
        EXPECT_EQ(t1.subtypeOf(t2), moreRefined);
        EXPECT_EQ(t1.strictSubtypeOf(t2), t1.strictlyMoreRefined(t2));
      }

      if (i1 == i2) {
        EXPECT_TRUE(equivRefined);
        EXPECT_FALSE(t1.strictlyMoreRefined(t2));
        EXPECT_FALSE(t1.strictSubtypeOf(t2));
      }

      auto const uni = union_of(t1, t2);
      auto const isect = intersection_of(t1, t2);

      EXPECT_EQ(t1 == t2, t2 == t1);
      EXPECT_EQ(equivRefined, t2.equivalentlyRefined(t1));
      EXPECT_EQ(couldBe, t2.couldBe(t1));
      EXPECT_EQ(uni, union_of(t2, t1));
      EXPECT_EQ(isect, intersection_of(t2, t1));

      EXPECT_TRUE(t1.moreRefined(uni));
      if (matchingData(t1, t2, matchingData)) {
        EXPECT_TRUE(isect.moreRefined(t1));
        EXPECT_TRUE(intersection_of(uni, t1).equivalentlyRefined(t1));
      } else {
        if (isNotInterface(t1, t2)) {
          EXPECT_TRUE(isect.moreRefined(t1) || isect.moreRefined(t2));
        }
        EXPECT_TRUE(intersection_of(uni, t1).equivalentlyRefined(t1) ||
                    intersection_of(uni, t2).equivalentlyRefined(t2));
      }

      if (moreRefined) {
        EXPECT_TRUE(uni.equivalentlyRefined(t2));
        EXPECT_TRUE(isect.equivalentlyRefined(t1));
      }

      if (couldBe) {
        if (!is_specialized_array_like(t1) && !is_specialized_array_like(t2)) {
          EXPECT_FALSE(isect.is(BBottom));
        }
      } else {
        EXPECT_TRUE(isect.is(BBottom));
      }

      if (!t1.is(BBottom)) {
        if (moreRefined) {
          EXPECT_TRUE(couldBe);
        }
        if (!couldBe) {
          EXPECT_FALSE(moreRefined);
        }
        EXPECT_TRUE(t1.couldBe(uni));
      }

      if (!isect.is(BBottom)) {
        EXPECT_TRUE(isect.couldBe(t1));
      }
    }
  }

  for (auto const& t : types) {
    if (t.couldBe(BVecish)) {
      EXPECT_FALSE(intersection_of(t, TVecish).is(BBottom));
    }
    if (t.couldBe(BDictish)) {
      EXPECT_FALSE(intersection_of(t, TDictish).is(BBottom));
    }
    if (t.couldBe(BKeyset)) {
      EXPECT_FALSE(intersection_of(t, TKeyset).is(BBottom));
    }
  }
}

}

TEST_F(TypeTest, BasicOperators) {
  auto const program = make_test_program();
  Index index { program.get() };
  test_basic_operators(withData(index));

  EXPECT_EQ(union_of(ival(0),TStr), TArrKey);
  EXPECT_EQ(union_of(TInt,sval(s_A)), TUncArrKey);
}

TEST_F(TypeTest, SpecializedArrays) {
  test_basic_operators(specialized_arrays());

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BVecishN|BKeysetN, TArrKey, TStr),
      make_specialized_arrmapn(BDictishN|BKeysetN, TInt, TArrKey)
    ),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BArrLikeN, TArrKey, TStr),
      make_specialized_arrmapn(BDictishN|BKeysetN, TInt, TArrKey)
    ),
    make_specialized_arrmapn(BDictishN, TInt, TStr)
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BVecishN|BDictishN, TArrKey, TInt),
      make_specialized_arrmapn(BVecishN|BKeysetN, TArrKey, TInt)
    ),
    make_specialized_arrpackedn(BVecishN, TInt)
  );

  {
    auto const map1 = make_specialized_arrmap(BDictishN, {map_elem(s_A, ival(123))});
    auto const map2 = make_specialized_arrmap(BDictishN, {map_elem_nonstatic(s_A, ival(123))});
    EXPECT_NE(map1, map2);
    EXPECT_TRUE(map1.couldBe(map2));
    EXPECT_TRUE(map2.couldBe(map1));
    EXPECT_TRUE(map1.subtypeOf(map2));
    EXPECT_FALSE(map2.subtypeOf(map1));
    EXPECT_EQ(union_of(map1, map2), map2);
    EXPECT_EQ(intersection_of(map1, map2), map1);
  }

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpacked(BVecN, {TStr, TArrKey, Type{BInt|BObj}, TInitCell}),
      TSArrLikeN
    ),
    make_specialized_arrpacked(BSVecN, {TSStr, TUncArrKey, TInt, TInitUnc})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpacked(BVecN, {TStr, TArrKey, TObj, TInitCell}),
      TSArrLikeN
    ),
    TBottom
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpacked(BVecN|BKeysetN, {TArrKey, TArrKey}),
      TKeysetN
    ),
    make_specialized_arrpacked(BKeysetN, {ival(0), ival(1)})
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpacked(BDictN|BVecN, {TObj}),
      Type{BSDictN|BVecN}
    ),
    make_specialized_arrpacked(BVecN, {TObj})
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpacked(BDictN|BVecN, {TInitCell}),
      make_specialized_arrpacked(BDArrN|BSVecN, {TInitCell})
    ),
    make_specialized_arrpacked(BSVecN, {TInitUnc})
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BVecN, TStr),
      TSArrLikeN
    ),
    make_specialized_arrpackedn(BSVecN, TSStr)
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BVecN, TObj),
      TSArrLikeN
    ),
    TBottom
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BVecN|BKeysetN, TArrKey),
      TKeysetN
    ),
    make_specialized_arrpackedn(BKeysetN, TInt)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BDictN|BVecN, TObj),
      Type{BSDictN|BVecN}
    ),
    make_specialized_arrpackedn(BVecN, TObj)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BDictN|BVecN, Type{BInitCell & ~BObj}),
      make_specialized_arrpackedn(BSDictN|BDArrN, Type{BInitCell & ~BObj})
    ),
    make_specialized_arrpackedn(BSDictN, TInitUnc)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BVecN, TArrKey, TObj),
      TVecN
    ),
    make_specialized_arrpackedn(BVecN, TObj)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN, TArrKey, TStr),
      TSDictN
    ),
    make_specialized_arrmapn(BSDictN, TUncArrKey, TSStr)
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN, TArrKey, TObj),
      TSDictN
    ),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN, TCStr, TStr),
      TSDictN
    ),
    TBottom
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BKeysetN, TStr, TArrKey),
      make_specialized_arrmapn(BVecN|BKeysetN, TArrKey, TArrKey)
    ),
    make_specialized_arrmapn(BKeysetN, TStr, TStr)
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BKeysetN, TStr, TArrKey),
      make_specialized_arrmapn(BVecN|BKeysetN, TArrKey, TInt)
    ),
    TBottom
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BKeysetN, TStr, TArrKey),
      make_specialized_arrmapn(BDictN|BKeysetN, TArrKey, TInt)
    ),
    make_specialized_arrmapn(BDictN, TStr, TInt)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BDArrN, TCStr, TInt),
      Type{BSDictN|BDArrN}
    ),
    make_specialized_arrmapn(BDArrN, TCStr, TInt)
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BDArrN, TStr, TObj),
      Type{BSDictN|BDArrN}
    ),
    make_specialized_arrmapn(BDArrN, TStr, TObj)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BDArrN, TArrKey, Type{BInitCell & ~BObj}),
      make_specialized_arrmapn(BSDictN|BVecN, TUncArrKey, Type{BInitCell & ~BObj})
    ),
    TSDictN
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmapn(BDictN|BVecN, TArrKey, Type{BInitCell & ~BObj}),
      make_specialized_arrmapn(BDArrN|BSVecN, TUncArrKey, Type{BInitCell & ~BObj})
    ),
    TSVecN
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN, {map_elem(s_A, TStr)}),
      TSDictN
    ),
    make_specialized_arrmap(BSDictN, {map_elem(s_A, TSStr)})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN, {map_elem_nonstatic(s_A, TStr)}),
      TSDictN
    ),
    make_specialized_arrmap(BSDictN, {map_elem(s_A, TSStr)})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN, {map_elem_counted(s_A, TStr)}),
      TSDictN
    ),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN, {map_elem(123, TStr)}),
      TSDictN
    ),
    make_specialized_arrmap(BSDictN, {map_elem(123, TSStr)})
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BKeysetN, {map_elem(s_A, TArrKey)}),
      make_specialized_arrmap(BDArrN|BKeysetN, {map_elem(s_A, TArrKey)})
    ),
    make_specialized_arrmap(BKeysetN, {map_elem(s_A, sval(s_A))})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BKeysetN, {map_elem_nonstatic(s_A, sval(s_A))}),
      make_specialized_arrmap(BDArrN|BKeysetN, {map_elem_nonstatic(s_A, sval(s_A))})
    ),
    make_specialized_arrmap(BKeysetN, {map_elem(s_A, sval(s_A))})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BKeysetN, {map_elem(s_A, TArrKey)}),
      make_specialized_arrmap(BDArrN|BKeysetN, {map_elem_nonstatic(s_A, TCStr)})
    ),
    TBottom
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BDArrN, {map_elem_counted(s_A, ival(123))}),
      Type{BSDictN|BDArrN}
    ),
    make_specialized_arrmap(BDArrN, {map_elem_counted(s_A, ival(123))})
  );
  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BKeysetN, {map_elem_nonstatic(s_A, TCStr)}),
      make_specialized_arrmap(BDictN|BSKeysetN, {map_elem(s_A, TArrKey)})
    ),
    make_specialized_arrmap(BDictN, {map_elem(s_A, TCStr)})
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrmap(BDictN|BSDArrN, {map_elem(s_A, TInitCell)}, TArrKey, TInitCell),
      make_specialized_arrmap(BKeysetN|BSDArrN, {map_elem(s_A, TArrKey)}, TArrKey, TArrKey)
    ),
    make_specialized_arrmap(BSDArrN, {map_elem(s_A, TUncArrKey)}, TUncArrKey, TUncArrKey)
  );

  EXPECT_EQ(
    intersection_of(
      make_specialized_arrpackedn(BVecishN|BKeysetN, ival(0)),
      TKeyset
    ),
    make_specialized_arrpacked(BKeysetN, {ival(0)})
  );
}

TEST_F(TypeTest, Split) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const test = [] (auto f, trep bits, const Type& split,
                        const Type& rest, const Type& orig) {
    EXPECT_TRUE(split.moreRefined(orig));
    EXPECT_TRUE(rest.moreRefined(orig));
    EXPECT_FALSE(split.couldBe(rest));
    EXPECT_TRUE(union_of(split, rest).equivalentlyRefined(orig));

    if (orig.couldBe(bits)) {
      EXPECT_TRUE(split.subtypeOf(bits));
    } else {
      EXPECT_EQ(split, TBottom);
    }
    EXPECT_FALSE(rest.couldBe(bits));
    if (orig.subtypeOf(bits)) {
      EXPECT_EQ(rest, TBottom);
    }

    if (f(orig)) {
      EXPECT_TRUE(f(split));
      EXPECT_FALSE(rest.hasData());
    } else if (orig.hasData()) {
      EXPECT_FALSE(split.hasData());
      EXPECT_TRUE(rest.hasData());
    } else {
      EXPECT_FALSE(split.hasData());
      EXPECT_FALSE(rest.hasData());
    }
  };

  auto const& types = allCases(index);
  for (auto const& t : types) {
    if (!t.subtypeOf(BCell)) continue;

    auto const [obj, objRest] = split_obj(t);
    test(is_specialized_obj, BObj, obj, objRest, t);

    auto const [cls, clsRest] = split_cls(t);
    test(is_specialized_cls, BCls, cls, clsRest, t);

    auto const [arr, arrRest] = split_array_like(t);
    test(is_specialized_array_like, BArrLike, arr, arrRest, t);

    auto const [str, strRest] = split_string(t);
    test(is_specialized_string, BStr, str, strRest, t);
  }

  auto split = split_array_like(Type{BDictN|BInt});
  EXPECT_EQ(split.first, TDictN);
  EXPECT_EQ(split.second, TInt);

  split = split_array_like(TVecE);
  EXPECT_EQ(split.first, TVecE);
  EXPECT_EQ(split.second, TBottom);

  split = split_array_like(TInt);
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, TInt);

  split = split_array_like(ival(123));
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, ival(123));

  split = split_array_like(union_of(TKeyset,ival(123)));
  EXPECT_EQ(split.first, TKeyset);
  EXPECT_EQ(split.second, TInt);

  split = split_array_like(make_specialized_arrmapn(BDictN, TStr, TObj));
  EXPECT_EQ(split.first, make_specialized_arrmapn(BDictN, TStr, TObj));
  EXPECT_EQ(split.second, TBottom);

  split = split_array_like(make_specialized_arrmapn(BDictN|BFalse, TStr, TObj));
  EXPECT_EQ(split.first, make_specialized_arrmapn(BDictN, TStr, TObj));
  EXPECT_EQ(split.second, TFalse);

  auto const clsA = index.resolve_class(Context{}, s_A.get());
  if (!clsA || !clsA->resolved()) ADD_FAILURE();

  split = split_obj(Type{BObj|BInt});
  EXPECT_EQ(split.first, TObj);
  EXPECT_EQ(split.second, TInt);

  split = split_obj(TObj);
  EXPECT_EQ(split.first, TObj);
  EXPECT_EQ(split.second, TBottom);

  split = split_obj(TInt);
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, TInt);

  split = split_obj(ival(123));
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, ival(123));

  split = split_obj(union_of(TObj,ival(123)));
  EXPECT_EQ(split.first, TObj);
  EXPECT_EQ(split.second, TInt);

  split = split_obj(make_specialized_sub_object(BObj, *clsA));
  EXPECT_EQ(split.first, make_specialized_sub_object(BObj, *clsA));
  EXPECT_EQ(split.second, TBottom);

  split = split_obj(make_specialized_sub_object(BObj|BFalse, *clsA));
  EXPECT_EQ(split.first, make_specialized_sub_object(BObj, *clsA));
  EXPECT_EQ(split.second, TFalse);

  split = split_cls(Type{BCls|BInt});
  EXPECT_EQ(split.first, TCls);
  EXPECT_EQ(split.second, TInt);

  split = split_cls(TCls);
  EXPECT_EQ(split.first, TCls);
  EXPECT_EQ(split.second, TBottom);

  split = split_cls(TInt);
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, TInt);

  split = split_cls(ival(123));
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, ival(123));

  split = split_cls(union_of(TCls,ival(123)));
  EXPECT_EQ(split.first, TCls);
  EXPECT_EQ(split.second, TInt);

  split = split_cls(make_specialized_sub_class(BCls, *clsA));
  EXPECT_EQ(split.first, make_specialized_sub_class(BCls, *clsA));
  EXPECT_EQ(split.second, TBottom);

  split = split_cls(make_specialized_sub_class(BCls|BFalse, *clsA));
  EXPECT_EQ(split.first, make_specialized_sub_class(BCls, *clsA));
  EXPECT_EQ(split.second, TFalse);

  split = split_string(TStr);
  EXPECT_EQ(split.first, TStr);
  EXPECT_EQ(split.second, TBottom);

  split = split_string(TInt);
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, TInt);

  split = split_string(sval(s_A));
  EXPECT_EQ(split.first, sval(s_A));
  EXPECT_EQ(split.second, TBottom);

  split = split_string(ival(123));
  EXPECT_EQ(split.first, TBottom);
  EXPECT_EQ(split.second, ival(123));

  split = split_string(Type{BStr|BInt});
  EXPECT_EQ(split.first, TStr);
  EXPECT_EQ(split.second, TInt);

  split = split_string(union_of(sval(s_A), TFalse));
  EXPECT_EQ(split.first, sval(s_A));
  EXPECT_EQ(split.second, TFalse);

  split = split_string(union_of(TStr, ival(123)));
  EXPECT_EQ(split.first, TStr);
  EXPECT_EQ(split.second, TInt);
}

TEST_F(TypeTest, Remove) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const test = [] (auto f, trep bits, const Type& removed,
                        const Type& orig) {
    EXPECT_TRUE(removed.moreRefined(orig));
    if (orig.couldBe(bits)) {
      EXPECT_TRUE(removed.strictlyMoreRefined(orig));
    }
    EXPECT_FALSE(removed.couldBe(bits));

    if (f(orig) || !orig.hasData()) {
      EXPECT_FALSE(removed.hasData());
    } else {
      EXPECT_TRUE(removed.hasData());
    }
  };

  auto const& types = allCases(index);
  for (auto const& t : types) {
    if (!t.subtypeOf(BCell)) continue;
    test(is_specialized_int, BInt, remove_int(t), t);
    test(is_specialized_double, BDbl, remove_double(t), t);
    test(is_specialized_string, BStr, remove_string(t), t);
    test(is_specialized_cls, BCls, remove_cls(t), t);
    test(is_specialized_obj, BObj, remove_obj(t), t);

    EXPECT_EQ(remove_int(t), remove_bits(t, BInt));
    EXPECT_EQ(remove_double(t), remove_bits(t, BDbl));
    EXPECT_EQ(remove_string(t), remove_bits(t, BStr));
    EXPECT_EQ(remove_cls(t), remove_bits(t, BCls));
    EXPECT_EQ(remove_obj(t), remove_bits(t, BObj));

    EXPECT_FALSE(is_specialized_array_like(remove_bits(t, BArrLikeN)));
    if (t.couldBe(BDictishN)) {
      EXPECT_FALSE(is_specialized_array_like(remove_bits(t, BDictishN)));
    }

    EXPECT_FALSE(remove_keyset(t).couldBe(BKeyset));
    if (!t.couldBe(BKeyset)) {
      EXPECT_EQ(remove_keyset(t), t);
    }
    if (t.subtypeAmong(BKeyset, BArrLike)) {
      EXPECT_FALSE(is_specialized_array_like(remove_keyset(t)));
    }
  }

  EXPECT_EQ(remove_int(TStr), TStr);
  EXPECT_EQ(remove_int(TInt), TBottom);
  EXPECT_EQ(remove_int(Type{BStr|BInt}), TStr);
  EXPECT_EQ(remove_int(ival(123)), TBottom);
  EXPECT_EQ(remove_int(dval(1.23)), dval(1.23));
  EXPECT_EQ(remove_int(union_of(ival(123),TDbl)), TDbl);
  EXPECT_EQ(remove_int(union_of(TInt,dval(1.23))), TDbl);

  EXPECT_EQ(remove_double(TStr), TStr);
  EXPECT_EQ(remove_double(TDbl), TBottom);
  EXPECT_EQ(remove_double(Type{BStr|BDbl}), TStr);
  EXPECT_EQ(remove_double(dval(1.23)), TBottom);
  EXPECT_EQ(remove_double(ival(123)), ival(123));
  EXPECT_EQ(remove_double(union_of(ival(123),TDbl)), TInt);
  EXPECT_EQ(remove_double(union_of(TInt,dval(1.23))), TInt);

  EXPECT_EQ(remove_string(TInt), TInt);
  EXPECT_EQ(remove_string(TStr), TBottom);
  EXPECT_EQ(remove_string(Type{BStr|BInt}), TInt);
  EXPECT_EQ(remove_string(ival(123)), ival(123));
  EXPECT_EQ(remove_string(sval(s_A)), TBottom);
  EXPECT_EQ(remove_string(union_of(ival(123),TStr)), TInt);
  EXPECT_EQ(remove_string(union_of(TInt,sval(s_A))), TInt);

  auto const clsA = index.resolve_class(Context{}, s_A.get());
  if (!clsA || !clsA->resolved()) ADD_FAILURE();

  EXPECT_EQ(remove_cls(TInt), TInt);
  EXPECT_EQ(remove_cls(TCls), TBottom);
  EXPECT_EQ(remove_cls(Type{BCls|BInt}), TInt);
  EXPECT_EQ(remove_cls(ival(123)), ival(123));
  EXPECT_EQ(remove_cls(make_specialized_sub_class(BCls, *clsA)), TBottom);
  EXPECT_EQ(remove_cls(union_of(ival(123),TCls)), TInt);
  EXPECT_EQ(remove_cls(make_specialized_sub_class(BCls|BFalse, *clsA)), TFalse);

  EXPECT_EQ(remove_obj(TInt), TInt);
  EXPECT_EQ(remove_obj(TObj), TBottom);
  EXPECT_EQ(remove_obj(Type{BInt|BObj}), TInt);
  EXPECT_EQ(remove_obj(ival(123)), ival(123));
  EXPECT_EQ(remove_obj(make_specialized_sub_object(BObj, *clsA)), TBottom);
  EXPECT_EQ(remove_obj(union_of(ival(123),TObj)), TInt);
  EXPECT_EQ(remove_obj(make_specialized_sub_object(BObj|BFalse, *clsA)), TFalse);
}

TEST_F(TypeTest, Prim) {
  const std::initializer_list<std::pair<Type, Type>> subtype_true{
    { TInt,      TPrim },
    { TBool,     TPrim },
    { TNum,      TPrim },
    { TInitNull, TPrim },
    { TDbl,      TPrim },
    { dval(0.0), TPrim },
    { ival(0),   TPrim },
    { TNull,     TPrim },
    { TInt,      TInitPrim },
    { TBool,     TInitPrim },
    { TNum,      TInitPrim },
    { TInitNull, TInitPrim },
    { TDbl,      TInitPrim },
    { dval(0.0), TInitPrim },
    { ival(0),   TInitPrim },
  };

  const std::initializer_list<std::pair<Type, Type>> subtype_false{
    { sval(s_test), TPrim },
    { TSStr, TPrim },
    { TNull, TInitPrim }, // TNull could be uninit
    { TPrim, TBool },
    { TPrim, TInt },
    { TPrim, TNum },
    { TInitPrim, TNum },
    { TUnc, TPrim },
    { TUnc, TInitPrim },
    { TInitUnc, TPrim },
    { TSStr, TInitPrim },
    { TRes, TPrim },
    { TObj, TPrim },
    { TRFunc, TPrim },
    { TPrim, dval(0.0) },
    { TCls, TInitPrim },
    { TFunc, TInitPrim },
  };

  const std::initializer_list<std::pair<Type, Type>> couldbe_true{
    { TPrim, TInt },
    { TPrim, TBool },
    { TPrim, TNum },
    { TInitPrim, TNum },
    { TInitPrim, TFalse },
    { TPrim, TCell },
    { TPrim, TOptObj },
    { TPrim, TOptRecord },
    { TPrim, TOptFalse },
  };

  const std::initializer_list<std::pair<Type, Type>> couldbe_false{
    { TPrim, TSStr },
    { TInitPrim, TSStr },
    { TInitPrim, sval(s_test) },
    { TPrim, sval(s_test) },
    { TInitPrim, TUninit },
    { TPrim, TObj },
    { TPrim, TRecord },
    { TPrim, TRes },
    { TPrim, TRFunc },
    { TPrim, TFunc },
  };

  for (auto kv : subtype_true) {
    EXPECT_TRUE(kv.first.subtypeOf(kv.second))
      << show(kv.first) << " subtypeOf " << show(kv.second);
  }

  for (auto kv : subtype_false) {
    EXPECT_FALSE(kv.first.subtypeOf(kv.second))
      << show(kv.first) << " !subtypeOf " << show(kv.second);
  }

  for (auto kv : couldbe_true) {
    EXPECT_TRUE(kv.first.couldBe(kv.second))
      << show(kv.first) << " couldbe " << show(kv.second);
    EXPECT_TRUE(kv.second.couldBe(kv.first))
      << show(kv.first) << " couldbe " << show(kv.second);
  }

  for (auto kv : couldbe_false) {
    EXPECT_FALSE(kv.first.couldBe(kv.second))
      << show(kv.first) << " !couldbe " << show(kv.second);
    EXPECT_FALSE(kv.second.couldBe(kv.first))
      << show(kv.first) << " !couldbe " << show(kv.second);
  }

  EXPECT_FALSE(TClsMeth.subtypeOf(TInitPrim));
  EXPECT_FALSE(TPrim.couldBe(TClsMeth));
}

TEST_F(TypeTest, CouldBeValues) {
  EXPECT_FALSE(ival(2).couldBe(ival(3)));
  EXPECT_TRUE(ival(2).couldBe(ival(2)));

  auto const packed_dict = static_dict(0, 42, 1, 23, 2, 12);
  auto const dict = static_dict(s_A.get(), s_B.get(), s_test.get(), 12);

  EXPECT_FALSE(dict_val(packed_dict).couldBe(dict_val(dict)));
  EXPECT_TRUE(dict_val(packed_dict).couldBe(dict_val(packed_dict)));
  EXPECT_TRUE(dval(2.0).couldBe(dval(2.0)));
  EXPECT_FALSE(dval(2.0).couldBe(dval(3.0)));

  EXPECT_FALSE(sval(s_test).couldBe(sval(s_A)));
  EXPECT_TRUE(sval(s_test).couldBe(sval(s_test)));
  EXPECT_FALSE(
    sval_nonstatic(s_test).couldBe(sval_nonstatic(s_A))
  );
  EXPECT_TRUE(
    sval_nonstatic(s_test).couldBe(sval_nonstatic(s_test))
  );
  EXPECT_TRUE(sval(s_test).couldBe(sval_nonstatic(s_test)));
  EXPECT_TRUE(sval_nonstatic(s_test).couldBe(sval(s_test)));
  EXPECT_FALSE(sval(s_test.get()).couldBe(sval_nonstatic(s_A)));
  EXPECT_FALSE(sval_nonstatic(s_test).couldBe(sval(s_A)));
}

TEST_F(TypeTest, Unc) {
  EXPECT_TRUE(TInt.subtypeOf(BInitUnc));
  EXPECT_TRUE(TInt.subtypeOf(BUnc));
  EXPECT_TRUE(TDbl.subtypeOf(BInitUnc));
  EXPECT_TRUE(TDbl.subtypeOf(BUnc));
  EXPECT_TRUE(dval(3.0).subtypeOf(BInitUnc));

  if (use_lowptr) {
    EXPECT_TRUE(TClsMeth.subtypeOf(BInitUnc));
  } else {
    EXPECT_FALSE(TClsMeth.subtypeOf(BInitUnc));
  }

  const std::initializer_list<std::tuple<Type, Type, bool>> tests{
    { TUnc, TInitUnc, true },
    { TUnc, TInitCell, true },
    { TUnc, TCell, true },
    { TInitUnc, TInt, true },
    { TInitUnc, TOptInt, true },
    { TInitUnc, opt(ival(2)), true },
    { TUnc, TInt, true },
    { TUnc, TOptInt, true },
    { TUnc, opt(ival(2)), true },
    { TNum, TUnc, true },
    { TNum, TInitUnc, true },
    { TUncArrKey, TInitUnc, true },
    { TClsMeth, TInitUnc, use_lowptr },
  };
  for (auto const& t : tests) {
    auto const& ty1 = std::get<0>(t);
    auto const& ty2 = std::get<1>(t);
    if (std::get<2>(t)) {
      EXPECT_TRUE(ty1.couldBe(ty2))
        << show(ty1) << " couldBe " << show(ty2);
    } else {
      EXPECT_FALSE(ty1.couldBe(ty2))
        << show(ty1) << " !couldBe " << show(ty2);
    }
  }
}

TEST_F(TypeTest, DblNan) {
  auto const qnan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_TRUE(dval(qnan).subtypeOf(dval(qnan)));
  EXPECT_TRUE(dval(qnan).couldBe(dval(qnan)));
  EXPECT_FALSE(dval(qnan).strictSubtypeOf(dval(qnan)));
  EXPECT_EQ(dval(qnan), dval(qnan));
  EXPECT_EQ(union_of(dval(qnan), dval(qnan)), dval(qnan));
  EXPECT_EQ(intersection_of(dval(qnan), dval(qnan)), dval(qnan));
}

TEST_F(TypeTest, ToObj) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BCls)) continue;
    EXPECT_TRUE(toobj(t).subtypeOf(BObj));
    if (!is_specialized_cls(t)) {
      EXPECT_EQ(toobj(t), TObj);
    } else {
      EXPECT_TRUE(is_specialized_obj(toobj(t)));
    }
  }

  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BObj)) continue;
    EXPECT_TRUE(objcls(t).subtypeOf(BCls));
    if (!is_specialized_obj(t)) {
      EXPECT_EQ(objcls(t), TCls);
    } else {
      EXPECT_TRUE(is_specialized_cls(objcls(t)));
    }
  }

  auto const clsA = index.resolve_class(Context{}, s_A.get());
  if (!clsA || !clsA->resolved()) ADD_FAILURE();

  auto const awaitable = index.builtin_class(s_Awaitable.get());

  EXPECT_EQ(toobj(TCls), TObj);
  EXPECT_EQ(toobj(make_specialized_sub_class(BCls, *clsA)),
            make_specialized_sub_object(BObj, *clsA));
  EXPECT_EQ(toobj(make_specialized_exact_class(BCls, *clsA)),
            make_specialized_exact_object(BObj, *clsA));

  EXPECT_EQ(objcls(TObj), TCls);
  EXPECT_EQ(objcls(make_specialized_sub_object(BObj, *clsA)),
            make_specialized_sub_class(BCls, *clsA));
  EXPECT_EQ(objcls(make_specialized_exact_object(BObj, *clsA)),
            make_specialized_exact_class(BCls, *clsA));
  EXPECT_EQ(objcls(make_specialized_wait_handle(BObj, TInt, index)),
            make_specialized_sub_class(BCls, awaitable));
}

TEST_F(TypeTest, Option) {
  auto const program = make_test_program();
  Index index { program.get() };

  EXPECT_TRUE(TTrue.subtypeOf(BOptTrue));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptTrue));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptTrue));

  EXPECT_TRUE(TFalse.subtypeOf(BOptFalse));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptFalse));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptFalse));

  EXPECT_TRUE(TFalse.subtypeOf(BOptBool));
  EXPECT_TRUE(TTrue.subtypeOf(BOptBool));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptBool));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptBool));

  EXPECT_TRUE(ival(3).subtypeOf(BOptInt));
  EXPECT_TRUE(TInt.subtypeOf(BOptInt));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptInt));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptInt));

  EXPECT_TRUE(TDbl.subtypeOf(BOptDbl));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptDbl));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptDbl));
  EXPECT_TRUE(dval(3.0).subtypeOf(BOptDbl));

  EXPECT_TRUE(sval(s_test).subtypeOf(BOptSStr));
  EXPECT_TRUE(sval(s_test).subtypeOf(BOptStr));
  EXPECT_TRUE(sval_nonstatic(s_test).subtypeOf(BOptStr));
  EXPECT_TRUE(TSStr.subtypeOf(BOptSStr));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptSStr));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptSStr));
  EXPECT_TRUE(!TStr.subtypeOf(BOptSStr));
  EXPECT_TRUE(TStr.couldBe(BOptSStr));

  EXPECT_TRUE(TStr.subtypeOf(BOptStr));
  EXPECT_TRUE(TSStr.subtypeOf(BOptStr));
  EXPECT_TRUE(sval(s_test).subtypeOf(BOptStr));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptStr));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptStr));

  EXPECT_TRUE(TSVArr.subtypeOf(BOptSVArr));
  EXPECT_TRUE(!TVArr.subtypeOf(BOptSVArr));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptSVArr));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptSVArr));

  EXPECT_TRUE(TDArr.subtypeOf(BOptDArr));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptDArr));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptDArr));

  EXPECT_TRUE(TObj.subtypeOf(BOptObj));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptObj));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptObj));

  EXPECT_TRUE(TRecord.subtypeOf(BOptRecord));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptRecord));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptRecord));

  EXPECT_TRUE(TRes.subtypeOf(BOptRes));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptRes));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptRes));

  EXPECT_TRUE(TClsMeth.subtypeOf(BOptClsMeth));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptClsMeth));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptClsMeth));

  EXPECT_TRUE(TRClsMeth.subtypeOf(BOptRClsMeth));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptRClsMeth));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptRClsMeth));

  EXPECT_TRUE(TLazyCls.subtypeOf(BOptLazyCls));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptLazyCls));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptLazyCls));

  EXPECT_TRUE(TArrKey.subtypeOf(BOptArrKey));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptArrKey));
  EXPECT_TRUE(!TUninit.subtypeOf(BOptArrKey));

  for (auto const& t : optionals()) {
    EXPECT_EQ(t, opt(unopt(t)));
  }

  EXPECT_TRUE(wait_handle(index, opt(dval(2.0))).couldBe(
    wait_handle(index, dval(2.0))));
}

TEST_F(TypeTest, OptUnionOf) {
  EXPECT_EQ(opt(ival(2)), union_of(ival(2), TInitNull));
  EXPECT_EQ(opt(dval(2.0)), union_of(TInitNull, dval(2.0)));
  EXPECT_EQ(opt(sval(s_test)), union_of(sval(s_test), TInitNull));
  EXPECT_EQ(opt(sval_nonstatic(s_test)),
            union_of(sval_nonstatic(s_test), TInitNull));
  EXPECT_EQ(opt(sval(s_test)), union_of(TInitNull, sval(s_test)));
  EXPECT_EQ(opt(sval_nonstatic(s_test)),
            union_of(TInitNull, sval_nonstatic(s_test)));

  EXPECT_EQ(TOptBool, union_of(TOptFalse, TOptTrue));
  EXPECT_EQ(TOptBool, union_of(TOptTrue, TOptFalse));

  EXPECT_EQ(TOptSDArr, union_of(TInitNull, TOptSDArr));
  EXPECT_EQ(TOptSVArr, union_of(TOptSVArr, TInitNull));
  EXPECT_EQ(TOptDArr, union_of(TOptDArr, TInitNull));
  EXPECT_EQ(TOptVArr, union_of(TInitNull, TOptVArr));

  EXPECT_EQ(TOptSStr,
            union_of(opt(sval(s_test)), opt(sval(s_TestClass))));
  EXPECT_EQ(TOptStr,
            union_of(opt(sval_nonstatic(s_test)),
                     opt(sval_nonstatic(s_TestClass))));

  EXPECT_EQ(TOptInt, union_of(opt(ival(2)), opt(ival(3))));
  EXPECT_EQ(TOptDbl, union_of(opt(dval(2.0)), opt(dval(3.0))));
  EXPECT_EQ(TOptNum, union_of(TInitNull, TNum));

  EXPECT_EQ(TOptTrue, union_of(TInitNull, TTrue));
  EXPECT_EQ(TOptFalse, union_of(TInitNull, TFalse));
  EXPECT_EQ(TOptRes, union_of(TInitNull, TRes));

  EXPECT_EQ(TOptTrue, union_of(TOptTrue, TTrue));
  EXPECT_EQ(TOptFalse, union_of(TOptFalse, TFalse));
  EXPECT_EQ(TOptBool, union_of(TOptTrue, TFalse));

  EXPECT_EQ(TOptClsMeth, union_of(TInitNull, TClsMeth));
  EXPECT_EQ(TOptRClsMeth, union_of(TInitNull, TRClsMeth));

  auto const program = make_test_program();
  Index index { program.get() };
  auto const rcls = index.builtin_class(s_Awaitable.get());

  EXPECT_TRUE(union_of(TObj, opt(objExact(rcls))) == TOptObj);

  auto wh1 = wait_handle(index, TInt);
  auto wh2 = wait_handle(index, ival(2));
  auto wh3 = wait_handle(index, ival(3));

  EXPECT_TRUE(union_of(wh1, wh2) == wh1);
  auto owh1 = opt(wh1);
  auto owh2 = opt(wh2);
  auto owh3 = opt(wh3);

  EXPECT_TRUE(union_of(owh1, owh2) == owh1);
  EXPECT_TRUE(union_of(owh1, wh2) == owh1);
  EXPECT_TRUE(union_of(owh2, wh1) == owh1);

  EXPECT_TRUE(union_of(wh1, owh2) == owh1);
  EXPECT_TRUE(union_of(wh2, owh1) == owh1);

  EXPECT_TRUE(union_of(wh2, owh3) == owh1);
  EXPECT_TRUE(union_of(owh2, wh3) == owh1);
}

TEST_F(TypeTest, TV) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    EXPECT_EQ(is_scalar(t), tv(t).has_value());
    if (!t.hasData() && !t.subtypeOf(BNull | BBool | BArrLikeE)) {
       EXPECT_FALSE(tv(t).has_value());
    }

    if (t.couldBe(BCounted & ~(BArrLike | BStr)) ||
        (t.couldBe(BStr) && t.subtypeAmong(BCStr, BStr)) ||
        (t.couldBe(BVArr) && t.subtypeAmong(BCVArr, BVArr)) ||
        (t.couldBe(BDArr) && t.subtypeAmong(BCDArr, BDArr)) ||
        (t.couldBe(BVec) && t.subtypeAmong(BCVec, BVec)) ||
        (t.couldBe(BDict) && t.subtypeAmong(BCDict, BDict)) ||
        (t.couldBe(BKeyset) && t.subtypeAmong(BCKeyset, BKeyset))) {
      EXPECT_FALSE(is_scalar(t));
      EXPECT_FALSE(tv(t).has_value());
    }

    if (t.couldBe(BInitNull) && !t.subtypeOf(BInitNull)) {
      EXPECT_FALSE(tv(t).has_value());
    }

    if (!t.subtypeOf(BInitNull)) {
      EXPECT_FALSE(is_scalar(opt(t)));
      EXPECT_FALSE(tv(opt(t)).has_value());
    }

    if (t.couldBe(BArrLikeE)) {
      if (!t.subtypeAmong(BVArrE, BArrLike) &&
          !t.subtypeAmong(BDArrE, BArrLike) &&
          !t.subtypeAmong(BVecE, BArrLike) &&
          !t.subtypeAmong(BDictE, BArrLike) &&
          !t.subtypeAmong(BKeysetE, BArrLike)) {
        EXPECT_FALSE(tv(t).has_value());
      }
    }
  }

  auto const test = [&] (const Type& t, TypedValue d) {
    auto const val = tv(t);
    EXPECT_TRUE(val && tvSame(*val, d));
  };
  test(TUninit, make_tv<KindOfUninit>());
  test(TInitNull, make_tv<KindOfNull>());
  test(TTrue, make_tv<KindOfBoolean>(true));
  test(TFalse, make_tv<KindOfBoolean>(false));
  test(aempty_varray(), make_array_like_tv(staticEmptyVArray()));
  test(some_aempty_varray(), make_array_like_tv(staticEmptyVArray()));
  test(vec_empty(), make_array_like_tv(staticEmptyVec()));
  test(some_vec_empty(), make_array_like_tv(staticEmptyVec()));
  test(aempty_darray(), make_array_like_tv(staticEmptyDArray()));
  test(some_aempty_darray(), make_array_like_tv(staticEmptyDArray()));
  test(dict_empty(), make_array_like_tv(staticEmptyDictArray()));
  test(some_dict_empty(), make_array_like_tv(staticEmptyDictArray()));
  test(keyset_empty(), make_array_like_tv(staticEmptyKeysetArray()));
  test(some_keyset_empty(), make_array_like_tv(staticEmptyKeysetArray()));
  test(ival(123), make_tv<KindOfInt64>(123));
  test(dval(3.141), make_tv<KindOfDouble>(3.141));
  test(sval(s_A), tv(s_A));
  test(vec_val(static_vec(123, 456, 789)),
       make_array_like_tv(const_cast<ArrayData*>(static_vec(123, 456, 789))));
  test(make_specialized_arrpacked(BDictN, {ival(1), ival(2), ival(3)}, LegacyMark::Unmarked),
       make_array_like_tv(const_cast<ArrayData*>(static_dict(0, 1, 1, 2, 2, 3))));
  test(make_specialized_arrpacked(BKeysetN, {ival(0), ival(1)}),
       make_array_like_tv(const_cast<ArrayData*>(static_keyset(0, 1))));

  test(
    make_specialized_arrmap(
      BDictN,
      {map_elem(s_A, ival(1)), map_elem(s_B, ival(2))},
      TBottom, TBottom,
      LegacyMark::Unmarked
    ),
    make_array_like_tv(const_cast<ArrayData*>(static_dict(s_A.get(), 1, s_B.get(), 2)))
  );
  test(
    make_specialized_arrmap(
      BDictN,
      {map_elem_nonstatic(s_A, ival(1)), map_elem_nonstatic(s_B, ival(2))},
      TBottom, TBottom,
      LegacyMark::Unmarked
    ),
    make_array_like_tv(const_cast<ArrayData*>(static_dict(s_A.get(), 1, s_B.get(), 2)))
  );

  EXPECT_FALSE(tv(TOptTrue).has_value());
  EXPECT_FALSE(tv(TOptFalse).has_value());
  EXPECT_FALSE(tv(TNull).has_value());
  EXPECT_FALSE(tv(union_of(dict_empty(), vec_empty())).has_value());
  EXPECT_FALSE(tv(make_specialized_int(BInt|BFalse, 123)).has_value());
  EXPECT_FALSE(tv(make_specialized_string(BStr|BFalse, s_A.get())).has_value());
  EXPECT_FALSE(
    tv(
      make_specialized_arrmap(
        BDict,
        {map_elem(s_A, ival(1)), map_elem(s_B, ival(2))},
        TBottom, TBottom,
        LegacyMark::Unmarked
      )
    ).has_value()
  );
  EXPECT_FALSE(
    tv(
      make_specialized_arrmap(
        BDictN,
        {map_elem_counted(s_A, ival(1)), map_elem_counted(s_B, ival(2))},
        TBottom, TBottom,
        LegacyMark::Unmarked
      )
    ).has_value()
  );

  EXPECT_FALSE(tv(sval_counted(s_A)).has_value());
  EXPECT_FALSE(tv(TCDictE).has_value());
  EXPECT_FALSE(tv(TCVecishE).has_value());
  EXPECT_FALSE(tv(make_specialized_arrpacked(BVecN, {sval_counted(s_A)})).has_value());
  EXPECT_FALSE(tv(make_specialized_arrpacked(BCVec, {ival(123)})).has_value());
  EXPECT_FALSE(
    tv(
      make_specialized_arrmap(
        BCDictN,
        {map_elem(s_A, ival(1)), map_elem(s_B, ival(2))},
        TBottom, TBottom,
        LegacyMark::Unmarked
      )
    ).has_value()
  );

  for (auto const& t : all) {
    EXPECT_EQ(is_scalar_counted(t), tvCounted(t).has_value());
    if (!t.hasData() && !t.subtypeOf(BNull | BBool | BArrLikeE)) {
      EXPECT_FALSE(tvCounted(t).has_value());
    }

    if (is_scalar(t)) {
      EXPECT_TRUE(is_scalar_counted(t));
      EXPECT_TRUE(tvCounted(t).has_value());
    }
    if (!is_scalar_counted(t)) {
      EXPECT_FALSE(is_scalar(t));
      EXPECT_FALSE(tv(t).has_value());
    }

    if (!(t.couldBe(BStr) && t.subtypeAmong(BCStr, BStr)) &&
        !(t.couldBe(BVArr) && t.subtypeAmong(BCVArr, BVArr)) &&
        !(t.couldBe(BDArr) && t.subtypeAmong(BCDArr, BDArr)) &&
        !(t.couldBe(BVec) && t.subtypeAmong(BCVec, BVec)) &&
        !(t.couldBe(BDict) && t.subtypeAmong(BCDict, BDict)) &&
        !(t.couldBe(BKeyset) && t.subtypeAmong(BCKeyset, BKeyset))) {
      EXPECT_EQ(is_scalar(t), is_scalar_counted(t));
      EXPECT_EQ(tv(t).has_value(), tvCounted(t).has_value());
    }

    if (t.couldBe(BInitNull) && !t.subtypeOf(BInitNull)) {
      EXPECT_FALSE(tvCounted(t).has_value());
    }

    if (!t.subtypeOf(BInitNull)) {
      EXPECT_FALSE(is_scalar_counted(opt(t)));
      EXPECT_FALSE(tvCounted(opt(t)).has_value());
    }

    if (t.couldBe(BArrLikeE)) {
      if (!t.subtypeAmong(BVArrE, BArrLike) &&
          !t.subtypeAmong(BDArrE, BArrLike) &&
          !t.subtypeAmong(BVecE, BArrLike) &&
          !t.subtypeAmong(BDictE, BArrLike) &&
          !t.subtypeAmong(BKeysetE, BArrLike)) {
        EXPECT_FALSE(tv(t).has_value());
      }
    }
  }

  auto const testC = [&] (const Type& t, TypedValue d) {
    auto const val = tvCounted(t);
    EXPECT_TRUE(val && tvSame(*val, d));
  };
  testC(make_unmarked(TCVArrE), make_array_like_tv(staticEmptyVArray()));
  testC(make_unmarked(TCDArrE), make_array_like_tv(staticEmptyDArray()));
  testC(make_unmarked(TCVecE), make_array_like_tv(staticEmptyVec()));
  testC(make_unmarked(TCDictE), make_array_like_tv(staticEmptyDictArray()));
  testC(TCKeysetE, make_array_like_tv(staticEmptyKeysetArray()));
  testC(sval_counted(s_A), tv(s_A));
  testC(
    make_unmarked(make_specialized_arrpacked(BVecN, {sval_counted(s_A)})),
    make_array_like_tv(const_cast<ArrayData*>(static_vec(s_A.get())))
  );
  testC(
    make_unmarked(make_specialized_arrpacked(BCVecN, {sval(s_A)})),
    make_array_like_tv(const_cast<ArrayData*>(static_vec(s_A.get())))
  );
  testC(
    make_specialized_arrmap(
      BDictN,
      {map_elem_counted(s_A, ival(1)), map_elem_counted(s_B, ival(2))},
      TBottom, TBottom,
      LegacyMark::Unmarked
    ),
    make_array_like_tv(const_cast<ArrayData*>(static_dict(s_A.get(), 1, s_B.get(), 2)))
  );
  testC(
    make_specialized_arrmap(
      BCDictN,
      {map_elem(s_A, ival(1)), map_elem(s_B, ival(2))},
      TBottom, TBottom,
      LegacyMark::Unmarked
    ),
    make_array_like_tv(const_cast<ArrayData*>(static_dict(s_A.get(), 1, s_B.get(), 2)))
  );
}

TEST_F(TypeTest, OptCouldBe) {
  for (auto const& t : optionals()) {
    if (t.subtypeOf(BInitNull)) continue;
    EXPECT_TRUE(t.couldBe(unopt(t)));
  }

  const std::initializer_list<std::pair<Type, Type>> true_cases{
    { opt(sval(s_test)), TStr },
    { opt(sval(s_test)), TInitNull },
    { opt(sval(s_test)), TSStr },
    { opt(sval(s_test)), sval(s_test) },
    { opt(sval(s_test)), sval_nonstatic(s_test) },

    { opt(sval_nonstatic(s_test)), TStr },
    { opt(sval_nonstatic(s_test)), TInitNull },
    { opt(sval_nonstatic(s_test)), TSStr },
    { opt(sval_nonstatic(s_test)), sval_nonstatic(s_test) },
    { opt(sval_nonstatic(s_test)), sval(s_test) },

    { opt(ival(2)), TInt },
    { opt(ival(2)), TInitNull },
    { opt(ival(2)), ival(2) },

    { opt(dval(2.0)), TDbl },
    { opt(dval(2.0)), TInitNull },
    { opt(dval(2.0)), dval(2) },

    { opt(TFalse), TBool },
    { opt(TFalse), TFalse },

    { opt(TTrue), TBool },
    { opt(TTrue), TTrue },

    { opt(TDbl), opt(TNum) },
    { TDbl, opt(TNum) },
    { TNum, opt(TDbl) },

    { opt(TInt), TNum },
    { TInt, opt(TNum) },
    { opt(TDbl), TNum },
  };

  for (auto kv : true_cases) {
    EXPECT_TRUE(kv.first.couldBe(kv.second))
      << show(kv.first) << " couldBe " << show(kv.second)
      << " should be true";
  }

  const std::initializer_list<std::pair<Type, Type>> false_cases{
    { opt(ival(2)), TDbl },
    { opt(dval(2.0)), TInt },
    { opt(TFalse), TTrue },
    { opt(TTrue), TFalse },
    { TFalse, opt(TNum) },
  };

  for (auto kv : false_cases) {
    EXPECT_TRUE(!kv.first.couldBe(kv.second))
      << show(kv.first) << " couldBe " << show(kv.second)
      << " should be false";
  }

  for (auto kv : boost::join(true_cases, false_cases)) {
    EXPECT_EQ(kv.first.couldBe(kv.second), kv.second.couldBe(kv.first))
      << show(kv.first) << " couldBe " << show(kv.second)
      << " wasn't reflexive";
  }

  for (auto const& x : optionals()) {
    if (!x.subtypeOf(BInitNull)) {
      EXPECT_TRUE(x.couldBe(unopt(x)));
    }
    EXPECT_TRUE(x.couldBe(BInitNull));
    for (auto const& y : optionals()) {
      EXPECT_TRUE(x.couldBe(y));
    }
  }
}

TEST_F(TypeTest, ArrayLikeElem) {
  auto const program = make_test_program();
  Index index { program.get() };

  const std::vector<Type> keys{
    TInt,
    TStr,
    TSStr,
    TCStr,
    TArrKey,
    TUncArrKey,
    sval(s_A),
    sval(s_B),
    sval(s_C),
    sval_nonstatic(s_A),
    sval_counted(s_A),
    ival(0),
    ival(1),
    ival(123),
    ival(777),
    ival(-1),
    ival(std::numeric_limits<int64_t>::max()),
    union_of(sval(s_A),TInt),
    union_of(sval(s_B),TInt),
    union_of(sval(s_C),TInt),
    union_of(sval_counted(s_A),TInt),
    union_of(ival(0),TStr),
    union_of(ival(1),TStr),
    union_of(ival(123),TStr),
    union_of(ival(777),TStr),
    union_of(ival(-1),TStr),
    union_of(ival(std::numeric_limits<int64_t>::max()),TStr)
  };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.couldBe(BArrLike)) continue;

    EXPECT_EQ(array_like_elem(t, TStr), array_like_elem(t, TSStr));
    EXPECT_EQ(array_like_elem(t, TStr), array_like_elem(t, TCStr));
    EXPECT_EQ(array_like_elem(t, TArrKey), array_like_elem(t, TUncArrKey));
    EXPECT_EQ(array_like_elem(t, TArrKey), array_like_elem(t, Type{BInt|BCStr}));
    EXPECT_EQ(array_like_elem(t, sval(s_A)), array_like_elem(t, sval_nonstatic(s_A)));
    EXPECT_EQ(array_like_elem(t, sval(s_A)), array_like_elem(t, sval_counted(s_A)));
    EXPECT_EQ(array_like_elem(t, union_of(TInt,sval(s_A))),
              array_like_elem(t, union_of(TInt,sval_nonstatic(s_A))));
    EXPECT_EQ(array_like_elem(t, union_of(TInt,sval(s_A))),
              array_like_elem(t, union_of(TInt,sval_counted(s_A))));

    for (auto const& key : keys) {
      auto const elem = array_like_elem(t, key);

      if (elem.first.is(BBottom)) {
        EXPECT_FALSE(elem.second);
      }
      if (t.couldBe(BArrLikeE)) {
        EXPECT_FALSE(elem.second);
      }
      if (!is_specialized_array_like(t)) {
        EXPECT_FALSE(elem.second);
        if (!t.couldBe(BKeysetN)) {
          EXPECT_FALSE(elem.first.hasData());
        }
      }
      if (!key.hasData()) {
        EXPECT_FALSE(elem.second);
      }
      if (elem.second) {
        EXPECT_TRUE(is_scalar_counted(key));
        EXPECT_TRUE(is_specialized_array_like_arrval(t) ||
                    is_specialized_array_like_map(t) ||
                    (key.subtypeOf(BInt) &&
                     is_specialized_int(key) &&
                     is_specialized_array_like_packed(t)));
      }

      EXPECT_TRUE(elem.first.subtypeOf(BInitCell));
      if (!t.couldBe(BArrLikeN)) {
        EXPECT_TRUE(elem.first.is(BBottom));
      }
      if (t.subtypeAmong(BSArrLikeN, BArrLikeN)) {
        EXPECT_TRUE(elem.first.subtypeOf(BInitUnc));
      }
      if (t.subtypeAmong(BKeysetN, BArrLikeN)) {
        EXPECT_TRUE(elem.first.subtypeOf(BArrKey));
      }
      if (t.subtypeAmong(BSKeysetN, BArrLikeN)) {
        EXPECT_TRUE(elem.first.subtypeOf(BUncArrKey));
      }
      if (t.subtypeAmong(BKeysetN, BArrLikeN)) {
        EXPECT_TRUE(elem.first.subtypeOf(loosen_staticness(key)));
      }

      if (t.subtypeAmong(BVecishN, BArrLikeN) && !key.couldBe(BInt)) {
        EXPECT_TRUE(elem.first.is(BBottom));
      }
      if ((is_specialized_array_like_packedn(t) ||
           is_specialized_array_like_packed(t)) && !key.couldBe(BInt)) {
        EXPECT_TRUE(elem.first.is(BBottom));
      }
      if ((is_specialized_array_like_packedn(t) ||
           is_specialized_array_like_packed(t)) &&
          is_specialized_int(key) && ival_of(key) < 0) {
        EXPECT_TRUE(elem.first.is(BBottom));
      }

      if (t.subtypeOf(BCell)) {
        auto const arr = split_array_like(t).first;
        EXPECT_EQ(array_like_elem(arr, key), elem);
      }

      auto const unionTest = [&] (const Type& key2) {
        auto const elem2 = array_like_elem(t, key2);
        auto const elem3 = array_like_elem(t, union_of(key, key2));
        EXPECT_EQ(elem3.first, union_of(elem.first, elem2.first));
        EXPECT_EQ(elem3.second, elem.second && elem2.second);
      };
      if (!key.hasData() || is_specialized_int(key)) unionTest(TInt);
      if (!key.hasData() || is_specialized_string(key)) unionTest(TStr);
      if (!key.hasData()) unionTest(TArrKey);
    }
  }

  auto const staticVec = static_vec(s_A, 100, s_B);
  auto const staticDict = static_dict(s_A, 100, 200, s_B, s_C, s_BA);
  auto const staticKeyset = static_keyset(s_A, 100, s_B);

  auto const mapElems1 = MapElems{
    map_elem(s_A, ival(100)),
    map_elem(200, sval(s_B)),
    map_elem(s_C, sval(s_BA))
  };
  auto const mapElems2 = MapElems{
    map_elem_nonstatic(s_A, ival(100)),
    map_elem(200, sval(s_B)),
    map_elem_nonstatic(s_C, sval(s_BA))
  };
  auto const mapElems3 = MapElems{
    map_elem(s_A, TObj),
    map_elem(s_B, TArrKey)
  };
  auto const mapElems4 = MapElems{
    map_elem(100, TObj),
    map_elem(200, TFalse)
  };

  const std::vector<std::tuple<Type, Type, Type, bool>> tests{
    { TVecishE, TInt, TBottom, false },
    { TVecishE, TStr, TBottom, false },
    { TVecishE, TArrKey, TBottom, false },
    { TVecishN, TInt, TInitCell, false },
    { TVecish, TInt, TInitCell, false },
    { TSVecishN, TInt, TInitUnc, false },
    { TVecishN, TStr, TBottom, false },
    { TVecishN, ival(-1), TBottom, false },
    { TVecishN, ival(0), TInitCell, false },
    { TSVecishN, ival(0), TInitUnc, false },
    { TVecishN, TArrKey, TInitCell, false },
    { TSVecishN, TArrKey, TInitUnc, false },
    { TVecishN, union_of(ival(-1),TStr), TInitCell, false },
    { TVecishN, union_of(ival(0),TStr), TInitCell, false },
    { TVecishN, union_of(TInt,sval(s_A)), TInitCell, false },

    { TDictishE, TInt, TBottom, false },
    { TDictishE, TStr, TBottom, false },
    { TDictishE, TArrKey, TBottom, false },
    { TDictishN, TInt, TInitCell, false },
    { TDictish, TInt, TInitCell, false },
    { TDictish, TStr, TInitCell, false },
    { TSDictishN, TInt, TInitUnc, false },
    { TDictishN, TStr, TInitCell, false },
    { TSDictishN, TStr, TInitUnc, false },
    { TDictishN, ival(-1), TInitCell, false },
    { TSDictishN, ival(-1), TInitUnc, false },
    { TDictishN, ival(0), TInitCell, false },
    { TSDictishN, ival(0), TInitUnc, false },
    { TDictishN, sval(s_A), TInitCell, false },
    { TSDictishN, sval(s_A), TInitUnc, false },
    { TDictishN, union_of(ival(-1),TStr), TInitCell, false },
    { TDictishN, union_of(ival(0),TStr), TInitCell, false },
    { TDictishN, union_of(TInt,sval(s_A)), TInitCell, false },
    { TDictishN, TArrKey, TInitCell, false },
    { TSDictishN, TArrKey, TInitUnc, false },

    { TKeysetE, TInt, TBottom, false },
    { TKeysetE, TStr, TBottom, false },
    { TKeysetE, TArrKey, TBottom, false },
    { TKeysetN, TInt, TInt, false },
    { TKeyset, TStr, TStr, false },
    { TKeyset, TSStr, TStr, false },
    { TKeyset, TCStr, TStr, false },
    { TKeyset, TInt, TInt, false },
    { TKeyset, TArrKey, TArrKey, false },
    { TKeyset, TUncArrKey, TArrKey, false },
    { TSKeyset, TArrKey, TUncArrKey, false },
    { TSKeyset, TStr, TSStr, false },
    { TSKeyset, TInt, TInt, false },
    { TKeysetN, ival(-1), ival(-1), false },
    { TSKeysetN, ival(-1), ival(-1), false },
    { TKeysetN, ival(0), ival(0), false },
    { TSKeysetN, ival(0), ival(0), false },
    { TKeysetN, sval(s_A), sval_nonstatic(s_A), false },
    { TSKeysetN, sval(s_A), sval(s_A), false },
    { TKeysetN, sval_nonstatic(s_A), sval_nonstatic(s_A), false },
    { TSKeysetN, sval_nonstatic(s_A), sval(s_A), false },
    { TKeysetN, union_of(ival(0),TStr), union_of(ival(0),TStr), false },
    { TSKeysetN, union_of(ival(0),TStr), union_of(ival(0),TSStr), false },
    { TKeysetN, union_of(TInt,sval(s_A)), union_of(TInt,sval_nonstatic(s_A)), false },
    { TSKeysetN, union_of(TInt,sval(s_A)), union_of(TInt,sval(s_A)), false },

    { make_specialized_arrval(BSVecN, staticVec), TInt, TUncArrKey, false },
    { make_specialized_arrval(BSVecN, staticVec), TStr, TBottom, false },
    { make_specialized_arrval(BSVecN, staticVec), TArrKey, TUncArrKey, false },
    { make_specialized_arrval(BSVecN, staticVec), ival(0), sval(s_A), true },
    { make_specialized_arrval(BSVec, staticVec), ival(0), sval(s_A), false },
    { make_specialized_arrval(BSVecN, staticVec), ival(1), ival(100), true },
    { make_specialized_arrval(BSVec, staticVec), ival(1), ival(100), false },
    { make_specialized_arrval(BSVecN, staticVec), ival(3), TBottom, false },
    { make_specialized_arrval(BSVecN, staticVec), ival(-1), TBottom, false },
    { make_specialized_arrval(BSVecN, staticVec), sval(s_A), TBottom, false },
    { make_specialized_arrval(BSVecN, staticVec), union_of(ival(0),TStr), TUncArrKey, false },
    { make_specialized_arrval(BSVecN, staticVec), union_of(ival(1),TStr), TUncArrKey, false },
    { make_specialized_arrval(BSVecN, staticVec), union_of(TInt,sval(s_A)), TUncArrKey, false },

    { make_specialized_arrval(BSDictN, staticDict), TInt, sval(s_B), false },
    { make_specialized_arrval(BSDictN, staticDict), TStr, union_of(sval(s_BA),TInt), false },
    { make_specialized_arrval(BSDictN, staticDict), TCStr, union_of(sval(s_BA),TInt), false },
    { make_specialized_arrval(BSDictN, staticDict), TArrKey, TUncArrKey, false },
    { make_specialized_arrval(BSDictN, staticDict), ival(0), TBottom, false },
    { make_specialized_arrval(BSDictN, staticDict), ival(-1), TBottom, false },
    { make_specialized_arrval(BSDictN, staticDict), ival(200), sval(s_B), true },
    { make_specialized_arrval(BSDict, staticDict), ival(200), sval(s_B), false },
    { make_specialized_arrval(BSDictN, staticDict), sval(s_A), ival(100), true },
    { make_specialized_arrval(BSDict, staticDict), sval(s_A), ival(100), false },
    { make_specialized_arrval(BSDictN, staticDict), sval_counted(s_A), ival(100), true },
    { make_specialized_arrval(BSDictN, staticDict), sval(s_C), sval(s_BA), true },
    { make_specialized_arrval(BSDict, staticDict), sval(s_C), sval(s_BA), false },
    { make_specialized_arrval(BSDictN, staticDict), sval_counted(s_C), sval(s_BA), true },
    { make_specialized_arrval(BSDictN, staticDict), union_of(ival(0),TStr), union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrval(BSDictN, staticDict), union_of(ival(100),TStr), union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrval(BSDictN, staticDict), union_of(TInt,sval(s_A)), union_of(TInt,sval(s_B)), false },
    { make_specialized_arrval(BSDictN, staticDict), union_of(TInt,sval(s_C)), TUncArrKey, false },

    { make_specialized_arrval(BSKeysetN, staticKeyset), TInt, ival(100), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), TStr, TSStr, false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), TArrKey, TUncArrKey, false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), ival(0), TBottom, false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), sval(s_C), TBottom, false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), ival(100), ival(100), true },
    { make_specialized_arrval(BSKeyset, staticKeyset), ival(100), ival(100), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), sval(s_A), sval(s_A), true },
    { make_specialized_arrval(BSKeyset, staticKeyset), sval(s_A), sval(s_A), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), sval_counted(s_A), sval(s_A), true },
    { make_specialized_arrval(BSKeysetN, staticKeyset), sval(s_B), sval(s_B), true },
    { make_specialized_arrval(BSKeyset, staticKeyset), sval(s_B), sval(s_B), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), sval_counted(s_B), sval(s_B), true },
    { make_specialized_arrval(BSKeysetN, staticKeyset), union_of(ival(0),TStr), TUncArrKey, false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), union_of(ival(100),TStr), union_of(ival(100),TSStr), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), union_of(TInt,sval(s_A)), union_of(TInt,sval(s_A)), false },
    { make_specialized_arrval(BSKeysetN, staticKeyset), union_of(TInt,sval(s_B)), union_of(TInt,sval(s_B)), false },

    { make_specialized_arrpackedn(BDictishN, TInitCell), TInt, TInitCell, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), TStr, TBottom, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), TArrKey, TInitCell, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), ival(-1), TBottom, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), sval(s_A), TBottom, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), ival(0), TInitCell, false },
    { make_specialized_arrpackedn(BSDictishN, TInitUnc), ival(0), TInitUnc, false },
    { make_specialized_arrpackedn(BDictishN, TObj), ival(0), TObj, false },
    { make_specialized_arrpackedn(BDictish, TObj), ival(0), TObj, false },
    { make_specialized_arrpackedn(BDictishN, TSStr), ival(0), TSStr, false },
    { make_specialized_arrpackedn(BDictishN, TObj), union_of(ival(-1),TStr), TObj, false },
    { make_specialized_arrpackedn(BDictishN, TObj), union_of(ival(0),TStr), TObj, false },
    { make_specialized_arrpackedn(BDictishN, TObj), union_of(TInt,sval(s_A)), TObj, false },

    { make_specialized_arrpacked(BDictishN, {TInitCell}), TInt, TInitCell, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), TStr, TBottom, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), TArrKey, TInitCell, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), ival(-1), TBottom, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), sval(s_A), TBottom, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), ival(1), TBottom, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), ival(0), TInitCell, true },
    { make_specialized_arrpacked(BDictish, {TInitCell}), ival(0), TInitCell, false },
    { make_specialized_arrpacked(BSDictishN, {TInitUnc}), ival(0), TInitUnc, true },
    { make_specialized_arrpacked(BSDictish, {TInitUnc}), ival(0), TInitUnc, false },
    { make_specialized_arrpacked(BDictishN, {TObj}), ival(0), TObj, true },
    { make_specialized_arrpacked(BDictishN, {TSStr}), TInt, TSStr, false },
    { make_specialized_arrpacked(BDictishN, {TObj}), union_of(ival(1),TStr), TObj, false },
    { make_specialized_arrpacked(BDictishN, {TObj}), union_of(ival(0),TStr), TObj, false },
    { make_specialized_arrpacked(BDictishN, {TObj}), union_of(TInt,sval(s_A)), TObj, false },
    { make_specialized_arrpacked(BDictishN, {TObj, TInt}), TInt, Type{BObj|BInt}, false },
    { make_specialized_arrpacked(BDictishN, {TObj, TInt}), ival(0), TObj, true },
    { make_specialized_arrpacked(BDictishN, {TObj, TInt}), ival(1), TInt, true },
    { make_specialized_arrpacked(BDictishN, {TObj, TInt}), ival(2), TBottom, false },

    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TInt, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TSStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TCStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TArrKey, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), TUncArrKey, TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), ival(0), TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), ival(-1), TObj, false },
    { make_specialized_arrmapn(BDictishN, TArrKey, TObj), sval(s_A), TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TInt, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TSStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TCStr, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TArrKey, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TObj), TUncArrKey, TObj, false },
    { make_specialized_arrmapn(BDictishN, TUncArrKey, TSStr), TInt, TSStr, false },
    { make_specialized_arrmapn(BDictishN, TInt, TObj), TInt, TObj, false },
    { make_specialized_arrmapn(BDictishN, TInt, TObj), TStr, TBottom, false },
    { make_specialized_arrmapn(BDictishN, TInt, TObj), TArrKey, TObj, false },

    { make_specialized_arrmap(BDictishN, mapElems1), TInt, sval(s_B), false },
    { make_specialized_arrmap(BDictishN, mapElems1), TStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems1), TSStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems1), TCStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems2), TInt, sval(s_B), false },
    { make_specialized_arrmap(BDictishN, mapElems2), TStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems2), TSStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems2), TCStr, union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems1), TArrKey, TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems1), TUncArrKey, TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems2), TArrKey, TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems2), TUncArrKey, TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems3), TInt, TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems3), TStr, Type{BObj|BArrKey}, false },
    { make_specialized_arrmap(BDictishN, mapElems3), ival(100), TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems1), ival(0), TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems1), sval(s_B), TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems1), ival(200), sval(s_B), true },
    { make_specialized_arrmap(BDictish, mapElems1), ival(200), sval(s_B), false },
    { make_specialized_arrmap(BDictishN, mapElems1), sval(s_A), ival(100), true },
    { make_specialized_arrmap(BDictish, mapElems1), sval(s_A), ival(100), false },
    { make_specialized_arrmap(BDictishN, mapElems1), sval_nonstatic(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems1), sval_counted(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems2), ival(200), sval(s_B), true },
    { make_specialized_arrmap(BDictishN, mapElems2), sval(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems2), sval_nonstatic(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems2), sval_counted(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems3), union_of(ival(0),TStr), Type{BObj|BArrKey}, false },
    { make_specialized_arrmap(BDictishN, mapElems3), union_of(TInt,sval(s_BA)), Type{BArrKey|BObj}, false },
    { make_specialized_arrmap(BDictishN, mapElems3), union_of(TInt,sval(s_A)), Type{BArrKey|BObj}, false },
    { make_specialized_arrmap(BDictishN, mapElems3), union_of(TInt,sval(s_B)), Type{BArrKey|BObj}, false },
    { make_specialized_arrmap(BDictishN, mapElems1), union_of(ival(0),TStr), union_of(TInt,sval(s_BA)), false },
    { make_specialized_arrmap(BDictishN, mapElems1), union_of(TInt,sval(s_BA)), TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems1), union_of(TInt,sval(s_A)), union_of(TInt,sval(s_B)), false },
    { make_specialized_arrmap(BDictishN, mapElems1), union_of(ival(200),TStr), TUncArrKey, false },
    { make_specialized_arrmap(BDictishN, mapElems4), union_of(ival(100),TStr), Type{BObj|BFalse}, false },
    { make_specialized_arrmap(BDictishN, mapElems4), union_of(ival(200),TStr), Type{BObj|BFalse}, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TInt, TObj), ival(0), TObj, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TSStr, TObj), sval(s_BA), TObj, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TSStr, TObj), sval_nonstatic(s_BA), TObj, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TSStr, TObj), sval_counted(s_BA), TObj, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TSStr, TObj), ival(0), TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems1, TInt, TObj), sval(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems1, TStr, TObj), sval(s_A), ival(100), true },
    { make_specialized_arrmap(BDictishN, mapElems3, TInt, TFalse), TInt, TFalse, false },
    { make_specialized_arrmap(BDictishN, mapElems3, TInt, TFalse), ival(0), TFalse, false },
    { make_specialized_arrmap(BDictishN, mapElems3, TStr, TFalse), TInt, TBottom, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), TStr, TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), TCStr, TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), TSStr, TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), sval(s_A), TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), sval_nonstatic(s_A), TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TSStr, TNum), sval_counted(s_A), TNum, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TStr, TNum), union_of(ival(0),TStr), Type{BNum|BFalse|BObj}, false },
    { make_specialized_arrmap(BDictishN, mapElems4, TStr, TNum), union_of(ival(100),TStr), Type{BObj|BNum|BFalse}, false },
    { make_specialized_arrmap(BDictishN, mapElems3, TInt, TNum), union_of(TInt,sval(s_BA)), Type{BObj|BNum|BStr}, false },
    { make_specialized_arrmap(BDictishN, mapElems3, TInt, TNum), union_of(TInt,sval(s_A)), Type{BObj|BNum|BStr}, false }
  };
  for (auto const& t : tests) {
    auto const elem = array_like_elem(std::get<0>(t), std::get<1>(t));
    EXPECT_EQ(elem.first, std::get<2>(t));
    EXPECT_EQ(elem.second, std::get<3>(t));
  }
}

TEST_F(TypeTest, ArrayLikeNewElem) {
  auto const program = make_test_program();
  Index index { program.get() };

  const std::vector<Type> values{
    TInt,
    TStr,
    TSStr,
    TCStr,
    TUncArrKey,
    TArrKey,
    ival(0),
    ival(1),
    ival(123),
    ival(777),
    ival(-1),
    ival(std::numeric_limits<int64_t>::max()),
    sval(s_A),
    sval(s_B),
    sval(s_C),
    sval_nonstatic(s_A),
    sval_counted(s_A),
    TObj,
    TInitUnc,
    TInitCell,
    Type{BObj|BInt},
    union_of(sval(s_A),TInt),
    union_of(sval(s_B),TInt),
    union_of(sval(s_C),TInt),
    union_of(sval_counted(s_A),TInt),
    union_of(ival(0),TStr),
    union_of(ival(1),TStr),
    union_of(ival(123),TStr),
    union_of(ival(777),TStr),
    union_of(ival(-1),TStr),
    union_of(ival(std::numeric_limits<int64_t>::max()),TStr)
  };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.subtypeOf(BCell) || !t.couldBe(BArrLike)) continue;

    for (auto const& v : values) {
      auto const newelem = array_like_newelem(t, v);
      EXPECT_FALSE(newelem.first.couldBe(BArrLikeE));

      if (!newelem.first.couldBe(BArrLike)) {
        EXPECT_TRUE(newelem.second);
      } else {
        EXPECT_FALSE(newelem.first.subtypeAmong(BSArrLike, BArrLike));
        if (!t.subtypeAmong(BKeyset, BArrLike) &&
            !array_like_elem(newelem.first, ival(std::numeric_limits<int64_t>::max())).second) {
          EXPECT_TRUE(v.subtypeOf(array_like_elem(newelem.first, TInt).first));
        }
      }

      if (t.subtypeAmong(BVecish, BArrLike)) {
        EXPECT_FALSE(newelem.second);
      }

      if (!t.couldBe(BArrLikeN) && !t.couldBe(BKeyset)) {
        EXPECT_TRUE(newelem.first.couldBe(BArrLike));
        EXPECT_TRUE(is_specialized_array_like_packed(newelem.first));
        EXPECT_EQ(array_like_elem(newelem.first, ival(0)).first, v);
        auto const size = arr_size(split_array_like(newelem.first).first);
        EXPECT_TRUE(size && *size == 1);
        EXPECT_EQ(array_like_elem(newelem.first, ival(1)).first, TBottom);
        EXPECT_FALSE(newelem.second);
      }

      auto [arr, rest] = split_array_like(t);
      auto const elem2 = array_like_newelem(arr, v);
      EXPECT_EQ(newelem.first, union_of(elem2.first, rest));
      EXPECT_EQ(newelem.second, elem2.second);
    }
  }

  auto const mapElem1 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj)
  };
  auto const mapElem2 = MapElems{
    map_elem(s_A, TInt),
    map_elem(100, TObj),
    map_elem(s_B, TFalse),
    map_elem(50, TObj),
  };
  auto const mapElem3 = MapElems{
    map_elem(std::numeric_limits<int64_t>::max(), TInitCell)
  };
  auto const mapElem4 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(int64_t(0), TFalse)
  };
  auto const mapElem5 = MapElems{
    map_elem(s_A, TInt),
    map_elem(100, TObj),
    map_elem(s_B, TFalse),
    map_elem(50, TObj),
    map_elem(101, TInitCell)
  };
  auto const mapElem6 = MapElems{
    map_elem(s_A, ival(100)),
    map_elem(200, sval(s_B)),
    map_elem(s_C, sval(s_BA)),
    map_elem(201, TInt)
  };
  auto const mapElem7 = MapElems{
    map_elem(1, ival(1))
  };
  auto const mapElem8 = MapElems{
    map_elem(s_A, sval(s_A))
  };
  auto const mapElem9 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(s_A, sval(s_A))
  };
  auto const mapElem10 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem(s_A, sval(s_A))
  };
  auto const mapElem11 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem_nonstatic(s_A, sval_nonstatic(s_A))
  };
  auto const mapElem12 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem_nonstatic(s_A, sval_nonstatic(s_A))
  };
  auto const mapElem13 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(-1, ival(-1))
  };
  auto const mapElem14 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem(3, ival(3))
  };
  auto const mapElem15 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem(s_A, sval(s_A)),
    map_elem(100, ival(100))
  };
  auto const mapElem16 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem(s_A, sval(s_A)),
    map_elem_nonstatic(s_B, sval_nonstatic(s_B))
  };
  auto const mapElem17 = MapElems{
    map_elem(int64_t(0), ival(0)),
    map_elem(1, ival(1)),
    map_elem(s_A, sval(s_A)),
    map_elem(s_B, sval(s_B))
  };
  auto const mapElem18 = MapElems{
    map_elem(1, ival(1)),
    map_elem(s_A, sval(s_A))
  };
  auto const mapElem19 = MapElems{
    map_elem_nonstatic(s_A, sval_nonstatic(s_A))
  };

  auto const staticVec = static_vec(s_A, s_B, s_C);
  auto const staticDict = static_dict(s_A, 100, 200, s_B, s_C, s_BA);

  const std::vector<std::tuple<Type, Type, Type, bool>> tests{
    { TVecishE, TObj, make_specialized_arrpacked(BVecishN, {TObj}), false },
    { TSVecishE, TObj, make_specialized_arrpacked(BVecishN, {TObj}), false },
    { TCVecishE, TObj, make_specialized_arrpacked(BVecishN, {TObj}), false },
    { TVecishN, TObj, TVecishN, false },
    { TSVecishN, TObj, TVecishN, false },
    { TCVecishN, TObj, TVecishN, false },
    { TVecish, TObj, TVecishN, false },
    { TSVecish, TObj, TVecishN, false },
    { TCVecish, TObj, TVecishN, false },
    { TDictishE, TObj, make_specialized_arrpacked(BDictishN, {TObj}), false },
    { TSDictishE, TObj, make_specialized_arrpacked(BDictishN, {TObj}), false },
    { TCDictishE, TObj, make_specialized_arrpacked(BDictishN, {TObj}), false },
    { TDictishN, TObj, TDictishN, true },
    { TSDictishN, TObj, TDictishN, true },
    { TCDictishN, TObj, TDictishN, true },
    { TDictish, TObj, TDictishN, true },
    { TSDictish, TObj, TDictishN, true },
    { TCDictish, TObj, TDictishN, true },
    { TKeysetE, TObj, TBottom, true },
    { TKeysetN, TObj, TBottom, true },
    { TKeyset, TObj, TBottom, true },
    { TSKeysetE, TFalse, TBottom, true },
    { TSKeysetN, TFalse, TBottom, true },
    { TSKeyset, TFalse, TBottom, true },
    { TSKeysetE, TInt, make_specialized_arrmapn(BKeysetN, TInt, TInt), false },
    { TKeysetE, TInitCell, TKeysetN, true },
    { TKeysetE, TInitUnc, make_specialized_arrmapn(BKeysetN, TUncArrKey, TUncArrKey), true },
    { TKeysetE, ival(0), make_specialized_arrpacked(BKeysetN, {ival(0)}), false },
    { TSKeysetE, ival(1), make_specialized_arrmap(BKeysetN, mapElem7), false },
    { TKeysetE, sval(s_A), make_specialized_arrmap(BKeysetN, mapElem8), false },
    { TKeysetE, TCls, make_specialized_arrmapn(BKeysetN, TSStr, TSStr), true },
    { TKeysetE, TLazyCls, make_specialized_arrmapn(BKeysetN, TSStr, TSStr), true },
    { TKeysetN, TInt, TKeysetN, false },
    { TKeyset, TInt, TKeysetN, false },
    { TSKeysetN, TInt, TKeysetN, false },
    { TSKeyset, TInt, TKeysetN, false },
    { TCKeysetN, TInt, TKeysetN, false },
    { TCKeyset, TInt, TKeysetN, false },
    { TKeysetN, TLazyCls, TKeysetN, true },
    { TKeysetN, TCls, TKeysetN, true },

    { make_specialized_arrval(BSVecN, staticVec),
      TInt, make_specialized_arrpacked(BVecN, {sval(s_A), sval(s_B), sval(s_C), TInt}), false },
    { make_specialized_arrval(BSVec, staticVec), TInt, make_specialized_arrpackedn(BVecN, TUncArrKey), false },
    { make_specialized_arrval(BSDictN, staticDict), TInt, make_specialized_arrmap(BDictN, mapElem6), false },
    { make_specialized_arrval(BSDict, staticDict), TInt, make_specialized_arrmapn(BDictN, TUncArrKey, TUncArrKey), false },

    { make_specialized_arrpackedn(BVecishN, TInt), TStr, make_specialized_arrpackedn(BVecishN, TArrKey), false },
    { make_specialized_arrpackedn(BSVecishN, TSStr), TInt, make_specialized_arrpackedn(BVecishN, TUncArrKey), false },
    { make_specialized_arrpackedn(BVecishN, Type{BInitCell & ~BObj}), TObj, TVecishN, false },
    { make_specialized_arrpackedn(BVecish, TInt), TStr, make_specialized_arrpackedn(BVecishN, TArrKey), false },
    { make_specialized_arrpackedn(BSVecish, TSStr), TInt, make_specialized_arrpackedn(BVecishN, TUncArrKey), false },
    { make_specialized_arrpackedn(BVecish, Type{BInitCell & ~BObj}), TObj, TVecishN, false },
    { make_specialized_arrpackedn(BDictishN, TInt), TStr, make_specialized_arrpackedn(BDictishN, TArrKey), false },
    { make_specialized_arrpackedn(BSDictishN, TSStr), TInt, make_specialized_arrpackedn(BDictishN, TUncArrKey), false },
    { make_specialized_arrpackedn(BDictishN, Type{BInitCell & ~BObj}), TObj, make_specialized_arrpackedn(BDictishN, TInitCell), false },
    { make_specialized_arrpackedn(BDictish, TInt), TStr, make_specialized_arrpackedn(BDictishN, TArrKey), false },
    { make_specialized_arrpackedn(BSDictish, TSStr), TInt, make_specialized_arrpackedn(BDictishN, TUncArrKey), false },
    { make_specialized_arrpackedn(BDictish, Type{BInitCell & ~BObj}), TObj, make_specialized_arrpackedn(BDictishN, TInitCell), false },
    { make_specialized_arrpackedn(BKeyset, TInt), TInt, make_specialized_arrmapn(BKeysetN, TInt, TInt), false },
    { make_specialized_arrpackedn(BKeyset, TInt), TStr, TKeysetN, false },
    { make_specialized_arrpackedn(BKeyset, TInt), TSStr, make_specialized_arrmapn(BKeysetN, TUncArrKey, TUncArrKey), false },
    { make_specialized_arrpackedn(BKeyset, TInt), sval(s_A),
      make_specialized_arrmapn(BKeysetN, union_of(TInt,sval(s_A)), union_of(TInt,sval(s_A))), false },
    { make_specialized_arrpackedn(BKeyset, TInt), ival(0), make_specialized_arrpackedn(BKeysetN, TInt), false },
    { make_specialized_arrpackedn(BKeyset, TInt), ival(1), make_specialized_arrmapn(BKeysetN, TInt, TInt), false },
    { make_specialized_arrpackedn(BKeysetN, TInt), ival(1), make_specialized_arrpackedn(BKeysetN, TInt), false },

    { make_specialized_arrpacked(BVecishN, {TObj}), TStr, make_specialized_arrpacked(BVecishN, {TObj, TStr}), false },
    { make_specialized_arrpacked(BSVecishN, {TInt}), TStr, make_specialized_arrpacked(BVecishN, {TInt, TStr}), false },
    { make_specialized_arrpacked(BVecish, {TObj}), TStr, make_specialized_arrpackedn(BVecishN, Type{BStr|BObj}), false },
    { make_specialized_arrpacked(BSVecish, {TInt}), TStr, make_specialized_arrpackedn(BVecishN, TArrKey), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0)}), TStr, TKeysetN, false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), TStr, TKeysetN, false },
    { make_specialized_arrpacked(BKeyset, {ival(0)}), sval(s_A),
      make_specialized_arrmapn(BKeysetN, union_of(TInt,sval(s_A)), union_of(TInt,sval(s_A))), false },
    { make_specialized_arrpacked(BKeyset, {ival(0), ival(1)}), sval(s_A),
      make_specialized_arrmapn(BKeysetN, union_of(TInt,sval(s_A)), union_of(TInt,sval(s_A))), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0)}), sval(s_A), make_specialized_arrmap(BKeysetN, mapElem9), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), sval(s_A), make_specialized_arrmap(BKeysetN, mapElem10), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0)}), sval_nonstatic(s_A), make_specialized_arrmap(BKeysetN, mapElem11), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), sval_nonstatic(s_A), make_specialized_arrmap(BKeysetN, mapElem12), false },
    { make_specialized_arrpacked(BKeyset, {ival(0)}), ival(-1), make_specialized_arrmapn(BKeysetN, TInt, TInt), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0)}), ival(-1), make_specialized_arrmap(BKeysetN, mapElem13), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), ival(0), make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), ival(1), make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), ival(2), make_specialized_arrpacked(BKeysetN, {ival(0),ival(1),ival(2)}), false },
    { make_specialized_arrpacked(BKeyset, {ival(0),ival(1)}), ival(3), make_specialized_arrmapn(BKeysetN, TInt, TInt), false },
    { make_specialized_arrpacked(BKeysetN, {ival(0),ival(1)}), ival(3), make_specialized_arrmap(BKeysetN, mapElem14), false },
    { make_specialized_arrpacked(BKeyset, {ival(0)}), ival(0), make_specialized_arrpacked(BKeysetN, {ival(0)}), false },
    { make_specialized_arrpacked(BKeyset, {ival(0),ival(1)}), ival(0), make_specialized_arrpackedn(BKeysetN, TInt), false },
    { make_specialized_arrpacked(BKeyset, {ival(0)}), ival(1), make_specialized_arrmapn(BKeysetN, TInt, TInt), false },

    { make_specialized_arrmapn(BDictishN, TInt, TObj), TStr, make_specialized_arrmapn(BDictishN, TInt, Type{BObj|BStr}), true },
    { make_specialized_arrmapn(BSDictishN, TSStr, TSStr), TInt, make_specialized_arrmapn(BDictishN, TUncArrKey, TUncArrKey), true },
    { make_specialized_arrmapn(BDictish, TArrKey, TStr), Type{BInitCell & ~BStr}, TDictishN, true },
    { make_specialized_arrmapn(BDictish, TStr, TInitCell), TInitCell, TDictishN, true },
    { make_specialized_arrmapn(BKeysetN, TInt, TInt), TStr, TKeysetN, false },
    { make_specialized_arrmapn(BKeyset, TInt, TInt), TStr, TKeysetN, false },
    { make_specialized_arrmapn(BKeysetN, TCStr, TCStr), TSStr, make_specialized_arrmapn(BKeysetN, TStr, TStr), false },
    { make_specialized_arrmapn(BKeyset, TCStr, TCStr), TSStr, make_specialized_arrmapn(BKeysetN, TStr, TStr), false },

    { make_specialized_arrmap(BDictishN, mapElem1), TFalse, make_specialized_arrmap(BDictishN, mapElem4), false },
    { make_specialized_arrmap(BDictishN, mapElem2), TInitCell, make_specialized_arrmap(BDictishN, mapElem5), false },
    { make_specialized_arrmap(BDictishN, mapElem3), TFalse, make_specialized_arrmap(BDictishN, mapElem3), true },
    { make_specialized_arrmap(BDictishN, mapElem1, TStr, TInt),
      TFalse, make_specialized_arrmap(BDictishN, mapElem1, TArrKey, Type{BInt|BFalse}), true },
    { make_specialized_arrmap(BDictishN, mapElem1, ival(10), TInt),
      TFalse, make_specialized_arrmap(BDictishN, mapElem1, TInt, Type{BInt|BFalse}), false },
    { make_specialized_arrmap(BDictishN, mapElem1, ival(std::numeric_limits<int64_t>::max()), TInt),
      TFalse, make_specialized_arrmap(BDictishN, mapElem1, TInt, Type{BInt|BFalse}), true },
    { make_specialized_arrmap(BDictish, mapElem1), TFalse,
      make_specialized_arrmapn(BDictishN, TUncArrKey, Type{BInt|BObj|BFalse}), false },
    { make_specialized_arrmap(BDictish, mapElem3), TFalse,
      make_specialized_arrmapn(BDictishN, TInt, TInitCell), true },
    { make_specialized_arrmap(BKeysetN, mapElem10), TInt, make_specialized_arrmap(BKeysetN, mapElem10, TInt, TInt), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), TSStr, make_specialized_arrmap(BKeysetN, mapElem10, TSStr, TSStr), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, TInt, TInt), TStr, make_specialized_arrmap(BKeysetN, mapElem10, TArrKey, TArrKey), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), ival(1), make_specialized_arrmap(BKeysetN, mapElem10), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), sval(s_A), make_specialized_arrmap(BKeysetN, mapElem10), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), sval_nonstatic(s_A), make_specialized_arrmap(BKeysetN, mapElem10), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), ival(100), make_specialized_arrmap(BKeysetN, mapElem15), false },
    { make_specialized_arrmap(BKeysetN, mapElem10), sval_nonstatic(s_B), make_specialized_arrmap(BKeysetN, mapElem16), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, TStr, TStr), ival(100),
      make_specialized_arrmap(BKeysetN, mapElem10, union_of(ival(100),TStr), union_of(ival(100),TStr)), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, TInt, TInt), sval(s_B),
      make_specialized_arrmap(BKeysetN, mapElem10, union_of(TInt,sval(s_B)), union_of(TInt,sval(s_B))), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, ival(100), ival(100)), TStr,
      make_specialized_arrmap(BKeysetN, mapElem10, union_of(ival(100),TStr), union_of(ival(100),TStr)), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, ival(100), ival(100)), ival(100), make_specialized_arrmap(BKeysetN, mapElem15), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, sval(s_B), sval(s_B)), sval(s_B), make_specialized_arrmap(BKeysetN, mapElem17), false },
    { make_specialized_arrmap(BKeysetN, mapElem10, sval(s_B), sval(s_B)), sval_nonstatic(s_B), make_specialized_arrmap(BKeysetN, mapElem16), false },
    { make_specialized_arrmap(BKeyset, mapElem10), TInt, make_specialized_arrmapn(BKeysetN, union_of(sval(s_A),TInt), union_of(sval(s_A),TInt)), false },
    { make_specialized_arrmap(BKeyset, mapElem16), TStr, TKeysetN, false },
    { make_specialized_arrmap(BKeyset, mapElem7), TStr, TKeysetN, false },
    { make_specialized_arrmap(BKeyset, mapElem10), ival(1), make_specialized_arrmapn(BKeysetN, union_of(sval(s_A),TInt), union_of(sval(s_A),TInt)), false },
    { make_specialized_arrmap(BKeyset, mapElem10), ival(0), make_specialized_arrmapn(BKeysetN, union_of(sval(s_A),TInt), union_of(sval(s_A),TInt)), false },
    { make_specialized_arrmap(BKeyset, mapElem7), ival(1), make_specialized_arrmap(BKeysetN, mapElem7), false },
    { make_specialized_arrmap(BKeyset, mapElem18), ival(1), make_specialized_arrmap(BKeysetN, mapElem7, sval(s_A), sval(s_A)), false },
    { make_specialized_arrmap(BKeyset, mapElem8), sval_nonstatic(s_A), make_specialized_arrmap(BKeysetN, mapElem19), false },
    { make_specialized_arrmap(BKeysetN, mapElem7), union_of(ival(1),TStr), make_specialized_arrmap(BKeysetN, mapElem7, TArrKey, TArrKey), false },
  };

  auto old = RO::EvalRaiseClassConversionWarning;
  RO::EvalRaiseClassConversionWarning = true;
  SCOPE_EXIT { RO::EvalRaiseClassConversionWarning = old; };

  for (auto const& t : tests) {
    auto const elem = array_like_newelem(std::get<0>(t), std::get<1>(t));
    EXPECT_EQ(loosen_mark_for_testing(elem.first), std::get<2>(t));
    EXPECT_EQ(elem.second, std::get<3>(t));
  }
}

TEST_F(TypeTest, ArrayLikeSetElem) {
  auto const program = make_test_program();
  Index index { program.get() };

  const std::vector<Type> keys{
    TInt,
    TStr,
    TSStr,
    TCStr,
    TArrKey,
    TUncArrKey,
    sval(s_A),
    sval(s_B),
    sval(s_C),
    sval_nonstatic(s_A),
    sval_counted(s_A),
    ival(0),
    ival(1),
    ival(123),
    ival(777),
    ival(-1),
    ival(std::numeric_limits<int64_t>::max()),
    union_of(sval(s_A),TInt),
    union_of(sval(s_B),TInt),
    union_of(sval(s_C),TInt),
    union_of(sval_counted(s_A),TInt),
    union_of(ival(0),TStr),
    union_of(ival(1),TStr),
    union_of(ival(123),TStr),
    union_of(ival(777),TStr),
    union_of(ival(-1),TStr),
    union_of(ival(std::numeric_limits<int64_t>::max()),TStr)
  };

  const std::vector<Type> values{
    TInt,
    TStr,
    TSStr,
    TObj,
    TInitUnc,
    TInitCell
  };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.subtypeOf(BCell) || !t.couldBe(BArrLike)) continue;

    for (auto const& k : keys) {
      for (auto const& v : values) {
        auto const set = array_like_set(t, k, v);
        EXPECT_FALSE(set.first.couldBe(BArrLikeE));

        if (!set.first.couldBe(BArrLike)) {
          EXPECT_TRUE(set.second);
        } else {
          EXPECT_FALSE(set.first.subtypeAmong(BSArrLike, BArrLike));
        }

        if (t.subtypeAmong(BKeyset, BArrLike)) {
          EXPECT_FALSE(set.first.couldBe(BArrLike));
        }
        if (t.couldBe(BKeyset)) {
          EXPECT_TRUE(set.second);
        }
        if (t.subtypeAmong(BDictish, BArrLike)) {
          EXPECT_FALSE(set.second);
        }
        if (t.subtypeAmong(BVecish, BArrLike)) {
          if (!k.couldBe(BInt) || !t.couldBe(BVecishN)) {
            EXPECT_FALSE(set.first.couldBe(BArrLike));
          }
        }

        if (set.first.couldBe(BArrLike)) {
          EXPECT_TRUE(v.subtypeOf(array_like_elem(set.first, k).first));
        }

        auto [arr, rest] = split_array_like(t);
        auto const set2 = array_like_set(arr, k, v);
        EXPECT_EQ(set.first, union_of(set2.first, rest));
        EXPECT_EQ(set.second, set2.second);
      }
    }
  }

  auto const mapElem1 = MapElems{
    map_elem(s_A, TObj)
  };
  auto const mapElem2 = MapElems{
    map_elem_nonstatic(s_A, TObj)
  };
  auto const mapElem3 = MapElems{
    map_elem(int64_t(0), TInt),
    map_elem(1, TObj),
    map_elem(s_A, TFalse)
  };
  auto const mapElem4 = MapElems{
    map_elem(int64_t(0), TInt),
    map_elem(-1, TStr)
  };
  auto const mapElem5 = MapElems{
    map_elem(int64_t(0), TInt),
    map_elem(2, TStr)
  };
  auto const mapElem6 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TFalse)
  };
  auto const mapElem7 = MapElems{
    map_elem_counted(s_A, TInt),
    map_elem_counted(s_B, TObj),
    map_elem(100, TFalse)
  };
  auto const mapElem8 = MapElems{
    map_elem(s_A, Type{BInt|BTrue}),
    map_elem(s_B, Type{BObj|BTrue}),
    map_elem(100, TFalse)
  };
  auto const mapElem9 = MapElems{
    map_elem_counted(s_A, Type{BInt|BTrue}),
    map_elem_counted(s_B, Type{BObj|BTrue}),
    map_elem(100, TFalse)
  };
  auto const mapElem10 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TBool)
  };
  auto const mapElem11 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TFalse),
    map_elem(100, TFalse)
  };
  auto const mapElem12 = MapElems{
    map_elem_counted(s_A, TInt),
    map_elem_counted(s_B, TFalse),
    map_elem(100, TFalse)
  };
  auto const mapElem13 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TFalse),
    map_elem(s_BA, TInt)
  };
  auto const mapElem14 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TFalse),
    map_elem_counted(s_BA, TInt)
  };
  auto const mapElem15 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TFalse),
    map_elem(s_BA, TTrue)
  };
  auto const mapElem16 = MapElems{
    map_elem(s_A, TInt),
    map_elem(s_B, TObj),
    map_elem(100, TFalse),
    map_elem_nonstatic(s_BA, TTrue)
  };
  auto const mapElem17 = MapElems{
    map_elem(s_A, TStr)
  };
  auto const mapElem18 = MapElems{
    map_elem_nonstatic(s_A, TStr)
  };
  auto const mapElem19 = MapElems{
    map_elem(s_A, union_of(ival(100),TFalse)),
    map_elem(s_B, union_of(ival(200),TFalse))
  };
  auto const mapElem20 = MapElems{
    map_elem(s_A, ival(100)),
    map_elem(s_B, ival(300))
  };
  auto const mapElem21 = MapElems{
    map_elem(1, TSStr)
  };
  auto const mapElem22 = MapElems{
    map_elem(1, TUncArrKey)
  };

  auto const staticVec1 = static_vec(s_A, s_B, 100);
  auto const staticDict1 = static_dict(s_A, 100, s_B, 200);

  const std::vector<std::tuple<Type, Type, Type, Type, bool>> tests{
    { TVecishN, TStr, TInitCell, TBottom, true },
    { TVecishE, TStr, TInitCell, TBottom, true },
    { TVecish, TStr, TInitCell, TBottom, true },
    { TVecishE, TInt, TInitCell, TBottom, true },
    { TVecishN, ival(-1), TInitCell, TBottom, true },
    { TSVecish, TInt, TInitCell, TVecishN, true },
    { TSVecishN, TInt, TInitCell, TVecishN, true },
    { TVecish, TInt, TInitCell, TVecishN, true },
    { TVecishN, TInt, TInitCell, TVecishN, true },
    { TVecishN, ival(0), TInitCell, TVecishN, true },
    { TDictishE, TInt, TStr, make_specialized_arrmapn(BDictishN, TInt, TStr), false },
    { TSDictishE, TInt, TStr, make_specialized_arrmapn(BDictishN, TInt, TStr), false },
    { TCDictishE, TInt, TStr, make_specialized_arrmapn(BDictishN, TInt, TStr), false },
    { TDictishE, TArrKey, TInitCell, TDictishN, false },
    { TDictishE, ival(0), TObj, make_specialized_arrpacked(BDictishN, {TObj}), false },
    { TDictishE, sval(s_A), TObj, make_specialized_arrmap(BDictishN, mapElem1), false },
    { TDictishE, sval_nonstatic(s_A), TObj, make_specialized_arrmap(BDictishN, mapElem2), false },
    { TDictishN, TInt, TStr, TDictishN, false },
    { TDictish, TInt, TStr, TDictishN, false },
    { TSDictish, TInt, TStr, TDictishN, false },
    { TSDictishN, TInt, TStr, TDictishN, false },
    { TKeysetN, TArrKey, TArrKey, TBottom, true },
    { TKeysetE, TArrKey, TArrKey, TBottom, true },
    { TKeyset, TArrKey, TArrKey, TBottom, true },
    { TSKeysetN, TArrKey, TArrKey, TBottom, true },
    { TSKeysetE, TArrKey, TArrKey, TBottom, true },
    { TSKeyset, TArrKey, TArrKey, TBottom, true },

    { make_specialized_arrval(BSVecN, staticVec1), TInt, TFalse,
      make_specialized_arrpacked(BVecN, {union_of(sval(s_A),TFalse),union_of(sval(s_B),TFalse),union_of(ival(100),TFalse)}), true },
    { make_specialized_arrval(BSVecN, staticVec1), ival(1), TFalse,
      make_specialized_arrpacked(BVecN, {sval(s_A),TFalse,ival(100)}), false },
    { make_specialized_arrval(BSDictN, staticDict1), TStr, TFalse, make_specialized_arrmap(BDictN, mapElem19, TStr, TFalse), false },
    { make_specialized_arrval(BSDictN, staticDict1), sval(s_B), ival(300), make_specialized_arrmap(BDictN, mapElem20), false },

    { make_specialized_arrpackedn(BVecishN, TObj), TInt, TStr, make_specialized_arrpackedn(BVecishN, Type{BObj|BStr}), true },
    { make_specialized_arrpackedn(BVecishN, Type{BInitCell & ~BObj}), TInt, TObj, TVecishN, true },
    { make_specialized_arrpackedn(BVecishN, TObj), ival(-1), TStr, TBottom, true },
    { make_specialized_arrpackedn(BVecishN, TObj), ival(0), TStr, make_specialized_arrpackedn(BVecishN, Type{BObj|BStr}), false },
    { make_specialized_arrpackedn(BVecish, TObj), ival(0), TStr, make_specialized_arrpackedn(BVecishN, Type{BObj|BStr}), true },
    { make_specialized_arrpackedn(BVecishN, TObj), ival(1), TStr, make_specialized_arrpackedn(BVecishN, Type{BObj|BStr}), true },
    { make_specialized_arrpackedn(BVecishN, TObj), union_of(ival(0),TStr), TStr, make_specialized_arrpackedn(BVecishN, Type{BObj|BStr}), true },
    { make_specialized_arrpackedn(BDictishN, TObj), TInt, TStr, make_specialized_arrmapn(BDictishN, TInt, Type{BObj|BStr}), false },
    { make_specialized_arrpackedn(BDictishN, Type{BInitCell & ~BObj}), TArrKey, TObj, TDictishN, false },
    { make_specialized_arrpackedn(BDictishN, TInitCell), TArrKey, TInitCell, TDictishN, false },
    { make_specialized_arrpackedn(BDictishN, TObj), sval(s_A), TObj, make_specialized_arrmapn(BDictishN, union_of(TInt,sval(s_A)), TObj), false },
    { make_specialized_arrpackedn(BDictishN, TObj), ival(0), TStr, make_specialized_arrpackedn(BDictishN, Type{BObj|BStr}), false },
    { make_specialized_arrpackedn(BDictishN, TObj), ival(1), TStr, make_specialized_arrpackedn(BDictishN, Type{BObj|BStr}), false },
    { make_specialized_arrpackedn(BDictish, TObj), ival(1), TStr, make_specialized_arrmapn(BDictishN, TInt, Type{BObj|BStr}), false },

    { make_specialized_arrpacked(BVecishN, {TStr}), TInt, TInt, make_specialized_arrpacked(BVecishN, {TArrKey}), true },
    { make_specialized_arrpacked(BVecishN, {TStr, TObj}), TInt, TInt, make_specialized_arrpacked(BVecishN, {TArrKey, Type{BObj|BInt}}), true },
    { make_specialized_arrpacked(BVecishN, {TStr}), ival(-1), TInt, TBottom, true },
    { make_specialized_arrpacked(BVecishN, {TStr}), ival(1), TInt, TBottom, true },
    { make_specialized_arrpacked(BVecishN, {TStr, TObj}), ival(1), TInt, make_specialized_arrpacked(BVecishN, {TStr, TInt}), false },
    { make_specialized_arrpacked(BVecish, {TStr, TObj}), ival(1), TInt, make_specialized_arrpacked(BVecishN, {TStr, TInt}), true },
    { make_specialized_arrpacked(BVecishN, {TStr, TObj}), union_of(ival(1),TStr), TInt, make_specialized_arrpacked(BVecishN, {TArrKey, Type{BInt|BObj}}), true },
    { make_specialized_arrpacked(BDictishN, {TObj}), TInt, TStr, make_specialized_arrmapn(BDictishN, TInt, Type{BObj|BStr}), false },
    { make_specialized_arrpacked(BDictishN, {TObj}), TStr, TObj, make_specialized_arrmapn(BDictishN, union_of(ival(0),TStr), TObj), false },
    { make_specialized_arrpacked(BDictishN, {TInitCell}), TStr, TInitCell, TDictishN, false },
    { make_specialized_arrpacked(BDictishN, {TInitCell, TInitCell}), TStr, TInitCell, TDictishN, false },
    { make_specialized_arrpacked(BDictishN, {TInt, TObj}), sval(s_A), TFalse, make_specialized_arrmap(BDictishN, mapElem3), false },
    { make_specialized_arrpacked(BDictish, {TInt, TObj}), sval(s_A), TFalse, make_specialized_arrmapn(BDictishN, union_of(TInt,sval(s_A)), Type{BInt|BObj|BFalse}), false },
    { make_specialized_arrpacked(BDictishN, {TInt}), ival(-1), TStr, make_specialized_arrmap(BDictishN, mapElem4), false },
    { make_specialized_arrpacked(BDictishN, {TInt}), ival(2), TStr, make_specialized_arrmap(BDictishN, mapElem5), false },
    { make_specialized_arrpacked(BDictish, {TInt}), ival(-1), TStr, make_specialized_arrmapn(BDictishN, TInt, TArrKey), false },
    { make_specialized_arrpacked(BDictish, {TInt}), ival(2), TStr, make_specialized_arrmapn(BDictishN, TInt, TArrKey), false },
    { make_specialized_arrpacked(BDictishN, {TInt}), ival(0), TStr, make_specialized_arrpacked(BDictishN, {TStr}), false },
    { make_specialized_arrpacked(BDictishN, {TInt}), ival(1), TStr, make_specialized_arrpacked(BDictishN, {TInt, TStr}), false },

    { make_specialized_arrmapn(BDictishN, TStr, TInt), TStr, TStr, make_specialized_arrmapn(BDictishN, TStr, TArrKey), false },
    { make_specialized_arrmapn(BDictishN, TStr, TInt), TInt, TInt, make_specialized_arrmapn(BDictishN, TArrKey, TInt), false },
    { make_specialized_arrmapn(BDictishN, TStr, Type{BInitCell & ~BObj}), TInt, TObj, TDictishN, false },
    { make_specialized_arrmapn(BDictish, TStr, TInt), TStr, TStr, make_specialized_arrmapn(BDictishN, TStr, TArrKey), false },
    { make_specialized_arrmapn(BDictish, TStr, TInt), TInt, TInt, make_specialized_arrmapn(BDictishN, TArrKey, TInt), false },
    { make_specialized_arrmapn(BDictish, TStr, Type{BInitCell & ~BObj}), TInt, TObj, TDictishN, false },

    { make_specialized_arrmap(BDictishN, mapElem6), TStr, TTrue, make_specialized_arrmap(BDictishN, mapElem8, TStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem6), TCStr, TTrue, make_specialized_arrmap(BDictishN, mapElem8, TCStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem6), TSStr, TTrue, make_specialized_arrmap(BDictishN, mapElem8, TSStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem7), TStr, TTrue, make_specialized_arrmap(BDictishN, mapElem9, TStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem7), TCStr, TTrue, make_specialized_arrmap(BDictishN, mapElem9, TCStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem7), TSStr, TTrue, make_specialized_arrmap(BDictishN, mapElem9, TSStr, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem6), TInt, TTrue, make_specialized_arrmap(BDictishN, mapElem10, TInt, TTrue), false },
    { make_specialized_arrmap(BDictishN, mapElem6), sval(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem11), false },
    { make_specialized_arrmap(BDictishN, mapElem6), sval_nonstatic(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem11), false },
    { make_specialized_arrmap(BDictishN, mapElem6), sval_counted(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem11), false },
    { make_specialized_arrmap(BDictishN, mapElem7), sval(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem12), false },
    { make_specialized_arrmap(BDictishN, mapElem7), sval_nonstatic(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem12), false },
    { make_specialized_arrmap(BDictishN, mapElem7), sval_counted(s_B), TFalse, make_specialized_arrmap(BDictishN, mapElem12), false },
    { make_specialized_arrmap(BDictishN, mapElem6), sval(s_BA), TInt, make_specialized_arrmap(BDictishN, mapElem13), false },
    { make_specialized_arrmap(BDictishN, mapElem6), sval_counted(s_BA), TInt, make_specialized_arrmap(BDictishN, mapElem14), false },
    { make_specialized_arrmap(BDictishN, mapElem6, TInt, TTrue), sval(s_BA), TFalse,
      make_specialized_arrmap(BDictishN, mapElem6, union_of(TInt,sval(s_BA)), TBool), false },
    { make_specialized_arrmap(BDictishN, mapElem6, sval(s_BA), TFalse), sval(s_BA), TTrue,
      make_specialized_arrmap(BDictishN, mapElem15), false },
    { make_specialized_arrmap(BDictishN, mapElem6, sval_counted(s_BA), TFalse), sval(s_BA), TTrue,
      make_specialized_arrmap(BDictishN, mapElem16), false },
    { make_specialized_arrmap(BDictishN, mapElem6, sval(s_BA), TFalse), sval_counted(s_BA), TTrue,
      make_specialized_arrmap(BDictishN, mapElem16), false },
    { make_specialized_arrmap(BDictish, mapElem6), TStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem6), TSStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TSStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem6), TCStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem7), TStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem7), TSStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem7), TCStr, TTrue,
      make_specialized_arrmapn(BDictishN, union_of(ival(100),TCStr), Type{BInt|BObj|BBool}), false },
    { make_specialized_arrmap(BDictish, mapElem6), sval(s_A), TStr,
      make_specialized_arrmap(BDictishN, mapElem17, union_of(TInt,sval(s_B)), Type{BObj|BFalse}), false },
    { make_specialized_arrmap(BDictish, mapElem6), sval_counted(s_A), TStr,
      make_specialized_arrmap(BDictishN, mapElem18, union_of(TInt,sval(s_B)), Type{BObj|BFalse}), false },
    { make_specialized_arrmap(BDictish, mapElem6, TInt, TTrue), sval(s_A), TStr,
      make_specialized_arrmap(BDictishN, mapElem17, union_of(TInt,sval(s_B)), Type{BObj|BBool}), false },
    { make_specialized_arrmap(BDictishN, mapElem21), union_of(ival(1),TStr), TInt,
      make_specialized_arrmap(BDictishN, mapElem22, TArrKey, TInt), false },
  };
  for (auto const& t : tests) {
    auto const elem = array_like_set(std::get<0>(t), std::get<1>(t), std::get<2>(t));
    EXPECT_EQ(loosen_mark_for_testing(elem.first), std::get<3>(t));
    EXPECT_EQ(elem.second, std::get<4>(t));
  }
}

TEST_F(TypeTest, SpecificExamples) {
  // Random examples to stress option types, values, etc:

  EXPECT_TRUE(!TInt.subtypeOf(ival(1)));

  EXPECT_TRUE(TInitCell.couldBe(ival(1)));
  EXPECT_TRUE(ival(2).subtypeOf(BInt));
  EXPECT_TRUE(!ival(2).subtypeOf(BBool));
  EXPECT_TRUE(ival(3).subtypeOf(BInt));
  EXPECT_TRUE(TInt.subtypeOf(BInt));
  EXPECT_TRUE(!TBool.subtypeOf(BInt));
  EXPECT_TRUE(TInitNull.subtypeOf(BOptInt));
  EXPECT_TRUE(!TNull.subtypeOf(BOptInt));
  EXPECT_TRUE(TNull.couldBe(BOptInt));
  EXPECT_TRUE(TNull.couldBe(BOptBool));

  EXPECT_TRUE(TInitNull.subtypeOf(BInitCell));
  EXPECT_TRUE(TInitNull.subtypeOf(BCell));
  EXPECT_TRUE(!TUninit.subtypeOf(BInitNull));

  EXPECT_TRUE(ival(3).subtypeOf(BInt));
  EXPECT_TRUE(ival(3).subtypeOf(opt(ival(3))));
  EXPECT_TRUE(ival(3).couldBe(opt(ival(3))));
  EXPECT_TRUE(ival(3).couldBe(BInt));
  EXPECT_TRUE(TInitNull.couldBe(opt(ival(3))));
  EXPECT_TRUE(TNull.couldBe(opt(ival(3))));
  EXPECT_TRUE(TInitNull.subtypeOf(opt(ival(3))));
  EXPECT_TRUE(!TNull.subtypeOf(opt(ival(3))));

  EXPECT_EQ(intersection_of(TClsMeth, TInitUnc),
            use_lowptr ? TClsMeth : TBottom);

  auto const test_map_a = MapElems{map_elem(s_A, TDbl), map_elem(s_B, TBool)};
  auto const test_map_b = MapElems{map_elem(s_A, TObj), map_elem(s_B, TRes)};

  auto const disjointArrSpecs = std::vector<Type>{
    dict_packedn(TInt),
    dict_packedn(TStr),
    dict_packed({TDbl}),
    dict_packed({TBool}),
    dict_n(TStr, TStr),
    dict_n(TStr, TInt),
    dict_map(test_map_a),
    dict_map(test_map_b)
  };
  for (auto const& t1 : disjointArrSpecs) {
    for (auto const& t2 : disjointArrSpecs) {
      if (t1 == t2) continue;
      EXPECT_FALSE(t1.couldBe(t2));
      EXPECT_FALSE(t2.couldBe(t1));

      auto const t3 = union_of(t1, TDictE);
      auto const t4 = union_of(t2, TDictE);
      EXPECT_TRUE(t3.couldBe(t4));
      EXPECT_TRUE(t4.couldBe(t3));
      EXPECT_FALSE(t3.subtypeOf(t4));
      EXPECT_FALSE(t4.subtypeOf(t3));
      EXPECT_EQ(intersection_of(t3, t4), TDictE);
      EXPECT_EQ(intersection_of(t4, t3), TDictE);

      auto const t5 = opt(t1);
      auto const t6 = opt(t2);
      EXPECT_TRUE(t5.couldBe(t6));
      EXPECT_TRUE(t6.couldBe(t5));
      EXPECT_FALSE(t5.subtypeOf(t6));
      EXPECT_FALSE(t6.subtypeOf(t5));
      EXPECT_EQ(intersection_of(t5, t6), TInitNull);
      EXPECT_EQ(intersection_of(t6, t5), TInitNull);

      auto const t7 = opt(t3);
      auto const t8 = opt(t4);
      EXPECT_TRUE(t7.couldBe(t8));
      EXPECT_TRUE(t8.couldBe(t7));
      EXPECT_FALSE(t7.subtypeOf(t8));
      EXPECT_FALSE(t8.subtypeOf(t7));
      EXPECT_EQ(intersection_of(t7, t8), opt(TDictE));
      EXPECT_EQ(intersection_of(t8, t7), opt(TDictE));
    }
  }
}

TEST_F(TypeTest, IndexBased) {
  auto const program = make_test_program();
  auto const unit = program->units.back().get();
  auto const func = [&]() -> php::Func* {
    for (auto& f : unit->funcs) {
      if (f->name->isame(s_test.get())) return f.get();
    }
    return nullptr;
  }();
  EXPECT_TRUE(func != nullptr);

  auto const ctx = Context { unit, func };
  Index idx{program.get()};

  auto const cls = idx.resolve_class(ctx, s_TestClass.get());
  if (!cls) ADD_FAILURE();
  auto const clsBase = idx.resolve_class(ctx, s_Base.get());
  if (!clsBase) ADD_FAILURE();

  // Need to use base because final records are always exact.
  auto const rec = idx.resolve_record(s_UniqueRecBase.get());
  if (!rec) ADD_FAILURE();

  auto const objExactTy = objExact(*cls);
  auto const subObjTy   = subObj(*cls);
  auto const clsExactTy = clsExact(*cls);
  auto const subClsTy   = subCls(*cls);
  auto const objExactBaseTy = objExact(*clsBase);
  auto const subObjBaseTy   = subObj(*clsBase);

  auto const exactRecTy = exactRecord(*rec);
  auto const subRecTy   = subRecord(*rec);

  // Basic relationship between the class types and object types.
  EXPECT_EQ(objcls(objExactTy), clsExactTy);
  EXPECT_EQ(objcls(subObjTy), subClsTy);

  // =TestClass <: <=TestClass, and not vice versa. Same for records.
  EXPECT_TRUE(objExactTy.subtypeOf(subObjTy));
  EXPECT_TRUE(!subObjTy.subtypeOf(objExactTy));
  EXPECT_TRUE(exactRecTy.subtypeOf(subRecTy));
  EXPECT_TRUE(!subRecTy.subtypeOf(exactRecTy));
  // =TestClass <: <=TestClass, and not vice versa.
  EXPECT_TRUE(clsExactTy.subtypeOf(subClsTy));
  EXPECT_TRUE(!subClsTy.subtypeOf(clsExactTy));

  // =TestClass couldBe <= TestClass, and vice versa. Same for records.
  EXPECT_TRUE(objExactTy.couldBe(subObjTy));
  EXPECT_TRUE(subObjTy.couldBe(objExactTy));
  EXPECT_TRUE(exactRecTy.couldBe(subRecTy));
  EXPECT_TRUE(subRecTy.couldBe(exactRecTy));
  EXPECT_TRUE(clsExactTy.couldBe(subClsTy));
  EXPECT_TRUE(subClsTy.couldBe(clsExactTy));

  // Foo= and Foo<= are both subtypes of Foo, and couldBe Foo.
  EXPECT_TRUE(objExactTy.subtypeOf(BObj));
  EXPECT_TRUE(subObjTy.subtypeOf(BObj));
  EXPECT_TRUE(objExactTy.couldBe(BObj));
  EXPECT_TRUE(subObjTy.couldBe(BObj));
  EXPECT_TRUE(TObj.couldBe(objExactTy));
  EXPECT_TRUE(TObj.couldBe(subObjTy));
  EXPECT_TRUE(clsExactTy.subtypeOf(BCls));
  EXPECT_TRUE(subClsTy.subtypeOf(BCls));
  EXPECT_TRUE(clsExactTy.couldBe(BCls));
  EXPECT_TRUE(subClsTy.couldBe(BCls));
  EXPECT_TRUE(TCls.couldBe(clsExactTy));
  EXPECT_TRUE(TCls.couldBe(subClsTy));
  EXPECT_TRUE(exactRecTy.subtypeOf(BRecord));
  EXPECT_TRUE(subRecTy.subtypeOf(BRecord));
  EXPECT_TRUE(exactRecTy.couldBe(BRecord));
  EXPECT_TRUE(subRecTy.couldBe(BRecord));
  EXPECT_TRUE(TRecord.couldBe(exactRecTy));
  EXPECT_TRUE(TRecord.couldBe(subRecTy));

  // These checks are relevant for class to key conversions
  EXPECT_TRUE(clsExactTy.subtypeOf(BOptCls | BOptLazyCls));
  EXPECT_TRUE(subClsTy.subtypeOf(BOptCls | BOptLazyCls));
  EXPECT_TRUE(TCls.subtypeOf(BOptCls | BOptLazyCls));
  EXPECT_TRUE(TLazyCls.subtypeOf(BOptCls | BOptLazyCls));
  EXPECT_TRUE(clsExactTy.couldBe(BOptCls | BOptLazyCls));
  EXPECT_TRUE(subClsTy.couldBe(BOptCls | BOptLazyCls));
  auto keyTy1 = union_of(clsExactTy, sval(s_TestClass));
  EXPECT_TRUE(keyTy1.couldBe(BOptCls | BOptLazyCls));
  auto keyTy2 = union_of(TLazyCls, sval(s_TestClass));
  EXPECT_TRUE(keyTy2.couldBe(BOptCls | BOptLazyCls));
  EXPECT_FALSE(TSStr.couldBe(BOptCls | BOptLazyCls));
  EXPECT_FALSE(TStr.couldBe(BOptCls | BOptLazyCls));


  // Obj= and Obj<= both couldBe ?Obj, and vice versa.
  EXPECT_TRUE(objExactTy.couldBe(BOptObj));
  EXPECT_TRUE(subObjTy.couldBe(BOptObj));
  EXPECT_TRUE(TOptObj.couldBe(objExactTy));
  EXPECT_TRUE(TOptObj.couldBe(subObjTy));
  EXPECT_TRUE(exactRecTy.couldBe(BOptRecord));
  EXPECT_TRUE(subRecTy.couldBe(BOptRecord));
  EXPECT_TRUE(TOptRecord.couldBe(exactRecTy));
  EXPECT_TRUE(TOptRecord.couldBe(subRecTy));

  // Obj= and Obj<= are subtypes of ?Obj.
  EXPECT_TRUE(objExactTy.subtypeOf(BOptObj));
  EXPECT_TRUE(subObjTy.subtypeOf(BOptObj));
  EXPECT_TRUE(exactRecTy.subtypeOf(BOptRecord));
  EXPECT_TRUE(subRecTy.subtypeOf(BOptRecord));

  // Obj= is a subtype of ?Obj=, and also ?Obj<=.
  EXPECT_TRUE(objExactTy.subtypeOf(opt(objExactTy)));
  EXPECT_TRUE(objExactTy.subtypeOf(opt(subObjTy)));
  EXPECT_TRUE(!opt(objExactTy).subtypeOf(objExactTy));
  EXPECT_TRUE(!opt(subObjTy).subtypeOf(objExactTy));
  EXPECT_TRUE(exactRecTy.subtypeOf(opt(exactRecTy)));
  EXPECT_TRUE(exactRecTy.subtypeOf(opt(subRecTy)));
  EXPECT_TRUE(!opt(exactRecTy).subtypeOf(exactRecTy));
  EXPECT_TRUE(!opt(subRecTy).subtypeOf(exactRecTy));

  // Obj= couldBe ?Obj= and ?Obj<=, and vice versa.
  EXPECT_TRUE(objExactTy.couldBe(opt(objExactTy)));
  EXPECT_TRUE(opt(objExactTy).couldBe(objExactTy));
  EXPECT_TRUE(objExactTy.couldBe(opt(subObjTy)));
  EXPECT_TRUE(opt(subObjTy).couldBe(objExactTy));
  EXPECT_TRUE(exactRecTy.couldBe(opt(exactRecTy)));
  EXPECT_TRUE(opt(exactRecTy).couldBe(exactRecTy));
  EXPECT_TRUE(exactRecTy.couldBe(opt(subRecTy)));
  EXPECT_TRUE(opt(subRecTy).couldBe(exactRecTy));

  // Obj<= is not a subtype of ?Obj=, it is overlapping but
  // potentially contains other types.  (We might eventually check
  // whether objects are final as part of this, but not right now.)
  EXPECT_TRUE(!subObjTy.subtypeOf(opt(objExactTy)));
  EXPECT_TRUE(!opt(objExactTy).subtypeOf(subObjTy));
  EXPECT_TRUE(!subRecTy.subtypeOf(opt(exactRecTy)));
  EXPECT_TRUE(!opt(exactRecTy).subtypeOf(subRecTy));

  // Obj<= couldBe ?Obj= and vice versa.
  EXPECT_TRUE(subObjTy.couldBe(opt(objExactTy)));
  EXPECT_TRUE(opt(objExactTy).couldBe(subObjTy));
  EXPECT_TRUE(subRecTy.couldBe(opt(exactRecTy)));
  EXPECT_TRUE(opt(exactRecTy).couldBe(subRecTy));

  // ?Obj<=, ?Obj=, ?Foo<= and ?Foo= couldBe each other
  EXPECT_TRUE(opt(subObjTy).couldBe(opt(objExactBaseTy)));
  EXPECT_TRUE(opt(objExactBaseTy).couldBe(opt(subObjTy)));
  EXPECT_TRUE(opt(subObjTy).couldBe(opt(subObjBaseTy)));
  EXPECT_TRUE(opt(subObjBaseTy).couldBe(opt(subObjTy)));
  EXPECT_TRUE(opt(objExactTy).couldBe(opt(objExactBaseTy)));
  EXPECT_TRUE(opt(objExactBaseTy).couldBe(opt(objExactTy)));
  EXPECT_TRUE(opt(objExactTy).couldBe(opt(subObjBaseTy)));
  EXPECT_TRUE(opt(subObjBaseTy).couldBe(opt(objExactTy)));
}

TEST_F(TypeTest, Hierarchies) {
  auto const program = make_test_program();
  auto const unit = program->units.back().get();
  auto const func = [&]() -> php::Func* {
    for (auto& f : unit->funcs) {
      if (f->name->isame(s_test.get())) return f.get();
    }
    return nullptr;
  }();
  EXPECT_TRUE(func != nullptr);

  auto const ctx = Context { unit, func };
  Index idx{program.get()};

  // load classes in hierarchy
  auto const clsBase = idx.resolve_class(ctx, s_Base.get());
  if (!clsBase) ADD_FAILURE();
  auto const clsA = idx.resolve_class(ctx, s_A.get());
  if (!clsA) ADD_FAILURE();
  auto const clsB = idx.resolve_class(ctx, s_B.get());
  if (!clsB) ADD_FAILURE();
  auto const clsAA = idx.resolve_class(ctx, s_AA.get());
  if (!clsAA) ADD_FAILURE();
  auto const clsAB = idx.resolve_class(ctx, s_AB.get());
  if (!clsAB) ADD_FAILURE();
  auto const clsBA = idx.resolve_class(ctx, s_BA.get());
  if (!clsBA) ADD_FAILURE();
  auto const clsBB = idx.resolve_class(ctx, s_BB.get());
  if (!clsBB) ADD_FAILURE();
  auto const clsBAA = idx.resolve_class(ctx, s_BAA.get());
  if (!clsBAA) ADD_FAILURE();
  auto const clsTestClass = idx.resolve_class(ctx, s_TestClass.get());
  if (!clsTestClass) ADD_FAILURE();

  auto const recBaseUnique = idx.resolve_record(s_UniqueRecBase.get());
  if (!recBaseUnique) ADD_FAILURE();
  auto const recUnique = idx.resolve_record(s_UniqueRec.get());
  if (!recUnique) ADD_FAILURE();
  auto const recUniqueA = idx.resolve_record(s_UniqueRecA.get());
  if (!recUniqueA) ADD_FAILURE();

  // make *exact type* and *sub type* types and objects for all loaded classes
  auto const objExactBaseTy = objExact(*clsBase);
  auto const subObjBaseTy   = subObj(*clsBase);
  auto const clsExactBaseTy = clsExact(*clsBase);
  auto const subClsBaseTy   = subCls(*clsBase);

  auto const objExactATy    = objExact(*clsA);
  auto const subObjATy      = subObj(*clsA);
  auto const clsExactATy    = clsExact(*clsA);
  auto const subClsATy      = subCls(*clsA);

  auto const objExactAATy    = objExact(*clsAA);
  auto const subObjAATy      = subObj(*clsAA);
  auto const clsExactAATy    = clsExact(*clsAA);
  auto const subClsAATy      = subCls(*clsAA);

  auto const objExactABTy    = objExact(*clsAB);
  auto const subObjABTy      = subObj(*clsAB);
  auto const clsExactABTy    = clsExact(*clsAB);
  auto const subClsABTy      = subCls(*clsAB);

  auto const objExactBTy    = objExact(*clsB);
  auto const subObjBTy      = subObj(*clsB);
  auto const clsExactBTy    = clsExact(*clsB);
  auto const subClsBTy      = subCls(*clsB);

  auto const objExactBATy    = objExact(*clsBA);
  auto const subObjBATy      = subObj(*clsBA);
  auto const clsExactBATy    = clsExact(*clsBA);
  auto const subClsBATy      = subCls(*clsBA);

  auto const objExactBBTy    = objExact(*clsBB);
  auto const subObjBBTy      = subObj(*clsBB);
  auto const clsExactBBTy    = clsExact(*clsBB);
  auto const subClsBBTy      = subCls(*clsBB);

  auto const objExactBAATy    = objExact(*clsBAA);
  auto const subObjBAATy      = subObj(*clsBAA);
  auto const clsExactBAATy    = clsExact(*clsBAA);
  auto const subClsBAATy      = subCls(*clsBAA);

  auto const objExactTestClassTy = objExact(*clsTestClass);
  auto const subObjTestClassTy   = subObj(*clsTestClass);
  auto const clsExactTestClassTy = clsExact(*clsTestClass);
  auto const subClsTestClassTy   = subCls(*clsTestClass);

  auto const exactRecUniqueBaseTy = exactRecord(*recBaseUnique);
  auto const subRecUniqueBaseTy = subRecord(*recBaseUnique);
  auto const exactRecUniqueTy = exactRecord(*recUnique);
  auto const subRecUniqueTy   = subRecord(*recUnique);
  auto const exactRecUniqueATy = exactRecord(*recUniqueA);
  auto const subRecUniqueATy   = subRecord(*recUniqueA);

  // check that type from object and type are the same (obnoxious test)
  EXPECT_EQ(objcls(objExactBaseTy), clsExactBaseTy);
  EXPECT_EQ(objcls(subObjBaseTy), subClsBaseTy);
  EXPECT_EQ(objcls(objExactATy), clsExactATy);
  EXPECT_EQ(objcls(subObjATy), subClsATy);
  EXPECT_EQ(objcls(objExactAATy), clsExactAATy);
  EXPECT_EQ(objcls(subObjAATy), subClsAATy);
  EXPECT_EQ(objcls(objExactABTy), clsExactABTy);
  EXPECT_EQ(objcls(subObjABTy), subClsABTy);
  EXPECT_EQ(objcls(objExactBTy), clsExactBTy);
  EXPECT_EQ(objcls(subObjBTy), subClsBTy);
  EXPECT_EQ(objcls(objExactBATy), clsExactBATy);
  EXPECT_EQ(objcls(subObjBATy), subClsBATy);
  EXPECT_EQ(objcls(objExactBBTy), clsExactBBTy);
  EXPECT_EQ(objcls(subObjBBTy), subClsBBTy);
  EXPECT_EQ(objcls(objExactBAATy), clsExactBAATy);
  EXPECT_EQ(objcls(subObjBAATy), subClsBAATy);

  // both subobj(A) and subcls(A) of no_override class A change to exact types
  EXPECT_EQ(objcls(objExactABTy), subClsABTy);
  EXPECT_EQ(objcls(subObjABTy), clsExactABTy);

  // a T= is a subtype of itself but not a strict subtype
  // also a T= is in a "could be" relationship with itself.
  EXPECT_TRUE(objcls(objExactBaseTy).subtypeOf(clsExactBaseTy));
  EXPECT_FALSE(objcls(objExactBaseTy).strictSubtypeOf(objcls(objExactBaseTy)));
  EXPECT_TRUE(objcls(objExactBAATy).subtypeOf(clsExactBAATy));
  EXPECT_FALSE(clsExactBAATy.strictSubtypeOf(objcls(objExactBAATy)));
  EXPECT_TRUE(clsExactBAATy.couldBe(clsExactBAATy));

  // Given the hierarchy A <- B <- C where A is the base then:
  // B= is not in any subtype relationshipt with a A= or C=.
  // Neither they are in "could be" relationships.
  // Overall T= sets are always disjoint.
  EXPECT_FALSE(objcls(objExactBATy).subtypeOf(clsExactBaseTy));
  EXPECT_FALSE(objcls(objExactBATy).subtypeOf(clsExactBTy));
  EXPECT_FALSE(objcls(objExactBATy).subtypeOf(clsExactBAATy));
  EXPECT_FALSE(clsExactBATy.strictSubtypeOf(objcls(objExactBaseTy)));
  EXPECT_FALSE(clsExactBATy.strictSubtypeOf(objcls(objExactBTy)));
  EXPECT_FALSE(clsExactBATy.strictSubtypeOf(objcls(objExactBAATy)));
  EXPECT_FALSE(clsExactBATy.couldBe(objcls(objExactBaseTy)));
  EXPECT_FALSE(objcls(objExactBATy).couldBe(clsExactBTy));
  EXPECT_FALSE(clsExactBATy.couldBe(objcls(objExactBAATy)));

  // any T= is both a subtype and strict subtype of T<=.
  // Given the hierarchy A <- B <- C where A is the base then:
  // C= is a subtype and a strict subtype of B<=, ?B<=, A<= and ?A<=.
  // The "could be" relationship also holds.
  EXPECT_TRUE(objcls(objExactATy).subtypeOf(subClsATy));
  EXPECT_TRUE(objcls(objExactBAATy).subtypeOf(subClsBaseTy));
  EXPECT_TRUE(objExactBAATy.subtypeOf(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(objExactBAATy).subtypeOf(subClsBTy));
  EXPECT_TRUE(objExactBAATy.subtypeOf(opt(subObjBTy)));
  EXPECT_TRUE(clsExactBAATy.subtypeOf(objcls(subObjBATy)));
  EXPECT_TRUE(objExactBAATy.subtypeOf(opt(subObjBATy)));
  EXPECT_TRUE(clsExactBAATy.subtypeOf(objcls(subObjBAATy)));
  EXPECT_TRUE(objExactBAATy.subtypeOf(opt(subObjBAATy)));
  EXPECT_TRUE(objcls(objExactATy).strictSubtypeOf(subClsATy));
  EXPECT_TRUE(objcls(objExactBAATy).strictSubtypeOf(subClsBaseTy));
  EXPECT_TRUE(objExactBAATy.strictSubtypeOf(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(objExactBAATy).strictSubtypeOf(subClsBTy));
  EXPECT_TRUE(objExactBAATy.strictSubtypeOf(opt(subObjBTy)));
  EXPECT_TRUE(clsExactBAATy.strictSubtypeOf(objcls(subObjBATy)));
  EXPECT_TRUE(objExactBAATy.strictSubtypeOf(opt(subObjBATy)));
  EXPECT_TRUE(clsExactBAATy.strictSubtypeOf(objcls(subObjBAATy)));
  EXPECT_TRUE(objExactBAATy.strictSubtypeOf(opt(subObjBAATy)));
  EXPECT_TRUE(objcls(objExactATy).couldBe(subClsATy));
  EXPECT_TRUE(objcls(objExactBAATy).couldBe(subClsBaseTy));
  EXPECT_TRUE(objExactBAATy.couldBe(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(objExactBAATy).couldBe(subClsBTy));
  EXPECT_TRUE(objExactBAATy.couldBe(opt(subObjBTy)));
  EXPECT_TRUE(clsExactBAATy.couldBe(objcls(subObjBATy)));
  EXPECT_TRUE(objExactBAATy.couldBe(opt(subObjBATy)));
  EXPECT_TRUE(clsExactBAATy.couldBe(objcls(subObjBAATy)));
  EXPECT_TRUE(objExactBAATy.couldBe(opt(subObjBAATy)));

  // a T<= is a subtype of itself but not a strict subtype
  // also a T<= is in a "could be" relationship with itself
  EXPECT_TRUE(objcls(subObjBaseTy).subtypeOf(subClsBaseTy));
  EXPECT_FALSE(objcls(subObjBaseTy).strictSubtypeOf(objcls(subObjBaseTy)));
  EXPECT_TRUE(objcls(subObjBAATy).subtypeOf(subClsBAATy));
  EXPECT_FALSE(subClsBAATy.strictSubtypeOf(objcls(subObjBAATy)));
  EXPECT_TRUE(subClsBAATy.couldBe(subClsBAATy));

  // a T<= type is in no subtype relationship with T=.
  // However a T<= is in a "could be" relationship with T=.
  EXPECT_FALSE(objcls(subObjATy).subtypeOf(clsExactATy));
  EXPECT_FALSE(objcls(subObjATy).strictSubtypeOf(clsExactATy));
  EXPECT_TRUE(clsExactATy.couldBe(objcls(subObjATy)));

  // Given 2 types A and B in no inheritance relationship then
  // A<= and B<= are in no subtype or "could be" relationship.
  // Same if one of the 2 types is an optional type
  EXPECT_FALSE(objcls(subObjATy).subtypeOf(clsExactBTy));
  EXPECT_FALSE(objcls(subObjATy).strictSubtypeOf(clsExactBTy));
  EXPECT_FALSE(subObjATy.subtypeOf(opt(objExactBTy)));
  EXPECT_FALSE(subObjATy.strictSubtypeOf(opt(objExactBTy)));
  EXPECT_FALSE(clsExactATy.couldBe(objcls(subObjBTy)));
  EXPECT_FALSE(objExactATy.couldBe(opt(subObjBTy)));
  EXPECT_FALSE(objcls(subObjBTy).subtypeOf(clsExactATy));
  EXPECT_FALSE(subObjBTy.subtypeOf(opt(objExactATy)));
  EXPECT_FALSE(objcls(subObjBTy).strictSubtypeOf(clsExactATy));
  EXPECT_FALSE(subObjBTy.strictSubtypeOf(opt(objExactATy)));
  EXPECT_FALSE(clsExactBTy.couldBe(objcls(subObjATy)));
  EXPECT_FALSE(objExactBTy.couldBe(opt(subObjATy)));

  // Given the hierarchy A <- B <- C where A is the base then:
  // C<= is a subtype and a strict subtype of B<=, ?B<=, A<= and ?A<=.
  // It is also in a "could be" relationship with all its ancestors
  // (including optional)
  EXPECT_TRUE(objcls(subObjBAATy).subtypeOf(subClsBaseTy));
  EXPECT_TRUE(subObjBAATy.subtypeOf(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(subObjBAATy).subtypeOf(subClsBTy));
  EXPECT_TRUE(subObjBAATy.subtypeOf(opt(subObjBTy)));
  EXPECT_TRUE(subClsBAATy.subtypeOf(objcls(subObjBATy)));
  EXPECT_TRUE(subObjBAATy.subtypeOf(opt(subObjBATy)));
  EXPECT_TRUE(objcls(subObjBAATy).strictSubtypeOf(subClsBaseTy));
  EXPECT_TRUE(subObjBAATy.strictSubtypeOf(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(subObjBAATy).strictSubtypeOf(subClsBTy));
  EXPECT_TRUE(subObjBAATy.strictSubtypeOf(opt(subObjBTy)));
  EXPECT_TRUE(subClsBAATy.strictSubtypeOf(objcls(subObjBATy)));
  EXPECT_TRUE(subObjBAATy.strictSubtypeOf(opt(subObjBATy)));
  EXPECT_TRUE(objcls(subObjBAATy).couldBe(subClsBaseTy));
  EXPECT_TRUE(subObjBAATy.couldBe(opt(subObjBaseTy)));
  EXPECT_TRUE(objcls(subObjBAATy).couldBe(subClsBTy));
  EXPECT_TRUE(subObjBAATy.couldBe(opt(subObjBTy)));
  EXPECT_TRUE(subClsBAATy.couldBe(objcls(subObjBATy)));
  EXPECT_TRUE(subObjBAATy.couldBe(opt(subObjBATy)));

  // Given the hierarchy A <- B <- C where A is the base then:
  // A<= is not in a subtype neither a strict subtype with B<=, ?B<=, A<=
  // ?A<=. However A<= is in a "could be" relationship with all its
  // children (including optional)
  EXPECT_FALSE(objcls(subObjBaseTy).subtypeOf(subClsATy));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjATy)));
  EXPECT_FALSE(objcls(subObjBaseTy).subtypeOf(subClsBTy));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjBTy)));
  EXPECT_FALSE(subClsBaseTy.subtypeOf(objcls(subObjAATy)));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjAATy)));
  EXPECT_FALSE(subClsBaseTy.subtypeOf(objcls(subObjABTy)));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjABTy)));
  EXPECT_FALSE(objcls(subObjBaseTy).subtypeOf(subClsBATy));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjBATy)));
  EXPECT_FALSE(subClsBaseTy.subtypeOf(objcls(subObjBBTy)));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjBBTy)));
  EXPECT_FALSE(subClsBaseTy.subtypeOf(objcls(subObjBAATy)));
  EXPECT_FALSE(subObjBaseTy.subtypeOf(opt(subObjBAATy)));
  EXPECT_FALSE(objcls(subObjBaseTy).strictSubtypeOf(subClsATy));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjATy)));
  EXPECT_FALSE(objcls(subObjBaseTy).strictSubtypeOf(subClsBTy));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjBTy)));
  EXPECT_FALSE(subClsBaseTy.strictSubtypeOf(objcls(subObjAATy)));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjAATy)));
  EXPECT_FALSE(subClsBaseTy.strictSubtypeOf(objcls(subObjABTy)));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjABTy)));
  EXPECT_FALSE(objcls(subObjBaseTy).strictSubtypeOf(subClsBATy));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjBATy)));
  EXPECT_FALSE(subClsBaseTy.strictSubtypeOf(objcls(subObjBBTy)));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjBBTy)));
  EXPECT_FALSE(subClsBaseTy.strictSubtypeOf(objcls(subObjBAATy)));
  EXPECT_FALSE(subObjBaseTy.strictSubtypeOf(opt(subObjBAATy)));
  EXPECT_TRUE(objcls(subObjBaseTy).couldBe(subClsATy));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjATy)));
  EXPECT_TRUE(objcls(subObjBaseTy).couldBe(subClsBTy));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjBTy)));
  EXPECT_TRUE(subClsBaseTy.couldBe(objcls(subObjAATy)));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjAATy)));
  EXPECT_TRUE(subClsBaseTy.couldBe(objcls(subObjABTy)));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjABTy)));
  EXPECT_TRUE(objcls(subObjBaseTy).couldBe(subClsBATy));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjBATy)));
  EXPECT_TRUE(subClsBaseTy.couldBe(objcls(subObjBBTy)));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjBBTy)));
  EXPECT_TRUE(subClsBaseTy.couldBe(objcls(subObjBAATy)));
  EXPECT_TRUE(subObjBaseTy.couldBe(opt(subObjBAATy)));

  // check union_of and commonAncestor API
  EXPECT_TRUE((*(*clsA).commonAncestor(*clsB)).same(*clsBase));
  EXPECT_TRUE((*(*clsB).commonAncestor(*clsA)).same(*clsBase));
  EXPECT_TRUE((*(*clsAA).commonAncestor(*clsAB)).same(*clsA));
  EXPECT_TRUE((*(*clsAB).commonAncestor(*clsAA)).same(*clsA));
  EXPECT_TRUE((*(*clsA).commonAncestor(*clsBAA)).same(*clsBase));
  EXPECT_TRUE((*(*clsBAA).commonAncestor(*clsA)).same(*clsBase));
  EXPECT_TRUE((*(*clsBAA).commonAncestor(*clsB)).same(*clsB));
  EXPECT_TRUE((*(*clsB).commonAncestor(*clsBAA)).same(*clsB));
  EXPECT_TRUE((*(*clsBAA).commonAncestor(*clsBB)).same(*clsB));
  EXPECT_TRUE((*(*clsBB).commonAncestor(*clsBAA)).same(*clsB));
  EXPECT_TRUE((*(*clsAA).commonAncestor(*clsBase)).same(*clsBase));
  EXPECT_TRUE((*(*clsBase).commonAncestor(*clsAA)).same(*clsBase));
  EXPECT_FALSE((*clsAA).commonAncestor(*clsTestClass));
  EXPECT_FALSE((*clsTestClass).commonAncestor(*clsAA));

  // check union_of
  // union of subCls
  EXPECT_EQ(union_of(subClsATy, subClsBTy), subClsBaseTy);
  EXPECT_EQ(union_of(subClsAATy, subClsABTy), subClsATy);
  EXPECT_EQ(union_of(subClsATy, subClsBAATy), subClsBaseTy);
  EXPECT_EQ(union_of(subClsBAATy, subClsBTy), subClsBTy);
  EXPECT_EQ(union_of(subClsBAATy, subClsBBTy), subClsBTy);
  EXPECT_EQ(union_of(subClsAATy, subClsBaseTy), subClsBaseTy);
  EXPECT_EQ(union_of(subClsAATy, subClsTestClassTy), TCls);
  // union of subCls and clsExact mixed
  EXPECT_EQ(union_of(clsExactATy, subClsBTy), subClsBaseTy);
  EXPECT_EQ(union_of(subClsAATy, clsExactABTy), subClsATy);
  EXPECT_EQ(union_of(clsExactATy, subClsBAATy), subClsBaseTy);
  EXPECT_EQ(union_of(subClsBAATy, clsExactBTy), subClsBTy);
  EXPECT_EQ(union_of(clsExactBAATy, subClsBBTy), subClsBTy);
  EXPECT_EQ(union_of(subClsAATy, clsExactBaseTy), subClsBaseTy);
  EXPECT_EQ(union_of(clsExactAATy, subClsTestClassTy), TCls);
  // union of clsExact
  EXPECT_EQ(union_of(clsExactATy, clsExactBTy), subClsBaseTy);
  EXPECT_EQ(union_of(clsExactAATy, clsExactABTy), subClsATy);
  EXPECT_EQ(union_of(clsExactATy, clsExactBAATy), subClsBaseTy);
  EXPECT_EQ(union_of(clsExactBAATy, clsExactBTy), subClsBTy);
  EXPECT_EQ(union_of(clsExactBAATy, clsExactBBTy), subClsBTy);
  EXPECT_EQ(union_of(clsExactAATy, clsExactBaseTy), subClsBaseTy);
  EXPECT_EQ(union_of(clsExactAATy, subClsTestClassTy), TCls);
  // union of subObj
  EXPECT_EQ(union_of(subObjATy, subObjBTy), subObjBaseTy);
  EXPECT_EQ(union_of(subObjAATy, subObjABTy), subObjATy);
  EXPECT_EQ(union_of(subObjATy, subObjBAATy), subObjBaseTy);
  EXPECT_EQ(union_of(subObjBAATy, subObjBTy), subObjBTy);
  EXPECT_EQ(union_of(subObjBAATy, subObjBBTy), subObjBTy);
  EXPECT_EQ(union_of(subObjAATy, subObjBaseTy), subObjBaseTy);
  EXPECT_EQ(union_of(subObjAATy, subObjTestClassTy), TObj);
  EXPECT_EQ(union_of(subRecUniqueTy, subRecUniqueATy), subRecUniqueBaseTy);
  // union of subObj and objExact mixed
  EXPECT_EQ(union_of(objExactATy, subObjBTy), subObjBaseTy);
  EXPECT_EQ(union_of(subObjAATy, objExactABTy), subObjATy);
  EXPECT_EQ(union_of(objExactATy, subObjBAATy), subObjBaseTy);
  EXPECT_EQ(union_of(subObjBAATy, objExactBTy), subObjBTy);
  EXPECT_EQ(union_of(objExactBAATy, subObjBBTy), subObjBTy);
  EXPECT_EQ(union_of(subObjAATy, objExactBaseTy), subObjBaseTy);
  EXPECT_EQ(union_of(objExactAATy, subObjTestClassTy), TObj);
  EXPECT_EQ(union_of(subRecUniqueATy, exactRecUniqueTy), subRecUniqueBaseTy);
  // union of objExact
  EXPECT_EQ(union_of(objExactATy, objExactBTy), subObjBaseTy);
  EXPECT_EQ(union_of(objExactAATy, objExactABTy), subObjATy);
  EXPECT_EQ(union_of(objExactATy, objExactBAATy), subObjBaseTy);
  EXPECT_EQ(union_of(objExactBAATy, objExactBTy), subObjBTy);
  EXPECT_EQ(union_of(objExactBAATy, objExactBBTy), subObjBTy);
  EXPECT_EQ(union_of(objExactAATy, objExactBaseTy), subObjBaseTy);
  EXPECT_EQ(union_of(objExactAATy, objExactTestClassTy), TObj);
  EXPECT_EQ(union_of(subRecUniqueATy, exactRecUniqueTy), subRecUniqueBaseTy);
  // optional sub obj
  EXPECT_EQ(union_of(opt(subObjATy), opt(subObjBTy)), opt(subObjBaseTy));
  EXPECT_EQ(union_of(subObjAATy, opt(subObjABTy)), opt(subObjATy));
  EXPECT_EQ(union_of(opt(subObjATy), subObjBAATy), opt(subObjBaseTy));
  EXPECT_EQ(union_of(opt(subObjBAATy), opt(subObjBTy)), opt(subObjBTy));
  EXPECT_EQ(union_of(opt(subObjBAATy), subObjBBTy), opt(subObjBTy));
  EXPECT_EQ(union_of(opt(subObjAATy), opt(subObjBaseTy)), opt(subObjBaseTy));
  EXPECT_EQ(union_of(subObjAATy, opt(subObjTestClassTy)), opt(TObj));
  EXPECT_EQ(union_of(opt(subRecUniqueATy), exactRecUniqueTy),
            opt(subRecUniqueBaseTy));
  EXPECT_EQ(union_of(opt(subRecUniqueATy), opt(exactRecUniqueTy)),
            opt(subRecUniqueBaseTy));
  // optional sub and exact obj mixed
  EXPECT_EQ(union_of(opt(objExactATy), subObjBTy), opt(subObjBaseTy));
  EXPECT_EQ(union_of(subObjAATy, opt(objExactABTy)), opt(subObjATy));
  EXPECT_EQ(union_of(opt(objExactATy), objExactBAATy), opt(subObjBaseTy));
  EXPECT_EQ(union_of(subObjBAATy, opt(objExactBTy)), opt(subObjBTy));
  EXPECT_EQ(union_of(opt(subObjBAATy), objExactBBTy), opt(subObjBTy));
  EXPECT_EQ(union_of(objExactAATy, opt(objExactBaseTy)), opt(subObjBaseTy));
  EXPECT_EQ(union_of(opt(subObjAATy), objExactTestClassTy), opt(TObj));
}

TEST_F(TypeTest, Interface) {
  auto const program = make_test_program();
  auto const unit = program->units.back().get();
  auto const func = [&]() -> php::Func* {
    for (auto& f : unit->funcs) {
      if (f->name->isame(s_test.get())) return f.get();
    }
    return nullptr;
  }();
  EXPECT_TRUE(func != nullptr);

  auto const ctx = Context { unit, func };
  Index idx{program.get()};

  // load classes in hierarchy
  auto const clsIA = idx.resolve_class(ctx, s_IA.get());
  if (!clsIA) ADD_FAILURE();
  auto const clsIB = idx.resolve_class(ctx, s_IB.get());
  if (!clsIB) ADD_FAILURE();
  auto const clsIAA = idx.resolve_class(ctx, s_IAA.get());
  if (!clsIAA) ADD_FAILURE();
  auto const clsA = idx.resolve_class(ctx, s_A.get());
  if (!clsA) ADD_FAILURE();
  auto const clsAA = idx.resolve_class(ctx, s_AA.get());
  if (!clsAA) ADD_FAILURE();

  // make sometypes and objects
  auto const subObjIATy   = subObj(*clsIA);
  auto const subClsIATy   = subCls(*clsIA);
  auto const subObjIAATy  = subObj(*clsIAA);
  auto const subClsIAATy  = subCls(*clsIAA);
  auto const subObjIBTy   = subObj(*clsIB);
  auto const subObjATy    = subObj(*clsA);
  auto const clsExactATy  = clsExact(*clsA);
  auto const subClsATy    = subCls(*clsA);
  auto const subObjAATy   = subObj(*clsAA);
  auto const subClsAATy   = subCls(*clsAA);
  auto const exactObjATy  = objExact(*clsA);
  auto const exactObjAATy = objExact(*clsAA);

  EXPECT_TRUE(subClsATy.subtypeOf(objcls(subObjIATy)));
  EXPECT_TRUE(subClsATy.couldBe(objcls(subObjIATy)));
  EXPECT_TRUE(objcls(subObjATy).strictSubtypeOf(subClsIATy));
  EXPECT_TRUE(subClsAATy.subtypeOf(objcls(subObjIAATy)));
  EXPECT_TRUE(subClsAATy.couldBe(objcls(subObjIAATy)));
  EXPECT_TRUE(objcls(subObjAATy).strictSubtypeOf(objcls(subObjIAATy)));

  EXPECT_FALSE(subClsATy.subtypeOf(objcls(subObjIAATy)));
  EXPECT_FALSE(objcls(subObjATy).strictSubtypeOf(objcls(subObjIAATy)));

  EXPECT_EQ(intersection_of(subObjIAATy, subObjAATy), subObjAATy);
  EXPECT_EQ(intersection_of(subObjIAATy, exactObjAATy), exactObjAATy);
  EXPECT_EQ(intersection_of(subObjIAATy, exactObjATy), TBottom);
  EXPECT_EQ(intersection_of(subObjIAATy, subObjATy), subObjATy);
  EXPECT_EQ(intersection_of(subObjIAATy, subObjIBTy), TObj);

  EXPECT_FALSE(clsExactATy.couldBe(objcls(subObjIAATy)));

  EXPECT_TRUE(union_of(opt(exactObjATy), opt(subObjIATy)) == opt(subObjIATy));
  // Since we have invariants in the index that types only improve, it is
  // important that the below union is more or equally refined than the
  // above union.
  EXPECT_TRUE(union_of(opt(exactObjATy), subObjIATy) == opt(subObjIATy));
}

TEST_F(TypeTest, LoosenInterfaces) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const test = [&] (const Type& t, auto const& self) -> Type {
    auto [obj, rest] = split_obj(t);
    EXPECT_EQ(loosen_interfaces(rest), rest);

    if (is_specialized_wait_handle(obj)) {
      EXPECT_EQ(loosen_interfaces(obj),
                wait_handle(index, self(wait_handle_inner(obj), self)));
    } else if (is_specialized_obj(obj) && dobj_of(obj).cls.couldBeInterface()) {
      EXPECT_EQ(loosen_interfaces(obj), TObj);
    } else {
      EXPECT_EQ(loosen_interfaces(obj), obj);
    }

    EXPECT_EQ(loosen_interfaces(t), union_of(loosen_interfaces(obj), rest));
    return loosen_interfaces(t);
  };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (t.is(BTop)) continue;

    EXPECT_EQ(loosen_interfaces(opt(t)), opt(loosen_interfaces(t)));
    if (t.subtypeOf(BInitCell) && !t.is(BBottom)) {
      EXPECT_EQ(loosen_interfaces(wait_handle(index, t)),
                wait_handle(index, loosen_interfaces(t)));
    }

    test(t, test);
  }
}

TEST_F(TypeTest, WaitH) {
  auto const program = make_test_program();
  Index index { program.get() };

  auto const rcls   = index.builtin_class(s_Awaitable.get());
  auto const twhobj = subObj(rcls);

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (t.is(BBottom)) continue;
    if (!t.subtypeOf(BInitCell)) continue;
    auto const wh = wait_handle(index, t);
    if (t.strictSubtypeOf(BInitCell)) {
      EXPECT_TRUE(is_specialized_wait_handle(wh));
    } else {
      EXPECT_FALSE(is_specialized_wait_handle(wh));
      EXPECT_TRUE(is_specialized_obj(wh));
      EXPECT_EQ(wh, twhobj);
    }
    EXPECT_TRUE(wh.couldBe(twhobj));
    EXPECT_TRUE(wh.subtypeOf(twhobj));
    EXPECT_TRUE(wh.subtypeOf(wait_handle(index, TInitCell)));
  }

  // union_of(WaitH<A>, WaitH<B>) == WaitH<union_of(A, B)>
  for (auto const& p1 : predefined()) {
    for (auto const& p2 : predefined()) {
      auto const& t1 = p1.second;
      auto const& t2 = p2.second;
      if (t1.is(BBottom) || t2.is(BBottom)) continue;
      if (!t1.subtypeOf(BInitCell) || !t2.subtypeOf(BInitCell)) continue;
      auto const u1 = union_of(t1, t2);
      auto const u2 = union_of(wait_handle(index, t1), wait_handle(index, t2));
      if (u1.strictSubtypeOf(BInitCell)) {
        EXPECT_TRUE(is_specialized_wait_handle(u2));
        EXPECT_EQ(wait_handle_inner(u2), u1);
        EXPECT_EQ(wait_handle(index, u1), u2);
      } else {
        EXPECT_FALSE(is_specialized_wait_handle(u2));
        EXPECT_TRUE(is_specialized_obj(u2));
        EXPECT_EQ(u2, twhobj);
      }

      if (t1.subtypeOf(t2)) {
        EXPECT_TRUE(wait_handle(index, t1).subtypeOf(wait_handle(index, t2)));
      } else {
        EXPECT_FALSE(wait_handle(index, t1).subtypeOf(wait_handle(index, t2)));
      }

      if (t1.couldBe(t2)) {
        EXPECT_TRUE(wait_handle(index, t1).couldBe(wait_handle(index, t2)));
      } else {
        EXPECT_FALSE(wait_handle(index, t1).couldBe(wait_handle(index, t2)));
      }
    }
  }

  // union_of(?WaitH<A>, ?WaitH<B>) == ?WaitH<union_of(A, B)>
  for (auto const& p1 : predefined()) {
    for (auto const& p2 : predefined()) {
      auto const& t1 = p1.second;
      auto const& t2 = p2.second;
      if (t1.is(BBottom) || t2.is(BBottom)) continue;
      if (!t1.subtypeOf(BInitCell) || !t2.subtypeOf(BInitCell)) continue;
      auto const w1 = opt(wait_handle(index, t1));
      auto const w2 = opt(wait_handle(index, t2));
      auto const u1 = union_of(w1, w2);
      auto const u2 = opt(wait_handle(index, union_of(t1, t2)));
      EXPECT_EQ(u1, u2);
    }
  }

  // Some test cases with optional wait handles.
  auto const optWH = opt(wait_handle(index, ival(2)));
  EXPECT_TRUE(TInitNull.subtypeOf(optWH));
  EXPECT_TRUE(optWH.subtypeOf(BOptObj));
  EXPECT_TRUE(optWH.subtypeOf(opt(twhobj)));
  EXPECT_TRUE(wait_handle(index, ival(2)).subtypeOf(optWH));
  EXPECT_FALSE(optWH.subtypeOf(wait_handle(index, ival(2))));
  EXPECT_TRUE(optWH.couldBe(wait_handle(index, ival(2))));

  // union_of(WaitH<T>, Obj<=Awaitable) == Obj<=Awaitable
  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BInitCell)) continue;
    auto const u = union_of(wait_handle(index, t), twhobj);
    EXPECT_EQ(u, twhobj);
  }

  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BInitCell)) continue;
    auto const u1 = union_of(wait_handle(index, t), TInitNull);
    auto const u2 = union_of(TInitNull, wait_handle(index, t));
    EXPECT_EQ(u1, u2);
    if (t.strictSubtypeOf(BInitCell)) {
      EXPECT_TRUE(is_specialized_wait_handle(u1));
      EXPECT_TRUE(is_specialized_wait_handle(u2));
    } else {
      EXPECT_FALSE(is_specialized_wait_handle(u1));
      EXPECT_FALSE(is_specialized_wait_handle(u2));
      EXPECT_EQ(u1, opt(twhobj));
      EXPECT_EQ(u2, opt(twhobj));
     }
  }

  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BInitCell)) continue;
    auto const wh = wait_handle(index, t);
    EXPECT_EQ(intersection_of(wh, twhobj), wh);
  }
}

TEST_F(TypeTest, FromHNIConstraint) {
  EXPECT_EQ(from_hni_constraint(makeStaticString("?HH\\resource")), TOptRes);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\resource")), TRes);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\bool")), TBool);
  EXPECT_EQ(from_hni_constraint(makeStaticString("?HH\\bool")), TOptBool);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\int")), TInt);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\float")), TDbl);
  EXPECT_EQ(from_hni_constraint(makeStaticString("?HH\\float")), TOptDbl);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\mixed")), TInitCell);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\arraykey")), TArrKey);
  EXPECT_EQ(from_hni_constraint(makeStaticString("?HH\\arraykey")), TOptArrKey);
  EXPECT_EQ(from_hni_constraint(makeStaticString("HH\\nonnull")), TNonNull);
  EXPECT_EQ(from_hni_constraint(makeStaticString("?HH\\nonnull")), TInitCell);

  // These are conservative, but we're testing them that way.  If we
  // make the function better later we'll remove the tests.
  EXPECT_EQ(from_hni_constraint(makeStaticString("stdClass")), TCell);
  EXPECT_EQ(from_hni_constraint(makeStaticString("?stdClass")), TCell);
  EXPECT_EQ(from_hni_constraint(makeStaticString("fooooo")), TCell);
  EXPECT_EQ(from_hni_constraint(makeStaticString("")), TCell);
}

TEST_F(TypeTest, DictPacked1) {
  auto const a1 = dict_packed({ival(2), TSStr, TInt});
  auto const a2 = dict_packed({TInt,    TStr,  TInitCell});
  auto const s1 = sdict_packed({ival(2), TSStr, TInt});
  auto const s2 = sdict_packed({TInt,    TSStr,  TInitUnc});

  for (auto& a : { a1, s1, a2, s2 }) {
    EXPECT_TRUE(a.subtypeOf(BDict));
    EXPECT_TRUE(a.subtypeOf(a));
    EXPECT_EQ(a, a);
  }

  // Subtype stuff.

  EXPECT_TRUE(a1.subtypeOf(BDict));
  EXPECT_FALSE(a1.subtypeOf(BSDict));

  EXPECT_TRUE(s1.subtypeOf(BDict));
  EXPECT_TRUE(s1.subtypeOf(BSDict));

  EXPECT_TRUE(a1.subtypeOf(a2));
  EXPECT_TRUE(s1.subtypeOf(s2));
  EXPECT_TRUE(s1.subtypeOf(a1));

  // Could be stuff.

  EXPECT_TRUE(s1.couldBe(a1));
  EXPECT_TRUE(s2.couldBe(a2));

  EXPECT_TRUE(a1.couldBe(a2));
  EXPECT_TRUE(a2.couldBe(a1));
  EXPECT_TRUE(s1.couldBe(a2));
  EXPECT_TRUE(s2.couldBe(a1));

  EXPECT_TRUE(s1.couldBe(s2));
  EXPECT_TRUE(s2.couldBe(s1));
}

TEST_F(TypeTest, OptDictPacked1) {
  auto const a1 = opt(dict_packed({ival(2), TSStr, TInt}));
  auto const a2 = opt(dict_packed({TInt,    TStr,  TInitCell}));
  auto const s1 = opt(sdict_packed({ival(2), TSStr, TInt}));
  auto const s2 = opt(sdict_packed({TInt,    TSStr,  TInitUnc}));

  for (auto& a : { a1, s1, a2, s2 }) {
    EXPECT_TRUE(a.subtypeOf(BOptDict));
    EXPECT_TRUE(a.subtypeOf(a));
    EXPECT_EQ(a, a);
  }

  // Subtype stuff.

  EXPECT_TRUE(a1.subtypeOf(BOptDict));
  EXPECT_FALSE(a1.subtypeOf(BOptSDict));

  EXPECT_TRUE(s1.subtypeOf(BOptDict));
  EXPECT_TRUE(s1.subtypeOf(BOptSDict));

  EXPECT_TRUE(a1.subtypeOf(a2));
  EXPECT_TRUE(s1.subtypeOf(s2));
  EXPECT_TRUE(s1.subtypeOf(a1));

  // Could be stuff.

  EXPECT_TRUE(s1.couldBe(a1));
  EXPECT_TRUE(s2.couldBe(a2));

  EXPECT_TRUE(a1.couldBe(a2));
  EXPECT_TRUE(a2.couldBe(a1));
  EXPECT_TRUE(s1.couldBe(a2));
  EXPECT_TRUE(s2.couldBe(a1));

  EXPECT_TRUE(s1.couldBe(s2));
  EXPECT_TRUE(s2.couldBe(s1));
}

TEST_F(TypeTest, DictPacked2) {
  {
    auto const a1 = dict_packed({TInt, TInt, TDbl});
    auto const a2 = dict_packed({TInt, TInt});
    EXPECT_FALSE(a1.subtypeOf(a2));
    EXPECT_FALSE(a1.couldBe(a2));
  }

  {
    auto const a1 = dict_packed({TInitCell, TInt});
    auto const a2 = dict_packed({TInt, TInt});
    EXPECT_TRUE(a1.couldBe(a2));
    EXPECT_TRUE(a2.subtypeOf(a1));
  }

  auto const packedDict = static_dict(0, 42, 1, 23, 2, 12);

  {
    auto const a1 = dict_packed({TInt, TInt, TInt});
    auto const s1 = sdict_packed({TInt, TInt, TInt});
    auto const s2 = dict_val(packedDict);
    EXPECT_TRUE(s2.subtypeOf(a1));
    EXPECT_TRUE(s2.subtypeOf(s1));
    EXPECT_TRUE(s2.couldBe(a1));
    EXPECT_TRUE(s2.couldBe(s1));
  }

  {
    auto const s1 = sdict_packed({ival(42), ival(23), ival(12)});
    auto const s2 = dict_val(packedDict);
    auto const s3 = sdict_packed({TInt});
    auto const a4 = sdict_packed({TInt});
    auto const a5 = dict_packed({ival(42), ival(23), ival(12)});
    EXPECT_TRUE(s2.subtypeOf(s1));
    EXPECT_NE(s1, s2);
    EXPECT_FALSE(s2.subtypeOf(s3));
    EXPECT_FALSE(s2.couldBe(s3));
    EXPECT_FALSE(s2.subtypeOf(s3));
    EXPECT_FALSE(s2.couldBe(s3));
    EXPECT_TRUE(s2.couldBe(s1));
    EXPECT_TRUE(s2.couldBe(a5));
    EXPECT_TRUE(s2.subtypeOf(a5));
    EXPECT_FALSE(a5.subtypeOf(s2));
  }
}

TEST_F(TypeTest, DictPackedUnion) {
  {
    auto const a1 = dict_packed({TInt, TDbl});
    auto const a2 = dict_packed({TDbl, TInt});
    EXPECT_EQ(union_of(a1, a2), dict_packed({TNum, TNum}));
  }

  {
    auto const s1 = sdict_packed({TInt, TDbl});
    auto const s2 = sdict_packed({TDbl, TInt});
    EXPECT_EQ(union_of(s1, s1), s1);
    EXPECT_EQ(union_of(s1, s2), sdict_packed({TNum, TNum}));
  }

  {
    auto const s1 = sdict_packed({TInt});
    auto const s2 = sdict_packed({TDbl, TDbl});
    EXPECT_EQ(union_of(s1, s2), sdict_packedn(TNum));
  }

  auto const packedDict1 = static_dict(0, 42, 1, 23, 2, 12);
  auto const packedDict2 = static_dict(0, 42, 1, 23.0, 2, 12);

  {
    auto const s1 = dict_val(packedDict1);
    auto const s2 = sdict_packed({TInt, TInt, TInt});
    auto const s3 = sdict_packed({TInt, TNum, TInt});
    EXPECT_EQ(union_of(s1, s2), s2);
    EXPECT_EQ(union_of(s1, s3), s3);
  }

  {
    auto const s1  = sdict_packed({TInt});
    auto const os1 = opt(s1);
    EXPECT_EQ(union_of(s1, TInitNull), os1);
    EXPECT_EQ(union_of(os1, s1), os1);
    EXPECT_EQ(union_of(TInitNull, s1), os1);
    EXPECT_EQ(union_of(os1, os1), os1);
  }

  {
    auto const s1 = sdict_packed({TInt});
    EXPECT_EQ(union_of(s1, TSDict), TSDict);
  }

  {
    auto const s1 = dict_val(packedDict1);
    auto const s2 = dict_val(packedDict2);
    EXPECT_EQ(
      loosen_mark_for_testing(union_of(s1, s2)),
      loosen_mark_for_testing(
        sdict_packed({ival(42), union_of(ival(23), TDbl), ival(12)})
      )
    );
  }
}

TEST_F(TypeTest, DictPackedN) {
  auto const packedDict = static_dict(0, 42, 1, 23, 2, 12);
  auto const a1 = dict_val(packedDict);
  auto const a2 = sdict_packed({TInt, TInt});
  EXPECT_EQ(union_of(a1, a2), sdict_packedn(TInt));

  auto const s2 = sdict_packed({TInt, TInt});
  EXPECT_TRUE(s2.subtypeOf(sdict_packedn(TInt)));
  EXPECT_FALSE(s2.subtypeOf(sdict_packedn(TDbl)));
  EXPECT_TRUE(s2.subtypeOf(sdict_packedn(TNum)));
  EXPECT_TRUE(s2.subtypeOf(dict_packedn(TInt)));
  EXPECT_TRUE(s2.subtypeOf(opt(dict_packedn(TInt))));

  EXPECT_TRUE(s2.couldBe(dict_packedn(TInt)));
  EXPECT_TRUE(s2.couldBe(dict_packedn(TInitCell)));

  auto const sn1 = sdict_packedn(TInt);
  auto const sn2 = sdict_packedn(TInitNull);

  EXPECT_EQ(union_of(sn1, sn2), sdict_packedn(TOptInt));
  EXPECT_EQ(union_of(sn1, TInitNull), opt(sn1));
  EXPECT_EQ(union_of(TInitNull, sn1), opt(sn1));
  EXPECT_FALSE(sn2.couldBe(sn1));
  EXPECT_FALSE(sn1.couldBe(sn2));

  auto const sn3 = sdict_packedn(TInitUnc);
  EXPECT_TRUE(sn1.couldBe(sn3));
  EXPECT_TRUE(sn2.couldBe(sn3));
  EXPECT_TRUE(sn3.couldBe(sn1));
  EXPECT_TRUE(sn3.couldBe(sn2));

  EXPECT_TRUE(s2.couldBe(sn3));
  EXPECT_TRUE(s2.couldBe(sn1));
  EXPECT_FALSE(s2.couldBe(sn2));
}

TEST_F(TypeTest, DictStruct) {
  auto const test_map_a          = MapElems{map_elem(s_test, ival(2))};
  auto const test_map_b          = MapElems{map_elem(s_test, TInt)};
  auto const test_map_c          = MapElems{map_elem(s_test, ival(2)),
                                            map_elem(s_A, TInt),
                                            map_elem(s_B, TDbl)};

  auto const ta = dict_map(test_map_a);
  auto const tb = dict_map(test_map_b);
  auto const tc = dict_map(test_map_c);

  EXPECT_FALSE(ta.subtypeOf(tc));
  EXPECT_FALSE(tc.subtypeOf(ta));
  EXPECT_TRUE(ta.subtypeOf(tb));
  EXPECT_FALSE(tb.subtypeOf(ta));
  EXPECT_TRUE(ta.couldBe(tb));
  EXPECT_TRUE(tb.couldBe(ta));
  EXPECT_FALSE(tc.couldBe(ta));
  EXPECT_FALSE(tc.couldBe(tb));

  EXPECT_TRUE(ta.subtypeOf(BDict));
  EXPECT_TRUE(tb.subtypeOf(BDict));
  EXPECT_TRUE(tc.subtypeOf(BDict));

  auto const sa = sdict_map(test_map_a);
  auto const sb = sdict_map(test_map_b);
  auto const sc = sdict_map(test_map_c);

  EXPECT_FALSE(sa.subtypeOf(sc));
  EXPECT_FALSE(sc.subtypeOf(sa));
  EXPECT_TRUE(sa.subtypeOf(sb));
  EXPECT_FALSE(sb.subtypeOf(sa));
  EXPECT_TRUE(sa.couldBe(sb));
  EXPECT_TRUE(sb.couldBe(sa));
  EXPECT_FALSE(sc.couldBe(sa));
  EXPECT_FALSE(sc.couldBe(sb));

  EXPECT_TRUE(sa.subtypeOf(BSDict));
  EXPECT_TRUE(sb.subtypeOf(BSDict));
  EXPECT_TRUE(sc.subtypeOf(BSDict));

  auto const testDict = static_dict(s_A.get(), s_B.get(), s_test.get(), 12);

  auto const test_map_d    = MapElems{map_elem(s_A, sval(s_B)), map_elem(s_test, ival(12))};
  auto const sd = sdict_map(test_map_d);
  EXPECT_TRUE(dict_val(testDict).subtypeOf(sd));

  auto const test_map_e    = MapElems{map_elem(s_A, TSStr), map_elem(s_test, TNum)};
  auto const se = sdict_map(test_map_e);
  EXPECT_TRUE(dict_val(testDict).subtypeOf(se));
  EXPECT_TRUE(se.couldBe(dict_val(testDict)));
}

TEST_F(TypeTest, DictMapN) {
  auto const test_map =
    dict_val(static_dict(s_A.get(), s_B.get(), s_test.get(), 12));
  EXPECT_TRUE(test_map != dict_n(TSStr, TInitUnc));
  EXPECT_TRUE(test_map.subtypeOf(dict_n(TSStr, TInitUnc)));
  EXPECT_TRUE(test_map.subtypeOf(sdict_n(TSStr, TInitUnc)));
  EXPECT_TRUE(sdict_packedn({TInt}).subtypeOf(dict_n(TInt, TInt)));
  EXPECT_TRUE(sdict_packed({TInt}).subtypeOf(dict_n(TInt, TInt)));

  auto const test_map_a    = MapElems{map_elem(s_test, ival(2))};
  auto const tstruct       = sdict_map(test_map_a);

  EXPECT_TRUE(tstruct.subtypeOf(dict_n(TSStr, ival(2))));
  EXPECT_TRUE(tstruct.subtypeOf(dict_n(TSStr, TInt)));
  EXPECT_TRUE(tstruct.subtypeOf(sdict_n(TSStr, TInt)));
  EXPECT_TRUE(tstruct.subtypeOf(dict_n(TStr, TInt)));

  EXPECT_TRUE(test_map.couldBe(dict_n(TSStr, TInitCell)));
  EXPECT_FALSE(test_map.couldBe(dict_n(TSStr, TStr)));
  EXPECT_FALSE(test_map.couldBe(dict_n(TSStr, TObj)));

  EXPECT_FALSE(test_map.couldBe(dict_val(staticEmptyDictArray())));
  EXPECT_FALSE(dict_n(TSStr, TInt).couldBe(dict_val(staticEmptyDictArray())));

  EXPECT_TRUE(sdict_packedn(TInt).couldBe(sdict_n(TInt, TInt)));
  EXPECT_FALSE(sdict_packedn(TInt).couldBe(dict_n(TInt, TObj)));

  EXPECT_TRUE(tstruct.couldBe(sdict_n(TSStr, TInt)));
  EXPECT_FALSE(tstruct.couldBe(dict_n(TSStr, TObj)));
}

TEST_F(TypeTest, DictEquivalentRepresentations) {
  {
    auto const simple = dict_val(static_dict(0, 42, 1, 23, 2, 12));
    auto const bulky  = sdict_packed({ival(42), ival(23), ival(12)});
    EXPECT_NE(simple, bulky);
    EXPECT_TRUE(simple.subtypeOf(bulky));
  }

  {
    auto const simple =
      dict_val(static_dict(s_A.get(), s_B.get(), s_test.get(), 12));

    auto const map    = MapElems{map_elem(s_A, sval(s_B)), map_elem(s_test, ival(12))};
    auto const bulky  = sdict_map(map);

    EXPECT_NE(simple, bulky);
    EXPECT_TRUE(simple.subtypeOf(bulky));
  }
}

TEST_F(TypeTest, DictUnions) {
  auto const test_map_a    = MapElems{map_elem(s_test, ival(2))};
  auto const tstruct       = sdict_map(test_map_a);

  auto const test_map_b    = MapElems{map_elem(s_test, TInt)};
  auto const tstruct2      = sdict_map(test_map_b);

  auto const test_map_c    = MapElems{map_elem(s_A, TInt)};
  auto const tstruct3      = sdict_map(test_map_c);

  auto const test_map_d    = MapElems{map_elem(s_A, TInt), map_elem(s_test, TDbl)};
  auto const tstruct4      = sdict_map(test_map_d);

  auto const packed_int = dict_packedn(TInt);

  EXPECT_EQ(union_of(tstruct, packed_int),
            dict_n(union_of(sval(s_test), TInt), TInt));
  EXPECT_EQ(union_of(tstruct, tstruct2), tstruct2);
  EXPECT_EQ(union_of(tstruct, tstruct3), sdict_n(TSStr, TInt));
  EXPECT_EQ(union_of(tstruct, tstruct4), sdict_n(TSStr, TNum));

  EXPECT_EQ(union_of(sdict_packed({TInt, TDbl, TDbl}), sdict_packedn(TDbl)),
            sdict_packedn(TNum));
  EXPECT_EQ(union_of(sdict_packed({TInt, TDbl}), tstruct),
            sdict_n(union_of(sval(s_test), TInt), TNum));

  EXPECT_EQ(union_of(dict_n(TInt, TTrue), dict_n(TStr, TFalse)),
            dict_n(TArrKey, TBool));

  auto const dict_val1 = dict_val(static_dict(0, 42, 1, 23, 2, 12));
  auto const dict_val2 = dict_val(static_dict(0, 1, 1, 2, 2, 3, 3, 4, 4, 5));
  EXPECT_EQ(
    loosen_mark_for_testing(union_of(dict_val1, dict_val2)),
    loosen_mark_for_testing(sdict_packedn(TInt))
  );
}

TEST_F(TypeTest, DictIntersections) {
  auto const test_map_a    = MapElems{map_elem(s_test, ival(2))};
  auto const tstruct       = sdict_map(test_map_a);

  auto const test_map_b    = MapElems{map_elem(s_test, TInt)};
  auto const tstruct2      = sdict_map(test_map_b);

  auto const test_map_c    = MapElems{map_elem(s_A, TInt)};
  auto const tstruct3      = sdict_map(test_map_c);

  auto const test_map_d    = MapElems{map_elem(s_A, TInt), map_elem(s_test, TDbl)};
  auto const tstruct4      = sdict_map(test_map_d);

  auto const test_map_e    = MapElems{map_elem(s_A, TInt), map_elem(s_B, TDbl)};
  auto const tstruct5      = sdict_map(test_map_e);

  auto const test_map_f    = MapElems{map_elem(s_A, TUncArrKey), map_elem(s_B, TInt)};
  auto const tstruct6      = sdict_map(test_map_f);

  auto const test_map_g    = MapElems{map_elem(s_A, TSStr), map_elem(s_B, TUncArrKey)};
  auto const tstruct7      = sdict_map(test_map_g);

  auto const test_map_h    = MapElems{map_elem(s_A, TSStr), map_elem(s_B, TInt)};
  auto const tstruct8      = sdict_map(test_map_h);

  auto const test_map_i    = MapElems{map_elem(s_A, TStr), map_elem(s_B, TInt), map_elem(s_BB, TVec)};
  auto const tstruct9      = dict_map(test_map_i);

  auto const test_map_j    = MapElems{map_elem(s_A, TSStr), map_elem(s_B, TInt), map_elem(s_BB, TSVec)};
  auto const tstruct10     = sdict_map(test_map_j);

  auto const test_map_k    = MapElems{map_elem(s_A, TSStr), map_elem(s_B, TInt), map_elem(s_BB, TObj)};
  auto const tstruct11     = dict_map(test_map_k);

  auto const mapn_str_int = dict_n(TStr, TInt);

  EXPECT_EQ(intersection_of(tstruct,  mapn_str_int), tstruct);
  EXPECT_EQ(intersection_of(tstruct2, mapn_str_int), tstruct2);
  EXPECT_EQ(intersection_of(tstruct3, mapn_str_int), tstruct3);
  EXPECT_EQ(intersection_of(tstruct4, mapn_str_int), TBottom);
  EXPECT_EQ(intersection_of(tstruct,  tstruct2),     tstruct);
  EXPECT_EQ(intersection_of(tstruct,  tstruct3),     TBottom);
  EXPECT_EQ(intersection_of(tstruct4, tstruct5),     TBottom);
  EXPECT_EQ(intersection_of(tstruct6, tstruct7),     tstruct8);
  EXPECT_EQ(intersection_of(tstruct8, tstruct),      TBottom);

  EXPECT_EQ(intersection_of(sdict_packed({TNum, TDbl, TNum}),
                            sdict_packedn(TDbl)),
            sdict_packed({TDbl, TDbl, TDbl}));
  EXPECT_EQ(intersection_of(sdict_packed({TNum, TDbl, TNum}),
                            sdict_packed({TDbl, TNum, TInt})),
            sdict_packed({TDbl, TDbl, TInt}));
  EXPECT_EQ(intersection_of(TSDictN, dict_n(TArrKey, TInitCell)),
            sdict_n(TUncArrKey, TInitUnc));
  EXPECT_EQ(intersection_of(TSDictN, dict_n(TArrKey, TInitCell)),
            sdict_n(TUncArrKey, TInitUnc));
  EXPECT_EQ(intersection_of(tstruct9, TSDictN), tstruct10);
  EXPECT_EQ(intersection_of(TSDictN, dict_packed({TStr, TVec, TInt, TInitCell})),
                            sdict_packed({TSStr, TSVec, TInt, TInitUnc}));
  EXPECT_EQ(intersection_of(dict_packedn(TStr), TSDictN), sdict_packedn(TSStr));
  EXPECT_EQ(intersection_of(TSDictN, dict_packedn(TObj)), TBottom);
  EXPECT_EQ(intersection_of(dict_packed({TStr, TInt, TObj}), TSDictN), TBottom);
  EXPECT_EQ(intersection_of(TSDictN, tstruct11), TBottom);
  EXPECT_EQ(
    intersection_of(dict_n(TArrKey, TObj), TSDictN),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(union_of(dict_n(TInt, TObj), TDictE),
                    union_of(dict_packed({TInt, TObj}), TDictE)),
    TDictE
  );
  EXPECT_EQ(
    intersection_of(opt(dict_n(TInt, TObj)), TUnc),
    TInitNull
  );
  EXPECT_EQ(
    intersection_of(opt(dict_packedn(TObj)), TInitUnc),
    TInitNull
  );
  EXPECT_EQ(
    intersection_of(union_of(dict_packed({TInt, TObj}), TDictE), TUnc),
    TSDictE
  );
  EXPECT_EQ(
    intersection_of(opt(union_of(dict_packed({TInt, TObj}), TDictE)), TUnc),
    opt(TSDictE)
  );
}

TEST_F(TypeTest, DictOfDict) {
  auto const t1 = dict_n(TSStr, dict_n(TInt, TSStr));
  auto const t2 = dict_n(TSStr, TDict);
  auto const t3 = dict_n(TSStr, dict_packedn(TSStr));
  auto const t4 = dict_n(TSStr, dict_n(TSStr, TSStr));
  EXPECT_TRUE(t1.subtypeOf(t2));
  EXPECT_TRUE(t1.couldBe(t3));
  EXPECT_FALSE(t1.subtypeOf(t3));
  EXPECT_TRUE(t3.subtypeOf(t1));
  EXPECT_TRUE(t3.subtypeOf(t2));
  EXPECT_FALSE(t1.couldBe(t4));
  EXPECT_FALSE(t4.couldBe(t3));
  EXPECT_TRUE(t4.subtypeOf(t2));
}

TEST_F(TypeTest, WideningAlreadyStable) {
  // A widening union on types that are already stable should not move
  // the type anywhere.
  auto const program = make_test_program();
  Index index { program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    EXPECT_EQ(widening_union(t, t), t);
  }

  auto deepPacked = svec({TInt});
  auto deepPackedN = svec_n(TInt);
  auto deepMap = sdict_map({map_elem(s_A, TInt)});
  auto deepMapN = sdict_n(TInt, TInt);
  for (size_t i = 0; i < 10; ++i) {
    deepPacked = widening_union(deepPacked, svec({deepPacked}));
    deepPackedN = widening_union(deepPackedN, svec_n(deepPackedN));
    deepMap = widening_union(deepMap, sdict_map({map_elem(s_A, deepMap)}, TSStr, deepMap));
    deepMapN = widening_union(deepMapN, sdict_n(TInt, deepMapN));
  }
  EXPECT_EQ(deepPacked, widening_union(deepPacked, svec({deepPacked})));
  EXPECT_EQ(deepPackedN, widening_union(deepPackedN, svec_n(deepPackedN)));
  EXPECT_EQ(deepMap, widening_union(deepMap, sdict_map({map_elem(s_A, deepMap)}, TSStr, deepMap)));
  EXPECT_EQ(deepMapN, widening_union(deepMapN, sdict_n(TInt, deepMapN)));
}

TEST_F(TypeTest, EmptyDict) {
  {
    auto const possible_e = union_of(dict_packedn(TInt), dict_empty());
    EXPECT_TRUE(possible_e.couldBe(dict_empty()));
    EXPECT_TRUE(possible_e.couldBe(dict_packedn(TInt)));
    EXPECT_EQ(array_like_elem(possible_e, ival(0)).first, TInt);
  }

  {
    auto const possible_e = union_of(dict_packed({TInt, TInt}), dict_empty());
    EXPECT_TRUE(possible_e.couldBe(dict_empty()));
    EXPECT_TRUE(possible_e.couldBe(dict_packed({TInt, TInt})));
    EXPECT_FALSE(possible_e.couldBe(dict_packed({TInt, TInt, TInt})));
    EXPECT_FALSE(possible_e.subtypeOf(dict_packedn(TInt)));
    EXPECT_EQ(array_like_elem(possible_e, ival(0)).first, TInt);
    EXPECT_EQ(array_like_elem(possible_e, ival(1)).first, TInt);
  }

  {
    auto const estat = union_of(sdict_packedn(TInt), dict_empty());
    EXPECT_TRUE(estat.couldBe(dict_empty()));
    EXPECT_TRUE(estat.couldBe(sdict_packedn(TInt)));
    EXPECT_FALSE(estat.subtypeOf(sdict_packedn(TInt)));
    EXPECT_FALSE(estat.subtypeOf(BSDictE));
    EXPECT_TRUE(estat.couldBe(BSDictE));
  }

  EXPECT_EQ(
    loosen_mark_for_testing(array_like_newelem(dict_empty(), ival(142)).first),
    loosen_mark_for_testing(dict_packed({ival(142)}))
  );
}

TEST_F(TypeTest, ArrKey) {
  EXPECT_TRUE(TInt.subtypeOf(BArrKey));
  EXPECT_TRUE(TStr.subtypeOf(BArrKey));
  EXPECT_TRUE(ival(0).subtypeOf(BArrKey));
  EXPECT_TRUE(sval(s_test).subtypeOf(BArrKey));
  EXPECT_TRUE(sval_nonstatic(s_test).subtypeOf(BArrKey));

  EXPECT_TRUE(TInt.subtypeOf(BUncArrKey));
  EXPECT_TRUE(TSStr.subtypeOf(BUncArrKey));
  EXPECT_TRUE(ival(0).subtypeOf(BUncArrKey));
  EXPECT_TRUE(sval(s_test).subtypeOf(BUncArrKey));

  EXPECT_TRUE(TArrKey.subtypeOf(BInitCell));
  EXPECT_TRUE(TUncArrKey.subtypeOf(BInitCell));
  EXPECT_TRUE(TOptArrKey.subtypeOf(BInitCell));
  EXPECT_TRUE(TOptUncArrKey.subtypeOf(BInitCell));

  EXPECT_TRUE(TUncArrKey.subtypeOf(BInitUnc));
  EXPECT_TRUE(TOptUncArrKey.subtypeOf(BInitUnc));

  EXPECT_EQ(union_of(TInt, TStr), TArrKey);
  EXPECT_EQ(union_of(TInt, TSStr), TUncArrKey);
  EXPECT_EQ(union_of(TArrKey, TInitNull), TOptArrKey);
  EXPECT_EQ(union_of(TUncArrKey, TInitNull), TOptUncArrKey);

  EXPECT_EQ(opt(TArrKey), TOptArrKey);
  EXPECT_EQ(opt(TUncArrKey), TOptUncArrKey);
  EXPECT_EQ(unopt(TOptArrKey), TArrKey);
  EXPECT_EQ(unopt(TOptUncArrKey), TUncArrKey);
}

TEST_F(TypeTest, LoosenStaticness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);

  for (auto const& t : all) {
    if (!t.couldBe(BStr | BArrLike)) {
      EXPECT_EQ(loosen_staticness(t), t);
    }

    if (!t.subtypeOf(BCell)) continue;
    auto const [obj, objRest] = split_obj(t);
    auto const [str, strRest] = split_string(objRest);
    auto const [arr, rest] = split_array_like(strRest);
    EXPECT_EQ(loosen_staticness(rest), rest);
    EXPECT_EQ(
      loosen_staticness(t),
      union_of(
        loosen_staticness(obj),
        loosen_staticness(str),
        loosen_staticness(arr),
        rest
      )
    );
    if (!t.is(BBottom)) {
      EXPECT_FALSE(loosen_staticness(t).subtypeOf(BSStr | BSArrLike));
      EXPECT_FALSE(loosen_staticness(t).subtypeOf(BCStr | BCArrLike));
    }
  }

  auto const test = [&] (const Type& a, const Type& b) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(opt(a))),
              loosen_mark_for_testing(opt(b)));
    if (a.strictSubtypeOf(BInitCell)) {
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(wait_handle(index, a))),
                loosen_mark_for_testing(wait_handle(index, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_packedn(a))),
                loosen_mark_for_testing(dict_packedn(b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_packed({a}))),
                loosen_mark_for_testing(dict_packed({b})));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_n(TSStr, a))),
                loosen_mark_for_testing(dict_n(TStr, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_map({map_elem(s_A, a)}, TInt, a))),
                loosen_mark_for_testing(dict_map({map_elem_nonstatic(s_A, b)}, TInt, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_map({map_elem(s_A, a)}, TSStr, a))),
                loosen_mark_for_testing(dict_map({map_elem_nonstatic(s_A, b)}, TStr, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(dict_map({map_elem_counted(s_A, a)}, TSStr, a))),
                loosen_mark_for_testing(dict_map({map_elem_nonstatic(s_A, b)}, TStr, b)));
    }
    if (a.strictSubtypeOf(BUnc)) {
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_packedn(a))),
                loosen_mark_for_testing(dict_packedn(b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_packedn(a))),
                loosen_mark_for_testing(dict_packedn(b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_packed({a}))),
                loosen_mark_for_testing(dict_packed({b})));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_n(TSStr, a))),
                loosen_mark_for_testing(dict_n(TStr, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_map({map_elem(s_A, a)}, TInt, a))),
                loosen_mark_for_testing(dict_map({map_elem_nonstatic(s_A, b)}, TInt, b)));
      EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(sdict_map({map_elem(s_A, a)}, TSStr, a))),
                loosen_mark_for_testing(dict_map({map_elem_nonstatic(s_A, b)}, TStr, b)));
    }
  };

  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BInitCell)) continue;
    test(t, loosen_staticness(t));
  }

  auto const uncClsMeth = use_lowptr ? BClsMeth : BBottom;

  auto const test_map1 = MapElems{map_elem(s_A, TInt)};
  auto const test_map2 = MapElems{map_elem_nonstatic(s_A, TInt)};
  auto const test_map3 = MapElems{map_elem_counted(s_A, TInt)};
  std::vector<std::pair<Type, Type>> tests = {
    { TSStr, TStr },
    { TSVecishE, TVecishE },
    { TSVecishN, TVecishN },
    { TSVecish, TVecish },
    { TSDictishE, TDictishE },
    { TSDictishN, TDictishN },
    { TSDictish, TDictish },
    { TSVArrE, TVArrE },
    { TSVArrN, TVArrN },
    { TSVArr, TVArr },
    { TSDArrE, TDArrE },
    { TSDArrN, TDArrN },
    { TSDArr, TDArr },
    { TSVecE, TVecE },
    { TSVecN, TVecN },
    { TSVec, TVec },
    { TSDictE, TDictE },
    { TSDictN, TDictN },
    { TSDict, TDict },
    { TSKeysetE, TKeysetE },
    { TSKeysetN, TKeysetN },
    { TSKeyset, TKeyset },
    { TUncArrKey, TArrKey },
    { TUnc,
      Type{BInitNull|BArrLike|BArrKey|BBool|BCls|BDbl|BFunc|BLazyCls|BUninit|uncClsMeth} },
    { TInitUnc,
      Type{BInitNull|BArrLike|BArrKey|BBool|BCls|BDbl|BFunc|BLazyCls|uncClsMeth} },
    { ival(123), ival(123) },
    { sval(s_test), sval_nonstatic(s_test) },
    { sdict_packedn(TInt), dict_packedn(TInt) },
    { sdict_packed({TInt, TBool}), dict_packed({TInt, TBool}) },
    { sdict_n(TSStr, TInt), dict_n(TStr, TInt) },
    { sdict_n(TInt, TSDictN), dict_n(TInt, TDictN) },
    { sdict_map(test_map1), dict_map(test_map2) },
    { dict_map(test_map3), dict_map(test_map2) },
    { TClsMeth, TClsMeth },
    { TObj, TObj },
    { TRes, TRes },
    { TInitCell, TInitCell },
    { vec_n(Type{BInitCell & ~BCStr}), TVecN },
    { dict_n(TArrKey, Type{BInitCell & ~BCStr}), TDictN },
    { dict_n(Type{BInt|BSStr}, TInitCell), TDictN },
    { wait_handle(index, Type{BInitCell & ~BCStr}), wait_handle(index, TInitCell) },
    { vec_val(static_vec(s_A.get(), 123, s_B.get(), 456)),
      vec({sval_nonstatic(s_A), ival(123), sval_nonstatic(s_B), ival(456)}) },
    { sdict_map({map_elem(s_A, TSStr)}, TSStr, TSStr), dict_map({map_elem_nonstatic(s_A, TStr)}, TStr, TStr) },
    { dict_val(static_dict(s_A.get(), s_A.get(), s_B.get(), s_B.get())),
      dict_map({map_elem_nonstatic(s_A, sval_nonstatic(s_A)), map_elem_nonstatic(s_B, sval_nonstatic(s_B))}) },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_staticness(p.first)),
              loosen_mark_for_testing(p.second));
    test(p.first, p.second);
  }
}

TEST_F(TypeTest, LoosenStringStaticness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.couldBe(BStr)) {
      EXPECT_EQ(loosen_string_staticness(t), t);
    } else {
      EXPECT_FALSE(loosen_string_staticness(t).subtypeAmong(BSStr, BStr));
      EXPECT_FALSE(loosen_string_staticness(t).subtypeAmong(BCStr, BStr));
    }

    if (!t.subtypeOf(BCell)) continue;
    auto const [str, rest] = split_string(t);
    EXPECT_EQ(loosen_string_staticness(rest), rest);
    EXPECT_EQ(
      loosen_string_staticness(t),
      union_of(loosen_string_staticness(str), rest)
    );
  }

  const std::vector<std::pair<Type, Type>> tests = {
    { TSStr, TStr },
    { TCStr, TStr },
    { TStr, TStr },
    { sval(s_A), sval_nonstatic(s_A) },
    { sval_counted(s_A), sval_nonstatic(s_A) },
    { sval_nonstatic(s_A), sval_nonstatic(s_A) },
    { TUncArrKey, TArrKey },
    { TArrKey, TArrKey },
    { union_of(TCStr,TInt), TArrKey },
    { TInt, TInt },
    { TObj, TObj },
    { TSArrLike, TSArrLike },
    { TCArrLike, TCArrLike },
    { TCell, TCell },
    { ival(1), ival(1) },
    { union_of(sval(s_A),TInt), union_of(sval_nonstatic(s_A),TInt) },
    { union_of(sval_counted(s_A),TInt), union_of(sval_nonstatic(s_A),TInt) },
    { union_of(sval_nonstatic(s_A),TInt), union_of(sval_nonstatic(s_A),TInt) },
    { union_of(ival(1),TSStr), union_of(ival(1),TStr) },
    { union_of(ival(1),TCStr), union_of(ival(1),TStr) },
    { union_of(ival(1),TStr), union_of(ival(1),TStr) },
    { TInitUnc, Type{(BInitUnc & ~BSStr) | BStr} },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_string_staticness(p.first), p.second);
  }
}

TEST_F(TypeTest, LoosenArrayStaticness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);

  for (auto const& t : all) {
    if (!t.couldBe(BArrLike)) {
      EXPECT_EQ(loosen_array_staticness(t), t);
    } else {
      EXPECT_FALSE(loosen_array_staticness(t).subtypeAmong(BSArrLike, BArrLike));
      EXPECT_FALSE(loosen_array_staticness(t).subtypeAmong(BCArrLike, BArrLike));
    }

    if (!t.subtypeOf(BCell)) continue;
    auto const [arr, rest] = split_array_like(t);
    EXPECT_EQ(loosen_array_staticness(rest), rest);
    EXPECT_EQ(
      loosen_array_staticness(t),
      union_of(loosen_array_staticness(arr), rest)
    );
  }

  for (auto const& t : all) {
    if (t.is(BBottom) || !t.subtypeOf(BInitCell)) continue;

    EXPECT_EQ(loosen_array_staticness(opt(t)), opt(loosen_array_staticness(t)));

    if (t.strictSubtypeOf(BInitCell)) {
      EXPECT_EQ(loosen_array_staticness(wait_handle(index, t)), wait_handle(index, t));
      EXPECT_EQ(loosen_array_staticness(dict_packedn(t)), dict_packedn(t));
      EXPECT_EQ(loosen_array_staticness(dict_packed({t})), dict_packed({t}));
      EXPECT_EQ(loosen_array_staticness(dict_n(TSStr, t)), dict_n(TSStr, t));
      EXPECT_EQ(loosen_array_staticness(dict_map({map_elem(s_A, t)}, TSStr, t)),
                dict_map({map_elem(s_A, t)}, TSStr, t));
      EXPECT_EQ(loosen_array_staticness(dict_map({map_elem_counted(s_A, t)}, TSStr, t)),
                dict_map({map_elem_counted(s_A, t)}, TSStr, t));
    }
    if (t.strictSubtypeOf(BUnc)) {
      EXPECT_EQ(loosen_array_staticness(sdict_packedn(t)), dict_packedn(t));
      EXPECT_EQ(loosen_array_staticness(sdict_packed({t})), dict_packed({t}));
      EXPECT_EQ(loosen_array_staticness(sdict_n(TSStr, t)), dict_n(TSStr, t));
      EXPECT_EQ(loosen_array_staticness(sdict_map({map_elem(s_A, t)}, TSStr, t)),
                dict_map({map_elem(s_A, t)}, TSStr, t));
    }
  }

  auto const test_map1 = MapElems{map_elem(s_A, TInt)};
  auto const test_map2 = MapElems{map_elem_nonstatic(s_A, TInt)};
  auto const test_map3 = MapElems{map_elem_counted(s_A, TInt)};
  std::vector<std::pair<Type, Type>> tests = {
    { TSStr, TSStr },
    { TCStr, TCStr},
    { TSVecishE, TVecishE },
    { TSVecishN, TVecishN },
    { TSVecish, TVecish },
    { TSDictishE, TDictishE },
    { TSDictishN, TDictishN },
    { TSDictish, TDictish },
    { TSVArrE, TVArrE },
    { TSVArrN, TVArrN },
    { TSVArr, TVArr },
    { TSDArrE, TDArrE },
    { TSDArrN, TDArrN },
    { TSDArr, TDArr },
    { TSVecE, TVecE },
    { TSVecN, TVecN },
    { TSVec, TVec },
    { TSDictE, TDictE },
    { TSDictN, TDictN },
    { TSDict, TDict },
    { TSKeysetE, TKeysetE },
    { TSKeysetN, TKeysetN },
    { TSKeyset, TKeyset },
    { TSArrLike, TArrLike },
    { TCArrLike, TArrLike },
    { TUncArrKey, TUncArrKey },
    { Type{BSVec|BInt}, Type{BVec|BInt} },
    { TUnc, Type{(BUnc & ~BSArrLike) | BArrLike} },
    { TInitUnc, Type{(BInitUnc & ~BSArrLike) | BArrLike} },
    { ival(123), ival(123) },
    { sval(s_test), sval(s_test) },
    { sdict_packedn(TInt), dict_packedn(TInt) },
    { sdict_packed({TInt, TBool}), dict_packed({TInt, TBool}) },
    { sdict_n(TSStr, TInt), dict_n(TSStr, TInt) },
    { sdict_n(TInt, TSDictN), dict_n(TInt, TSDictN) },
    { sdict_map(test_map1), dict_map(test_map1) },
    { dict_map(test_map2), dict_map(test_map2) },
    { dict_map(test_map3), dict_map(test_map3) },
    { TClsMeth, TClsMeth },
    { TObj, TObj },
    { TRes, TRes },
    { TInitCell, TInitCell },
    { vec_n(Type{BInitCell & ~BCArrLike}), vec_n(Type{BInitCell & ~BCArrLike}) },
    { dict_n(TArrKey, Type{BInitCell & ~BCArrLike}), dict_n(TArrKey, Type{BInitCell & ~BCArrLike}) },
    { wait_handle(index, Type{BInitCell & ~BCArrLike}), wait_handle(index, Type{BInitCell & ~BCArrLike}) },
    { vec_val(static_vec(s_A.get(), 123, s_B.get(), 456)),
      vec({sval(s_A), ival(123), sval(s_B), ival(456)}) },
    { sdict_map({map_elem(s_A, TSStr)}, TSStr, TSStr), dict_map({map_elem(s_A, TSStr)}, TSStr, TSStr) },
    { dict_val(static_dict(s_A.get(), s_A.get(), s_B.get(), s_B.get())),
      dict_map({map_elem(s_A, sval(s_A)), map_elem(s_B, sval(s_B))}) },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_array_staticness(p.first)),
              loosen_mark_for_testing(p.second));
  }
}

TEST_F(TypeTest, Emptiness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  std::vector<std::pair<Type, Emptiness>> tests{
    { TInitNull, Emptiness::Empty },
    { TUninit, Emptiness::Empty },
    { TFalse, Emptiness::Empty },
    { TVecE, Emptiness::Empty },
    { TSKeysetE, Emptiness::Empty },
    { TDictishE, Emptiness::Empty },
    { TDictish, Emptiness::Maybe },
    { TTrue, Emptiness::NonEmpty },
    { TVecN, Emptiness::NonEmpty },
    { TDictishN, Emptiness::NonEmpty },
    { TArrLikeN, Emptiness::NonEmpty },
    { TArrLike, Emptiness::Maybe },
    { TObj, Emptiness::Maybe },
    { wait_handle(index, TInt), Emptiness::NonEmpty },
    { ival(0), Emptiness::Empty },
    { ival(1), Emptiness::NonEmpty },
    { opt(ival(0)), Emptiness::Empty },
    { opt(ival(1)), Emptiness::Maybe },
    { sempty(), Emptiness::Empty },
    { sval(s_A), Emptiness::NonEmpty },
    { dval(3.14), Emptiness::NonEmpty },
    { dval(0), Emptiness::Empty },
    { TInitCell, Emptiness::Maybe },
    { TInt, Emptiness::Maybe },
    { TStr, Emptiness::Maybe },
    { TDbl, Emptiness::Maybe }
  };
  for (auto const& p : tests) {
    EXPECT_EQ(emptiness(p.first), p.second);
  }
}

TEST_F(TypeTest, AssertNonEmptiness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.subtypeOf(BCell)) continue;

    switch (emptiness(t)) {
      case Emptiness::Empty:
        EXPECT_EQ(assert_nonemptiness(t), TBottom);
        break;
      case Emptiness::Maybe:
        EXPECT_NE(emptiness(assert_nonemptiness(t)), Emptiness::Empty);
        break;
      case Emptiness::NonEmpty:
        EXPECT_EQ(assert_nonemptiness(t), t);
        break;
    }

    if (!is_specialized_int(t) &&
        !is_specialized_double(t) &&
        !is_specialized_string(t) &&
        !t.couldBe(BNull | BFalse | BArrLikeE)) {
      EXPECT_EQ(assert_nonemptiness(t), t);
    }
    EXPECT_FALSE(assert_nonemptiness(t).couldBe(BNull | BFalse | BArrLikeE));
  }

  std::vector<std::pair<Type, Type>> tests{
    { TInitNull, TBottom },
    { TUninit, TBottom },
    { TFalse, TBottom },
    { TTrue, TTrue },
    { TBool, TTrue },
    { TVecE, TBottom },
    { TVec, TVecN },
    { TVecN, TVecN },
    { TDictishE, TBottom },
    { TDictishN, TDictishN },
    { TDictish, TDictishN },
    { TArrLikeE, TBottom },
    { TArrLikeN, TArrLikeN },
    { TArrLike, TArrLikeN },
    { TObj, TObj },
    { Type{BInt|BFalse}, TInt },
    { wait_handle(index, TInt), wait_handle(index, TInt) },
    { ival(0), TBottom },
    { ival(1), ival(1) },
    { sempty(), TBottom },
    { sval(s_A), sval(s_A) },
    { dval(3.14), dval(3.14) },
    { dval(0), TBottom },
    { opt(ival(0)), TBottom },
    { opt(ival(1)), ival(1) },
    { TInitCell, Type{BInitCell & ~(BNull | BFalse | BArrLikeE)} },
    { TInt, TInt },
    { TStr, TStr },
    { TDbl, TDbl },
    { union_of(ival(1),TStr), union_of(ival(1),TStr) },
    { union_of(ival(0),TStr), TArrKey },
    { union_of(ival(0),TDictE), TBottom }
  };
  for (auto const& p : tests) {
    EXPECT_EQ(assert_nonemptiness(p.first), p.second);
  }
}

TEST_F(TypeTest, AssertEmptiness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.subtypeOf(BCell)) continue;

    switch (emptiness(t)) {
      case Emptiness::Empty:
        EXPECT_EQ(assert_emptiness(t), t);
        break;
      case Emptiness::Maybe:
        EXPECT_NE(emptiness(assert_emptiness(t)), Emptiness::NonEmpty);
        break;
      case Emptiness::NonEmpty:
        EXPECT_EQ(assert_emptiness(t), TBottom);
        break;
    }

    EXPECT_EQ(t.couldBe(BInitNull), assert_emptiness(t).couldBe(BInitNull));
    EXPECT_FALSE(assert_emptiness(t).couldBe(BTrue | BArrLikeN));
  }

  std::vector<std::pair<Type, Type>> tests{
    { TInitNull, TInitNull },
    { TUninit, TUninit },
    { TFalse, TFalse },
    { TTrue, TBottom },
    { TBool, TFalse },
    { TVecE, TVecE },
    { TVec, TVecE },
    { TVecN, TBottom },
    { TDictishE, TDictishE },
    { TDictishN, TBottom },
    { TDictish, TDictishE },
    { TArrLikeE, TArrLikeE },
    { TArrLikeN, TBottom },
    { TArrLike, TArrLikeE },
    { TObj, TObj },
    { Type{BInt|BFalse}, union_of(ival(0),TFalse) },
    { Type{BInt|BTrue}, ival(0) },
    { Type{BInt|BBool}, union_of(ival(0),TFalse) },
    { wait_handle(index, TInt), TBottom },
    { ival(0), ival(0) },
    { ival(1), TBottom },
    { sempty(), sempty() },
    { sempty_nonstatic(), sempty_nonstatic() },
    { sval(s_A), TBottom },
    { dval(3.14), TBottom },
    { dval(0), dval(0) },
    { opt(ival(0)), opt(ival(0)) },
    { opt(ival(1)), TInitNull },
    { TInt, ival(0) },
    { TStr, sempty_nonstatic() },
    { TSStr, sempty() },
    { TDbl, dval(0) },
    { union_of(ival(1),TStr), TArrKey },
    { union_of(ival(0),TStr), union_of(TInt,sempty_nonstatic()) },
    { union_of(ival(0),TDictE), union_of(ival(0),TDictE) },
    { union_of(ival(0),TDictN), ival(0) },
    { dict_n(TArrKey, TInt), TBottom },
    { union_of(dict_n(TArrKey, TInt),TDictE), TDictE }
  };
  for (auto const& p : tests) {
    EXPECT_EQ(assert_emptiness(p.first), p.second);
  }
}

TEST_F(TypeTest, LoosenEmptiness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);

  for (auto const& t : all) {
    if (!t.couldBe(BArrLike)) {
      EXPECT_EQ(loosen_emptiness(t), t);
    } else {
      EXPECT_FALSE(loosen_emptiness(t).subtypeAmong(BArrLikeE, BArrLike));
      EXPECT_FALSE(loosen_emptiness(t).subtypeAmong(BArrLikeN, BArrLike));
    }

    EXPECT_EQ(t.hasData(), loosen_emptiness(t).hasData());

    if (!t.subtypeOf(BCell)) continue;
    auto const [arr, rest] = split_array_like(t);
    EXPECT_EQ(
      loosen_emptiness(t),
      union_of(loosen_emptiness(arr), loosen_emptiness(rest))
    );
  }

  auto const test_map    = MapElems{map_elem(s_A, TInt)};
  std::vector<std::pair<Type, Type>> tests = {
    { TSVArrE, TSVArr },
    { TSVArrN, TSVArr },
    { TVArrE, TVArr },
    { TVArrN, TVArr },
    { TSDArrE, TSDArr },
    { TSDArrN, TSDArr },
    { TDArrE, TDArr },
    { TDArrN, TDArr },
    { TSVecE, TSVec },
    { TSVecN, TSVec },
    { TVecE, TVec },
    { TVecN, TVec },
    { TSDictE, TSDict },
    { TSDictN, TSDict },
    { TDictE, TDict },
    { TDictN, TDict },
    { TSKeysetE, TSKeyset },
    { TSKeysetN, TSKeyset },
    { TKeysetE, TKeyset },
    { TKeysetN, TKeyset },
    { TSVecishE, TSVecish },
    { TSVecishN, TSVecish },
    { TVecishE, TVecish },
    { TVecishN, TVecish },
    { TSDictishE, TSDictish },
    { TSDictishN, TSDictish },
    { TDictishE, TDictish },
    { TDictishN, TDictish },
    { dict_packedn(TInt), union_of(TDictE, dict_packedn(TInt)) },
    { dict_packed({TInt, TBool}), union_of(TDictE, dict_packed({TInt, TBool})) },
    { dict_n(TStr, TInt), union_of(TDictE, dict_n(TStr, TInt)) },
    { dict_map(test_map), union_of(TDictE, dict_map(test_map)) },
    { TSArrLikeE, TSArrLike },
    { TSArrLikeN, TSArrLike },
    { TArrLikeE, TArrLike },
    { TArrLikeN, TArrLike },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_emptiness(p.first)),
              loosen_mark_for_testing(p.second));
    EXPECT_EQ(loosen_mark_for_testing(loosen_emptiness(opt(p.first))),
              loosen_mark_for_testing(opt(p.second)));
  }
}

TEST_F(TypeTest, LoosenValues) {
  auto const program = make_test_program();
  auto const unit = program->units.back().get();
    auto const func = [&]() -> php::Func* {
    for (auto& f : unit->funcs) {
      if (f->name->isame(s_test.get())) return f.get();
    }
    return nullptr;
  }();
  EXPECT_TRUE(func != nullptr);

  auto const ctx = Context { unit, func };
  Index index{ program.get() };

  auto const& all = allCases(index);

  for (auto const& t : all) {
    if (t.couldBe(BBool)) {
      EXPECT_FALSE(loosen_values(t).subtypeAmong(BFalse, BBool));
      EXPECT_FALSE(loosen_values(t).subtypeAmong(BTrue, BBool));
      EXPECT_EQ(get_bits(t) & ~BBool, get_bits(loosen_values(t)) & ~BBool);
    } else {
      EXPECT_EQ(get_bits(t), get_bits(loosen_values(t)));
    }

    if (is_specialized_string(t) ||
        is_specialized_int(t) ||
        is_specialized_double(t) ||
        is_specialized_array_like(t)) {
      EXPECT_FALSE(loosen_values(t).hasData());
    } else if (!t.couldBe(BBool)) {
      EXPECT_EQ(loosen_values(t), t);
    }

    EXPECT_TRUE(t.subtypeOf(loosen_values(t)));
    EXPECT_FALSE(loosen_values(t).strictSubtypeOf(t));
  }

  EXPECT_TRUE(loosen_values(TTrue) == TBool);
  EXPECT_TRUE(loosen_values(TFalse) == TBool);
  EXPECT_TRUE(loosen_values(TOptTrue) == TOptBool);
  EXPECT_TRUE(loosen_values(TOptFalse) == TOptBool);

  auto const test_map = MapElems{map_elem(s_A, TInt)};
  std::vector<std::pair<Type, Type>> tests = {
    { ival(123), TInt },
    { dval(3.14), TDbl },
    { sval(s_test), TSStr },
    { sval_nonstatic(s_test), TStr },
    { dict_val(static_dict(0, 42, 1, 23, 2, 12)), TSDictN },
    { dict_packedn(TInt), TDictN },
    { dict_packed({TInt, TBool}), TDictN },
    { dict_n(TStr, TInt), TDictN },
    { dict_map(test_map), TDictN },
    { Type{BFalse|BInt}, Type{BBool|BInt} },
    { union_of(ival(123),TTrue), Type{BInt|BBool} },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_values(p.first)), p.second);
    EXPECT_EQ(loosen_mark_for_testing(loosen_values(opt(p.first))), opt(p.second));
  }

  auto const cls = index.resolve_class(ctx, s_TestClass.get());
  EXPECT_TRUE(!!cls);
  auto const rec = index.resolve_record(s_UniqueRec.get());
  EXPECT_TRUE(rec);

  EXPECT_TRUE(loosen_values(objExact(*cls)) == objExact(*cls));
  EXPECT_TRUE(loosen_values(subObj(*cls)) == subObj(*cls));
  EXPECT_TRUE(loosen_values(clsExact(*cls)) == clsExact(*cls));
  EXPECT_TRUE(loosen_values(subCls(*cls)) == subCls(*cls));
  EXPECT_TRUE(loosen_values(exactRecord(*rec)) == exactRecord(*rec));
  EXPECT_TRUE(loosen_values(subRecord(*rec)) == subRecord(*rec));

  EXPECT_TRUE(loosen_values(opt(objExact(*cls))) == opt(objExact(*cls)));
  EXPECT_TRUE(loosen_values(opt(subObj(*cls))) == opt(subObj(*cls)));
}

TEST_F(TypeTest, LoosenArrayValues) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    EXPECT_EQ(get_bits(t), get_bits(loosen_array_values(t)));
    if (!is_specialized_array_like(t)) {
      EXPECT_EQ(loosen_array_values(t), t);
    }
    EXPECT_FALSE(is_specialized_array_like(loosen_array_values(t)));
    EXPECT_TRUE(t.subtypeOf(loosen_array_values(t)));
    EXPECT_FALSE(loosen_array_values(t).strictSubtypeOf(t));
  }
}

TEST_F(TypeTest, LoosenStringValues) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    EXPECT_EQ(get_bits(t), get_bits(loosen_string_values(t)));
    if (!is_specialized_string(t)) {
      EXPECT_EQ(loosen_string_values(t), t);
    }
    EXPECT_FALSE(is_specialized_string(loosen_string_values(t)));
    EXPECT_TRUE(t.subtypeOf(loosen_string_values(t)));
    EXPECT_FALSE(loosen_string_values(t).strictSubtypeOf(t));
  }
}

TEST_F(TypeTest, AddNonEmptiness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.couldBe(BArrLikeE)) {
      EXPECT_EQ(add_nonemptiness(t), t);
    } else {
      EXPECT_EQ(get_bits(t) & ~BArrLike,
                get_bits(add_nonemptiness(t)) & ~BArrLike);
      EXPECT_FALSE(add_nonemptiness(t).subtypeAmong(BArrLikeE, BArrLike));
      EXPECT_TRUE(add_nonemptiness(t).couldBe(BArrLikeN));
      EXPECT_TRUE(t.subtypeOf(add_nonemptiness(t)));
    }

    EXPECT_EQ(t.hasData(), add_nonemptiness(t).hasData());

    if (!t.subtypeOf(BCell)) continue;
    auto const [arr, rest] = split_array_like(t);
    EXPECT_EQ(
      add_nonemptiness(t),
      union_of(add_nonemptiness(arr), add_nonemptiness(rest))
    );
  }

  auto const test_map    = MapElems{map_elem(s_A, TInt)};
  std::vector<std::pair<Type, Type>> tests = {
    { TVArrE, TVArr },
    { TSVArrE, TSVArr },
    { TVArrN, TVArrN },
    { TSVArrN, TSVArrN },
    { TDArrE, TDArr },
    { TSDArrE, TSDArr },
    { TDArrN, TDArrN },
    { TSDArrN, TSDArrN },
    { TVecE, TVec },
    { TSVecE, TSVec },
    { TVecN, TVecN },
    { TSVecN, TSVecN },
    { TDictE, TDict },
    { TSDictE, TSDict },
    { TDictN, TDictN },
    { TSDictN, TSDictN },
    { TKeysetE, TKeyset },
    { TSKeysetE, TSKeyset },
    { TKeysetN, TKeysetN },
    { TSKeysetN, TSKeysetN },
    { TVecishE, TVecish },
    { TSVecishE, TSVecish },
    { TVecishN, TVecishN },
    { TSVecishN, TSVecishN },
    { TDictishE, TDictish },
    { TSDictishE, TSDictish },
    { TDictishN, TDictishN },
    { TSDictishN, TSDictishN },
    { TSArrLikeE, TSArrLike },
    { TArrLikeE, TArrLike },
    { TSArrLikeN, TSArrLikeN },
    { TArrLikeN, TArrLikeN },
    { dict_packedn(TInt), dict_packedn(TInt) },
    { dict_packed({TInt, TBool}), dict_packed({TInt, TBool}) },
    { dict_n(TStr, TInt), dict_n(TStr, TInt) },
    { dict_map(test_map), dict_map(test_map) },
    { vec_val(static_vec(s_A.get(), 123, s_B.get(), 456)),
      vec_val(static_vec(s_A.get(), 123, s_B.get(), 456)) },
    { TInitCell, TInitCell },
    { TObj, TObj },
    { Type{BVecE|BInt}, Type{BVec|BInt} },
    { Type{BVecN|BInt}, Type{BVecN|BInt} },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(add_nonemptiness(p.first), p.second);
    EXPECT_EQ(add_nonemptiness(opt(p.first)), opt(p.second));
  }
}

TEST_F(TypeTest, LoosenVecishOrDictish) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.couldBe(BKVish)) {
      EXPECT_EQ(loosen_vecish_or_dictish(t), t);
    } else {
      EXPECT_EQ(get_bits(t) & ~BArrLike,
                get_bits(loosen_vecish_or_dictish(t)) & ~BArrLike);
      EXPECT_TRUE(t.subtypeOf(loosen_vecish_or_dictish(t)));
    }

    if (!t.couldBe(BKeysetN)) {
      EXPECT_FALSE(is_specialized_array_like(loosen_vecish_or_dictish(t)));
    }

    if (!t.subtypeOf(BCell)) continue;
    auto const [arr, rest] = split_array_like(t);
    EXPECT_EQ(
      loosen_vecish_or_dictish(t),
      union_of(loosen_vecish_or_dictish(arr), loosen_vecish_or_dictish(rest))
    );
  }

  auto const vecOrDict = union_of(TVec, TDict);
  auto const varrOrDArr = union_of(TVArr, TDArr);
  auto const both = union_of(vecOrDict, varrOrDArr);
  std::vector<std::pair<Type, Type>> tests = {
    { TSVecE, vecOrDict },
    { TSVecN, vecOrDict },
    { TVecE, vecOrDict },
    { TVecN, vecOrDict },
    { TSVec, vecOrDict },
    { TVec, vecOrDict },
    { TSDictE, vecOrDict },
    { TSDictN, vecOrDict },
    { TDictE, vecOrDict },
    { TDictN, vecOrDict },
    { TSDict, vecOrDict },
    { TDict, vecOrDict },
    { TSVArrE, varrOrDArr },
    { TSVArrN, varrOrDArr },
    { TVArrE, varrOrDArr },
    { TVArrN, varrOrDArr },
    { TSVArr, varrOrDArr },
    { TVArr, varrOrDArr },
    { TSDArrE, varrOrDArr },
    { TSDArrN, varrOrDArr },
    { TDArrE, varrOrDArr },
    { TDArrN, varrOrDArr },
    { TSDArr, varrOrDArr },
    { TDArr, varrOrDArr },
    { TSKeysetE, TSKeysetE },
    { TSKeysetN, TSKeysetN },
    { TKeysetE, TKeysetE },
    { TKeysetN, TKeysetN },
    { TSKeyset, TSKeyset },
    { TKeyset, TKeyset },
    { TSVecishE, both },
    { TSVecishN, both },
    { TVecishE, both },
    { TVecishN, both },
    { TSVecish, both },
    { TVecish, both },
    { TSDictishE, both },
    { TSDictishN, both },
    { TDictishE, both },
    { TDictishN, both },
    { TSDictish, both },
    { TDictish, both },
    { TSArrLikeE, union_of(both, TSKeysetE) },
    { TSArrLikeN, union_of(both, TSKeysetN) },
    { TArrLikeE, union_of(both, TKeysetE) },
    { TArrLikeN, union_of(both, TKeysetN) },
    { TSArrLike, union_of(both, TSKeyset) },
    { TArrLike, TArrLike },
    { TInitCell, TInitCell },
    { TObj, TObj },
    { TInt, TInt },
    { ival(123), ival(123) },
    { dict_packedn(TInt), vecOrDict },
    { dict_packed({TInt, TBool}), vecOrDict },
    { dict_n(TStr, TInt), vecOrDict },
    { dict_map({map_elem(s_A, TInt)}), vecOrDict },
    { vec_val(static_vec(s_A.get(), 123, s_B.get(), 456)), vecOrDict },
    { Type{BVecE|BInt}, union_of(vecOrDict, TInt) },
    { Type{BVecN|BInt}, union_of(vecOrDict, TInt) },
 };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_vecish_or_dictish(p.first), p.second);
    EXPECT_EQ(loosen_vecish_or_dictish(opt(p.first)), opt(p.second));
  }
}

TEST_F(TypeTest, Scalarize) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!is_scalar(t)) continue;
    EXPECT_EQ(scalarize(t), from_cell(*tv(t)));
    EXPECT_TRUE(scalarize(t).subtypeOf(BUnc));
    EXPECT_EQ(scalarize(t).hasData(), t.hasData());
    if (!t.hasData() && !t.subtypeOf(BArrLikeE)) {
      EXPECT_EQ(scalarize(t), t);
    }
    if (is_specialized_int(t) || is_specialized_double(t)) {
      EXPECT_EQ(scalarize(t), t);
    }
    if (is_specialized_string(t)) {
      EXPECT_TRUE(scalarize(t).subtypeOf(BSStr));
    }
    if (is_specialized_array_like(t)) {
      EXPECT_TRUE(scalarize(t).subtypeOf(BSArrLikeN));
    }
  }

  std::vector<std::pair<Type, Type>> tests = {
    { TUninit, TUninit },
    { TInitNull, TInitNull },
    { TFalse, TFalse },
    { TTrue, TTrue },
    { TSVArrE, TSVArrE },
    { TVArrE, TSVArrE },
    { TSDArrE, TSDArrE },
    { TDArrE, TSDArrE },
    { TSVecE, TSVecE },
    { TVecE, TSVecE },
    { TSDictE, TSDictE },
    { TDictE, TSDictE },
    { TSKeysetE, TSKeysetE },
    { TKeysetE, TSKeysetE },
    { ival(123), ival(123) },
    { sval(s_A), sval(s_A) },
    { sval_nonstatic(s_A), sval(s_A) },
    { dval(3.14), dval(3.14) },
    { make_specialized_arrval(BSVecN, static_vec(100, 200)),
      make_specialized_arrval(BSVecN, static_vec(100, 200)) },
    { make_specialized_arrval(BSDictN, static_dict(s_A.get(), 100, s_B.get(), 200)),
      make_specialized_arrval(BSDictN, static_dict(s_A.get(), 100, s_B.get(), 200)) },
    { make_specialized_arrpacked(BVecN, {sval_nonstatic(s_A)}),
      make_specialized_arrval(BSVecN, static_vec(s_A.get())) },
    { make_specialized_arrpacked(BDictN, {sval_nonstatic(s_A)}),
      make_specialized_arrval(BSDictN, static_dict(0, s_A.get())) },
    { make_specialized_arrmap(BDictN, {map_elem_nonstatic(s_A, sval_nonstatic(s_B))}),
      make_specialized_arrval(BSDictN, static_dict(s_A.get(), s_B.get())) },
  };
  for (auto const& p : tests) {
    EXPECT_EQ(scalarize(make_unmarked(p.first)), make_unmarked(p.second));
  }
}

TEST_F(TypeTest, StrValues) {
  auto const t1 = sval(s_test);
  auto const t2 = sval_nonstatic(s_test);
  auto const t3 = sval(s_A);
  auto const t4 = sval_nonstatic(s_test);
  auto const t5 = sval_nonstatic(s_A);

  EXPECT_TRUE(t1.subtypeOf(t2));
  EXPECT_TRUE(t1.subtypeOf(TSStr));
  EXPECT_TRUE(t1.subtypeOf(TStr));
  EXPECT_FALSE(t1.subtypeOf(t3));

  EXPECT_FALSE(t2.subtypeOf(t1));
  EXPECT_FALSE(t2.subtypeOf(TSStr));
  EXPECT_TRUE(t2.subtypeOf(TStr));
  EXPECT_FALSE(t2.subtypeOf(t3));
  EXPECT_TRUE(t2.subtypeOf(t4));
  EXPECT_FALSE(t2.subtypeOf(t5));

  EXPECT_FALSE(TStr.subtypeOf(t1));
  EXPECT_FALSE(TSStr.subtypeOf(t2));
  EXPECT_FALSE(TStr.subtypeOf(t2));
  EXPECT_FALSE(TSStr.subtypeOf(t2));
  EXPECT_FALSE(t2.subtypeOf(t1));
  EXPECT_FALSE(t3.subtypeOf(t2));
  EXPECT_TRUE(t4.subtypeOf(t2));
  EXPECT_FALSE(t5.subtypeOf(t2));

  EXPECT_TRUE(t1.couldBe(t2));
  EXPECT_FALSE(t1.couldBe(t3));
  EXPECT_TRUE(t1.couldBe(TStr));
  EXPECT_TRUE(t1.couldBe(TSStr));

  EXPECT_TRUE(t2.couldBe(t1));
  EXPECT_FALSE(t2.couldBe(t3));
  EXPECT_TRUE(t2.couldBe(t4));
  EXPECT_FALSE(t2.couldBe(t5));
  EXPECT_TRUE(t2.couldBe(TStr));
  EXPECT_TRUE(t2.couldBe(TSStr));

  EXPECT_TRUE(TSStr.couldBe(t1));
  EXPECT_TRUE(TStr.couldBe(t1));
  EXPECT_TRUE(TSStr.couldBe(t2));
  EXPECT_TRUE(TStr.couldBe(t2));
  EXPECT_FALSE(t3.couldBe(t1));
  EXPECT_FALSE(t3.couldBe(t2));
  EXPECT_TRUE(t4.couldBe(t2));
  EXPECT_FALSE(t5.couldBe(t2));

  EXPECT_EQ(union_of(t1, t1), t1);
  EXPECT_EQ(union_of(t2, t2), t2);
  EXPECT_EQ(union_of(t1, t2), t2);
  EXPECT_EQ(union_of(t2, t1), t2);
  EXPECT_EQ(union_of(t1, t3), TSStr);
  EXPECT_EQ(union_of(t3, t1), TSStr);
  EXPECT_EQ(union_of(t2, t3), TStr);
  EXPECT_EQ(union_of(t3, t2), TStr);
  EXPECT_EQ(union_of(t2, t4), t2);
  EXPECT_EQ(union_of(t4, t2), t2);
  EXPECT_EQ(union_of(t2, t5), TStr);
  EXPECT_EQ(union_of(t5, t2), TStr);
}

TEST_F(TypeTest, DictMapOptValues) {
  auto const test_map_a = MapElems{map_elem(s_A, TInt), map_elem(s_B, TDbl)};
  auto const test_map_b = MapElems{map_elem(s_A, TInt)};
  auto const test_map_c = MapElems{map_elem(s_A, TInt), map_elem(s_test, TInt)};
  auto const test_map_d = MapElems{map_elem(s_test, TInt), map_elem(s_A, TInt)};
  auto const test_map_e = MapElems{map_elem(s_A, TInt), map_elem(s_B, TObj)};
  auto const test_map_f = MapElems{map_elem(10, TInt), map_elem(11, TDbl)};
  auto const test_map_g = MapElems{map_elem(s_A, TArrKey)};
  auto const test_map_h = MapElems{map_elem(s_A, TInt), map_elem(s_B, TStr)};
  auto const test_map_i = MapElems{map_elem(s_A, TInt), map_elem(s_B, TDbl), map_elem(s_test, TStr)};
  auto const test_map_j = MapElems{map_elem(s_A, TInt), map_elem(s_B, TDbl), map_elem(s_test, TObj)};

  EXPECT_EQ(dict_map(test_map_a, TInt, TSStr), dict_map(test_map_a, TInt, TSStr));
  EXPECT_NE(dict_map(test_map_a, TInt, TSStr), dict_map(test_map_a, TInt, TStr));

  EXPECT_FALSE(
    dict_map(test_map_c, TSStr, TInt).subtypeOf(dict_map(test_map_d, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_map(test_map_e, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_b, TSStr, TInt).subtypeOf(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_map(test_map_b, TSStr, TInt))
  );
  EXPECT_TRUE(
    sdict_map(test_map_a, TSStr, TInt).subtypeOf(sdict_map(test_map_b, TSStr, TNum))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_map(test_map_b, TInt, TNum))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_map(test_map_a, TStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TStr, TInt).subtypeOf(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TNum).subtypeOf(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_n(TStr, TNum))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).subtypeOf(dict_n(TStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_f, TSStr, TInt).subtypeOf(dict_n(TInt, TNum))
  );
  EXPECT_FALSE(dict_map(test_map_a).subtypeOf(dict_n(TInt, TNum)));
  EXPECT_FALSE(
    dict_n(TSStr, TInt).subtypeOf(dict_map(test_map_a, TSStr, TInt))
  );

  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_a, TSStr, TNum))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TNum).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TArrKey, TInt).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_a, TArrKey, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_a, TInt, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TInt, TInt).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a, TSStr, TDbl).couldBe(dict_map(test_map_a, TSStr, TObj))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_c, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_c, TSStr, TInt).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_a, TSStr, TInt).couldBe(dict_map(test_map_e, TSStr, TInt))
  );
  EXPECT_FALSE(
    dict_map(test_map_e, TSStr, TInt).couldBe(dict_map(test_map_a, TSStr, TInt))
  );
  EXPECT_TRUE(
    dict_map(test_map_a).couldBe(dict_map(test_map_b, TSStr, TDbl))
  );
  EXPECT_TRUE(
    dict_map(test_map_b, TSStr, TDbl).couldBe(dict_map(test_map_a))
  );
  EXPECT_FALSE(
    dict_map(test_map_a).couldBe(dict_map(test_map_b, TSStr, TObj))
  );
  EXPECT_FALSE(
    dict_map(test_map_b, TSStr, TObj).couldBe(dict_map(test_map_a))
  );

  EXPECT_EQ(
    union_of(sdict_map(test_map_a), sdict_map(test_map_b)),
    sdict_map(test_map_b, sval(s_B), TDbl)
  );
  EXPECT_EQ(
    union_of(sdict_map(test_map_a), sdict_map(test_map_c)),
    sdict_map(test_map_b, TSStr, TNum)
  );
  EXPECT_EQ(
    union_of(dict_map(test_map_a, TInt, TStr), dict_map(test_map_a, TStr, TInt)),
    dict_map(test_map_a, TArrKey, TArrKey)
  );
  EXPECT_EQ(
    union_of(dict_map(test_map_c), dict_map(test_map_d)),
    dict_n(TSStr, TInt)
  );
  EXPECT_EQ(
    union_of(dict_map(test_map_c, TInt, TInt), dict_map(test_map_d, TInt, TInt)),
    dict_n(TUncArrKey, TInt)
  );
  EXPECT_EQ(
    union_of(
      dict_map(test_map_c, TSStr, TDbl),
      dict_map(test_map_d, TSStr, TDbl)
    ),
    dict_n(TSStr, TNum)
  );
  EXPECT_EQ(
    union_of(dict_map(test_map_c, TSStr, TDbl), dict_packed({TInt})),
    dict_n(union_of(ival(0),TSStr), TNum)
  );
  EXPECT_EQ(
    union_of(sdict_map(test_map_c, TSStr, TDbl), sdict_packedn(TInt)),
    sdict_n(TUncArrKey, TNum)
  );
  EXPECT_EQ(
    union_of(dict_map(test_map_c, TInt, TDbl), dict_n(TSStr, TInt)),
    dict_n(TUncArrKey, TNum)
  );

  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TSStr, TInt),
      dict_map(test_map_a, TSStr, TInt)
    ),
    dict_map(test_map_a, TSStr, TInt)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TSStr, TArrKey),
      dict_map(test_map_a, TSStr, TInt)
    ),
    dict_map(test_map_a, TSStr, TInt)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TSStr, TInt),
      dict_map(test_map_a, TArrKey, TInt)
    ),
    dict_map(test_map_a, TSStr, TInt)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TSStr, TInt),
      dict_map(test_map_a, TInt, TInt)
    ),
    dict_map(test_map_a)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TInt, TStr),
      dict_map(test_map_a, TInt, TInt)
    ),
    dict_map(test_map_a)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TInt, TInt),
      dict_map(test_map_e, TInt, TInt)
    ),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_a), dict_map(test_map_b, TSStr, TNum)),
    dict_map(test_map_a)
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_b, TSStr, TNum), dict_map(test_map_a)),
    dict_map(test_map_a)
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_a), dict_map(test_map_b, TSStr, TObj)),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_b, TSStr, TObj), dict_map(test_map_a)),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_a, TSStr, TObj), dict_n(TSStr, TObj)),
    TBottom
  );
  EXPECT_EQ(
    intersection_of(dict_map(test_map_a, TSStr, TObj), dict_n(TSStr, TNum)),
    dict_map(test_map_a)
  );
  EXPECT_EQ(
    intersection_of(
      dict_map(test_map_a, TSStr, TInitCell),
      dict_n(TSStr, TNum)
    ),
    dict_map(test_map_a, TSStr, TNum)
  );

  EXPECT_EQ(
    array_like_set(dict_map(test_map_b), TSStr, TStr).first,
    dict_map(test_map_g, TSStr, TStr)
  );
  EXPECT_EQ(
    array_like_set(dict_map(test_map_a), sval(s_B), TStr).first,
    dict_map(test_map_h)
  );
  EXPECT_EQ(
    array_like_set(dict_map(test_map_a), sval(s_test), TStr).first,
    dict_map(test_map_i)
  );
  EXPECT_EQ(
    array_like_set(dict_map(test_map_a, TSStr, TInt), sval(s_test), TStr).first,
    dict_map(test_map_a, TSStr, TArrKey)
  );
  EXPECT_EQ(
    array_like_set(
      dict_map(test_map_a, sval(s_test), TInt),
      sval(s_test),
      TObj
    ).first,
    dict_map(test_map_j)
  );
}

TEST_F(TypeTest, ContextDependent) {
  // This only covers basic cases involving objects.  More testing should
  // be added for non object types, and nested types.
  auto const program = make_test_program();
  auto const unit = program->units.back().get();
  auto const func = [&]() -> php::Func* {
    for (auto& f : unit->funcs) {
      if (f->name->isame(s_test.get())) return f.get();
    }
    return nullptr;
  }();
  EXPECT_TRUE(func != nullptr);

  auto const ctx = Context { unit, func };
  Index idx{program.get()};

  // load classes in hierarchy  Base -> B -> BB
  auto const clsBase = idx.resolve_class(ctx, s_Base.get());
  if (!clsBase) ADD_FAILURE();
  auto const clsB = idx.resolve_class(ctx, s_B.get());
  if (!clsB) ADD_FAILURE();
  auto const clsBB = idx.resolve_class(ctx, s_BB.get());
  if (!clsBB) ADD_FAILURE();
  // Unrelated class.
  auto const clsUn = idx.resolve_class(ctx, s_TestClass.get());
  if (!clsUn) ADD_FAILURE();

  auto const objExactBaseTy     = objExact(*clsBase);
  auto const thisObjExactBaseTy = setctx(objExact(*clsBase));
  auto const subObjBaseTy       = subObj(*clsBase);
  auto const thisSubObjBaseTy   = setctx(subObj(*clsBase));

  auto const objExactBTy        = objExact(*clsB);
  auto const thisObjExactBTy    = setctx(objExact(*clsB));
  auto const subObjBTy          = subObj(*clsB);
  auto const thisSubObjBTy      = setctx(subObj(*clsB));
  auto const clsExactBTy        = clsExact(*clsB);
  auto const thisClsExactBTy    = setctx(clsExact(*clsB));
  auto const subClsBTy          = subCls(*clsB);
  auto const thisSubClsBTy      = setctx(subCls(*clsB));

  auto const objExactBBTy       = objExact(*clsBB);
  auto const thisObjExactBBTy   = setctx(objExact(*clsBB));
  auto const subObjBBTy         = subObj(*clsBB);
  auto const thisSubObjBBTy     = setctx(subObj(*clsBB));
  auto const clsExactBBTy       = clsExact(*clsBB);
  auto const thisClsExactBBTy   = setctx(clsExact(*clsBB));
  auto const subClsBBTy         = subCls(*clsBB);
  auto const thisSubClsBBTy     = setctx(subCls(*clsBB));

  auto const objExactUnTy       = objExact(*clsUn);
  auto const thisObjExactUnTy   = setctx(objExact(*clsUn));
  auto const subObjUnTy         = subObj(*clsUn);
  auto const thisSubObjUnTy     = setctx(subObj(*clsUn));

#define REFINE_EQ(A, B) \
  EXPECT_TRUE((A).equivalentlyRefined((B)))
#define REFINE_NEQ(A, B) \
  EXPECT_FALSE((A).equivalentlyRefined((B)))

  // check that improving any non context dependent type does not change the
  // type whether or not the context is related.
  REFINE_EQ(return_with_context(objExactBaseTy, objExactBTy),
            objExactBaseTy);
  REFINE_EQ(return_with_context(subObjBaseTy, objExactBTy),
            subObjBaseTy);
  REFINE_EQ(return_with_context(objExactBTy, objExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(subObjBTy, objExactBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(objExactBBTy, objExactBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(subObjBBTy, objExactBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(objExactUnTy, objExactBTy),
            objExactUnTy);
  REFINE_EQ(return_with_context(subObjUnTy, objExactBTy),
            subObjUnTy);
  REFINE_EQ(return_with_context(objExactBaseTy, clsExactBTy),
            objExactBaseTy);
  REFINE_EQ(return_with_context(subObjBaseTy, clsExactBTy),
            subObjBaseTy);
  REFINE_EQ(return_with_context(objExactBTy, clsExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(subObjBTy, clsExactBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(objExactBBTy, clsExactBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(subObjBBTy, clsExactBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(objExactUnTy, clsExactBTy),
            objExactUnTy);
  REFINE_EQ(return_with_context(subObjUnTy, clsExactBTy),
            subObjUnTy);

  // With sub.
  REFINE_EQ(return_with_context(objExactBaseTy, subObjBTy),
            objExactBaseTy);
  REFINE_EQ(return_with_context(subObjBaseTy, subObjBTy),
            subObjBaseTy);
  REFINE_EQ(return_with_context(objExactBTy, subObjBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(subObjBTy, subObjBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(objExactBBTy, subObjBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(subObjBBTy, subObjBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(objExactUnTy, subObjBTy),
            objExactUnTy);
  REFINE_EQ(return_with_context(subObjUnTy, subObjBTy),
            subObjUnTy);
  REFINE_EQ(return_with_context(objExactBaseTy, subClsBTy),
            objExactBaseTy);
  REFINE_EQ(return_with_context(subObjBaseTy, subClsBTy),
            subObjBaseTy);
  REFINE_EQ(return_with_context(objExactBTy, subClsBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(subObjBTy, subClsBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(objExactBBTy, subClsBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(subObjBBTy, subClsBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(objExactUnTy, subClsBTy),
            objExactUnTy);
  REFINE_EQ(return_with_context(subObjUnTy, subClsBTy),
            subObjUnTy);

  // Improvements (exact)
  REFINE_EQ(return_with_context(thisObjExactBaseTy, objExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, objExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, objExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, objExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, objExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBBTy, objExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactUnTy, objExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, objExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, clsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, clsExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, clsExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, clsExactBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, clsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBBTy, clsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactUnTy, clsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, clsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, thisObjExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, thisObjExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, thisObjExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, thisObjExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, thisObjExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBBTy, thisObjExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactUnTy, thisObjExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, thisObjExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, thisClsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, thisClsExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, thisClsExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, thisClsExactBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, thisClsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBBTy, thisClsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactUnTy, thisClsExactBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, thisClsExactBTy),
            TBottom);

  // Improvements (sub)
  REFINE_EQ(return_with_context(thisObjExactBaseTy, subObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, subObjBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, subObjBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, subObjBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, subObjBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(thisSubObjBBTy, subObjBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(thisObjExactUnTy, subObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, subObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, subClsBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, subClsBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, subClsBTy),
            objExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, subClsBTy),
            subObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, subClsBTy),
            objExactBBTy);
  REFINE_EQ(return_with_context(thisSubObjBBTy, subClsBTy),
            subObjBBTy);
  REFINE_EQ(return_with_context(thisObjExactUnTy, subClsBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, subClsBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, thisSubObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, thisSubObjBTy),
            thisSubObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, thisSubObjBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, thisSubObjBTy),
            thisSubObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, thisSubObjBTy),
            thisObjExactBBTy);
  REFINE_EQ(return_with_context(thisSubObjBBTy, thisSubObjBTy),
            thisSubObjBBTy);
  REFINE_EQ(return_with_context(thisObjExactUnTy, thisSubObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, thisSubObjBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisObjExactBaseTy, thisSubClsBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjBaseTy, thisSubClsBTy),
            thisSubObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBTy, thisSubClsBTy),
            thisObjExactBTy);
  REFINE_EQ(return_with_context(thisSubObjBTy, thisSubClsBTy),
            thisSubObjBTy);
  REFINE_EQ(return_with_context(thisObjExactBBTy, thisSubClsBTy),
            thisObjExactBBTy);
  REFINE_EQ(return_with_context(thisSubObjBBTy, thisSubClsBTy),
            thisSubObjBBTy);
  REFINE_EQ(return_with_context(thisObjExactUnTy, thisSubClsBTy),
            TBottom);
  REFINE_EQ(return_with_context(thisSubObjUnTy, thisSubClsBTy),
            TBottom);

  // Optional type preservation.
  REFINE_EQ(return_with_context(opt(subObjBaseTy), objExactBTy),
            opt(subObjBaseTy));
  REFINE_EQ(return_with_context(opt(subObjBaseTy), clsExactBTy),
            opt(subObjBaseTy));
  REFINE_EQ(return_with_context(opt(subObjBaseTy), subObjBTy),
            opt(subObjBaseTy));
  REFINE_EQ(return_with_context(opt(subObjBaseTy), subClsBTy),
            opt(subObjBaseTy));
  REFINE_EQ(return_with_context(opt(thisSubObjBaseTy), objExactBTy),
            opt(objExactBTy));
  REFINE_EQ(return_with_context(opt(thisSubObjBaseTy), clsExactBTy),
            opt(objExactBTy));
  REFINE_EQ(return_with_context(opt(thisSubObjBaseTy), thisObjExactBTy),
            opt(thisObjExactBTy));
  REFINE_EQ(return_with_context(opt(thisSubObjBaseTy), thisClsExactBTy),
            opt(thisObjExactBTy));


  // Refinedness operators.
  REFINE_EQ(objExactBTy, objExactBTy);
  REFINE_EQ(subObjBTy, subObjBTy);
  REFINE_EQ(clsExactBTy, clsExactBTy);
  REFINE_EQ(subClsBTy, subClsBTy);
  REFINE_EQ(thisObjExactBTy, thisObjExactBTy);
  REFINE_EQ(thisSubObjBTy, thisSubObjBTy);
  REFINE_EQ(thisClsExactBTy, thisClsExactBTy);
  REFINE_EQ(thisSubClsBTy, thisSubClsBTy);

  REFINE_NEQ(objExactBTy, thisObjExactBTy);
  REFINE_NEQ(subObjBTy, thisSubObjBTy);
  REFINE_NEQ(clsExactBTy, thisClsExactBTy);
  REFINE_NEQ(subClsBTy, thisSubClsBTy);
  REFINE_NEQ(thisObjExactBTy, objExactBTy);
  REFINE_NEQ(thisSubObjBTy, subObjBTy);
  REFINE_NEQ(thisClsExactBTy, clsExactBTy);
  REFINE_NEQ(thisSubClsBTy, subClsBTy);

  EXPECT_FALSE(objExactBTy.moreRefined(thisObjExactBTy));
  EXPECT_FALSE(subObjBTy.moreRefined(thisSubObjBTy));
  EXPECT_FALSE(clsExactBTy.moreRefined(thisClsExactBTy));
  EXPECT_FALSE(subClsBTy.moreRefined(thisSubClsBTy));

  EXPECT_TRUE(thisObjExactBTy.moreRefined(objExactBTy));
  EXPECT_TRUE(thisSubObjBTy.moreRefined(subObjBTy));
  EXPECT_TRUE(thisClsExactBTy.moreRefined(clsExactBTy));
  EXPECT_TRUE(thisSubClsBTy.moreRefined(subClsBTy));

  EXPECT_TRUE(thisObjExactBTy.moreRefined(thisObjExactBTy));
  EXPECT_TRUE(thisSubObjBTy.moreRefined(thisSubObjBTy));
  EXPECT_TRUE(thisClsExactBTy.moreRefined(thisClsExactBTy));
  EXPECT_TRUE(thisSubClsBTy.moreRefined(thisSubClsBTy));

  EXPECT_FALSE(thisObjExactBTy.strictlyMoreRefined(thisObjExactBTy));
  EXPECT_FALSE(thisSubObjBTy.strictlyMoreRefined(thisSubObjBTy));
  EXPECT_FALSE(thisClsExactBTy.strictlyMoreRefined(thisClsExactBTy));
  EXPECT_FALSE(thisSubClsBTy.strictlyMoreRefined(thisSubClsBTy));

  EXPECT_FALSE(thisObjExactBBTy.strictlyMoreRefined(thisObjExactBTy));
  EXPECT_TRUE(thisSubObjBBTy.strictlyMoreRefined(thisSubObjBTy));
  EXPECT_FALSE(thisClsExactBBTy.strictlyMoreRefined(thisClsExactBTy));
  EXPECT_TRUE(thisSubClsBBTy.strictlyMoreRefined(thisSubClsBTy));

  EXPECT_FALSE(thisObjExactBTy.strictlyMoreRefined(thisObjExactBBTy));
  EXPECT_FALSE(thisSubObjBTy.strictlyMoreRefined(thisSubObjBBTy));
  EXPECT_FALSE(thisClsExactBTy.strictlyMoreRefined(thisClsExactBBTy));
  EXPECT_FALSE(thisSubClsBTy.strictlyMoreRefined(thisSubClsBBTy));

  EXPECT_FALSE(objExactBBTy.strictlyMoreRefined(thisObjExactBTy));
  EXPECT_FALSE(subObjBBTy.strictlyMoreRefined(thisSubObjBTy));
  EXPECT_FALSE(clsExactBBTy.strictlyMoreRefined(thisClsExactBTy));
  EXPECT_FALSE(subClsBBTy.strictlyMoreRefined(thisSubClsBTy));

  // Normal equality should still hold.
  EXPECT_EQ(objExactBTy, thisObjExactBTy);
  EXPECT_EQ(subObjBTy, thisSubObjBTy);
  EXPECT_EQ(clsExactBTy, thisClsExactBTy);
  EXPECT_EQ(subClsBTy, thisSubClsBTy);
  EXPECT_EQ(thisObjExactBTy, objExactBTy);
  EXPECT_EQ(thisSubObjBTy, subObjBTy);
  EXPECT_EQ(thisClsExactBTy, clsExactBTy);
  EXPECT_EQ(thisSubClsBTy, subClsBTy);

  auto const& types = allCases(idx);
  auto const test = [&] (const Type& context) {
    for (auto const& t: types) {
      if (!t.subtypeOf(BInitCell)) continue;
      auto const [obj, objRest] = split_obj(t);
      auto const [cls, clsRest] = split_cls(objRest);
      REFINE_EQ(
        return_with_context(t, context),
        union_of(
          return_with_context(obj, context),
          return_with_context(cls, context),
          clsRest
        )
      );
    }
  };
  test(objExactBTy);
  test(clsExactBTy);
  test(subObjBTy);
  test(subClsBTy);
  test(thisObjExactBTy);
  test(thisClsExactBTy);
  test(thisSubObjBTy);
  test(thisSubClsBTy);

#undef REFINE_NEQ
#undef REFINE_EQ
}

TEST_F(TypeTest, ArrLike) {
  const std::initializer_list<std::pair<Type, Type>> subtype_true{
    // Expect all static arrays to be subtypes
    { TSKeyset, TArrLike },
    { TSDict,   TArrLike },
    { TSVec,    TArrLike },
    // Expect other arrays to be subtypes
    { TKeyset,  TArrLike },
    { TDict,    TArrLike },
    { TVec,     TArrLike },
    // Expect VArray and DArray to be subtypes
    { TDArr,    TArrLike },
    { TVArr,    TArrLike },
  };

  const std::initializer_list<std::pair<Type, Type>> subtype_false{
    // ClsMeth is not an array
    { TClsMeth, TArrLike },
    // Ints are not arrays
    { TInt,     TArrLike },
    // ArrLike doesn't contain null
    { TOptVec,  TArrLike },
  };

  const std::initializer_list<std::pair<Type, Type>> couldbe_true{
    { TArrLike, TOptKeysetE },
  };

  const std::initializer_list<std::pair<Type, Type>> couldbe_false{
    { TArrLike, TPrim },
    { TArrLike, TNull },
  };

  for (auto kv : subtype_true) {
    EXPECT_TRUE(kv.first.subtypeOf(kv.second))
      << show(kv.first) << " subtypeOf " << show(kv.second);
  }

  for (auto kv : subtype_false) {
    EXPECT_FALSE(kv.first.subtypeOf(kv.second))
      << show(kv.first) << " !subtypeOf " << show(kv.second);
  }

  for (auto kv : couldbe_true) {
    EXPECT_TRUE(kv.first.couldBe(kv.second))
      << show(kv.first) << " couldbe " << show(kv.second);
    EXPECT_TRUE(kv.second.couldBe(kv.first))
      << show(kv.first) << " couldbe " << show(kv.second);
  }

  for (auto kv : couldbe_false) {
    EXPECT_FALSE(kv.first.couldBe(kv.second))
      << show(kv.first) << " !couldbe " << show(kv.second);
    EXPECT_FALSE(kv.second.couldBe(kv.first))
      << show(kv.first) << " !couldbe " << show(kv.second);
  }
}

TEST_F(TypeTest, LoosenLikeness) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto old = RO::EvalIsCompatibleClsMethType;
  RO::EvalIsCompatibleClsMethType = true;
  SCOPE_EXIT { RO::EvalIsCompatibleClsMethType = old; };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    if (!t.couldBe(BCls | BLazyCls | BClsMeth)) {
      EXPECT_EQ(loosen_likeness(t), t);
    } else {
      auto u = BBottom;
      if (t.couldBe(BCls | BLazyCls)) u |= BSStr;
      if (t.couldBe(BClsMeth)) u |= BVArrN|BDArrN;
      EXPECT_EQ(loosen_likeness(t), union_of(t, Type{u}));
    }
  }

  std::vector<std::pair<Type, Type>> tests{
    { TClsMeth, Type{BClsMeth|BVArrN|BDArrN} },
    { TCls, Type{BCls|BSStr} },
    { TLazyCls, Type{BLazyCls|BSStr} },
    { TInt, TInt },
    { Type{BInt|BCls}, Type{BCls|BSStr|BInt} }
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_likeness(p.first), p.second);
  }
}

TEST_F(TypeTest, LoosenLikenessRecursively) {
  auto const program = make_test_program();
  Index index{ program.get() };

  auto old = RO::EvalIsCompatibleClsMethType;
  RO::EvalIsCompatibleClsMethType = true;
  SCOPE_EXIT { RO::EvalIsCompatibleClsMethType = old; };

  auto const test = [&] (const Type& t) {
    if (!t.subtypeOf(BInitCell)) return;

    if (!t.is(BBottom)) {
      EXPECT_EQ(loosen_likeness_recursively(opt(t)),
                opt(loosen_likeness_recursively(t)));
      EXPECT_EQ(
        loosen_likeness_recursively(wait_handle(index, t)),
        wait_handle(index, loosen_likeness_recursively(t)));
      EXPECT_EQ(
        loosen_likeness_recursively(vec_n(t)),
        vec_n(loosen_likeness_recursively(t)));
      EXPECT_EQ(
        loosen_likeness_recursively(vec({t})),
        vec({loosen_likeness_recursively(t)}));
      EXPECT_EQ(
        loosen_likeness_recursively(dict_n(TArrKey, t)),
        dict_n(TArrKey, loosen_likeness_recursively(t)));
      EXPECT_EQ(
        loosen_likeness_recursively(dict_map({map_elem(s_A, t)}, TArrKey, t)),
        dict_map({map_elem(s_A, loosen_likeness_recursively(t))},
                 TArrKey, loosen_likeness_recursively(t)));
    }

    if (t.couldBe(BArrLikeN | BObj)) return;

    if (!t.couldBe(BCls | BLazyCls | BClsMeth)) {
      EXPECT_EQ(loosen_likeness_recursively(t), loosen_array_staticness(t));
    } else {
      auto u = BBottom;
      if (t.couldBe(BCls | BLazyCls)) u |= BSStr;
      if (t.couldBe(BClsMeth)) u |= BVArrN|BDArrN;
      EXPECT_EQ(loosen_likeness_recursively(t), union_of(t, Type{u}));
    }
  };

  auto const almostAll1 = Type{BInitCell & ~BSStr};
  auto const almostAll2 = Type{BInitCell & ~(BVArrN|BDArrN)};

  auto const& all = allCases(index);
  for (auto const& t : all) test(t);
  test(almostAll1);
  test(almostAll2);

  std::vector<std::pair<Type, Type>> tests{
    { TClsMeth, Type{BClsMeth|BVArrN|BDArrN} },
    { TCls, Type{BCls|BSStr} },
    { TLazyCls, Type{BLazyCls|BSStr} },
    { TInt, TInt },
    { Type{BInt|BCls}, Type{BCls|BSStr|BInt} },
    { wait_handle(index, TInt), wait_handle(index, TInt) },
    { wait_handle(index, TCls), wait_handle(index, Type{BCls|BSStr}) },
    { wait_handle(index, TClsMeth), wait_handle(index, Type{BClsMeth|BVArrN|BDArrN}) },
    { dict_val(static_dict(s_A, 100, s_B, 200)),
      dict_map({map_elem(s_A, ival(100)), map_elem(s_B, ival(200))}) },
    { vec_n(TInt), vec_n(TInt) },
    { vec_n(TCls), vec_n(Type{BCls|BSStr}) },
    { vec_n(TClsMeth), vec_n(Type{BClsMeth|BVArrN|BDArrN}) },
    { vec({TInt}), vec({TInt}) },
    { vec({TCls}), vec({Type{BCls|BSStr}}) },
    { vec({TClsMeth}), vec({Type{BClsMeth|BVArrN|BDArrN}}) },
    { dict_n(TArrKey, TInt), dict_n(TArrKey, TInt) },
    { dict_n(TArrKey, TCls), dict_n(TArrKey, Type{BCls|BSStr}) },
    { dict_n(TArrKey, TClsMeth), dict_n(TArrKey, Type{BClsMeth|BVArrN|BDArrN}) },
    { dict_map({map_elem(s_A, TInt)}, TArrKey, TInt),
      dict_map({map_elem(s_A, TInt)}, TArrKey, TInt) },
    { dict_map({map_elem(s_A, TCls)}, TArrKey, TCls),
      dict_map({map_elem(s_A, Type{BCls|BSStr})}, TArrKey, Type{BCls|BSStr}) },
    { dict_map({map_elem(s_A, TClsMeth)}, TArrKey, TClsMeth),
      dict_map({map_elem(s_A, Type{BClsMeth|BVArrN|BDArrN})},
               TArrKey, Type{BClsMeth|BVArrN|BDArrN}) },
    { vec_n(almostAll1), TVecN },
    { vec_n(almostAll2), TVecN },
    { dict_n(TArrKey, almostAll1), TDictN },
    { dict_n(TArrKey, almostAll2), TDictN }
  };
  for (auto const& p : tests) {
    EXPECT_EQ(loosen_mark_for_testing(loosen_likeness_recursively(p.first)),
              loosen_mark_for_testing(p.second));
  }
}

TEST_F(TypeTest, ArrayProvenance) {
  RO::EvalArrayProvenance = true;

  auto const program = make_test_program();
  Index index{ program.get() };

  auto const& all = allCases(index);
  for (auto const& t : all) {
    EXPECT_EQ(opt(loosen_provenance(t)), loosen_provenance(opt(t)));
    if (t.strictSubtypeOf(BInitCell)) {
      EXPECT_EQ(wait_handle(index, loosen_provenance(t)),
                loosen_provenance(wait_handle(index, t)));
    }

    if (!t.couldBe(BArrLike) && !is_specialized_wait_handle(t)) {
      EXPECT_EQ(t, loosen_provenance(t));
    } else {
      EXPECT_FALSE(is_specialized_array_like_arrval(loosen_provenance(t)));
    }

    if (t.subtypeOf(BCell)) {
      EXPECT_FALSE(
        assert_nonemptiness(union_of(t,TArrLikeE)).couldBe(BArrLikeE)
      );

      EXPECT_FALSE(
        remove_bits(union_of(t,TArrLikeE), BArrLikeE).couldBe(BArrLikeE)
      );

      EXPECT_FALSE(
        intersection_of(
          union_of(t, TVecishE),
          TDictish
        ).couldBe(BVecishE)
      );
    }
  }
}

TEST_F(TypeTest, IterTypes) {
  auto const elem1 = map_elem(s_A, TObj);
  auto const elem2 = map_elem_nonstatic(s_B, TInt);
  auto const sdict1 = static_dict(s_A, 100);
  auto const sdict2 = static_dict(s_A, 100, s_B, 200);

  std::vector<std::pair<Type, IterTypes>> tests{
    { TInt, { TBottom, TBottom, IterTypes::Count::Empty, true, true } },
    { TInitNull, { TBottom, TBottom, IterTypes::Count::Empty, true, true } },
    { Type{BObj|BArrLike}, { TInitCell, TInitCell, IterTypes::Count::Any, true, true } },
    { Type{BInt|BArrLike}, { TInitCell, TInitCell, IterTypes::Count::Any, true, false } },
    { TVecE, { TBottom, TBottom, IterTypes::Count::Empty, false, false } },
    { TOptVecE, { TBottom, TBottom, IterTypes::Count::Empty, true, false } },

    { TSVecish, { TInt, TInitUnc, IterTypes::Count::Any, false, false } },
    { TOptSVecish, { TInt, TInitUnc, IterTypes::Count::Any, true, false } },
    { TSVecishN, { TInt, TInitUnc, IterTypes::Count::NonEmpty, false, false } },
    { TOptSVecishN, { TInt, TInitUnc, IterTypes::Count::Any, true, false } },

    { TSKeyset, { TUncArrKey, TUncArrKey, IterTypes::Count::Any, false, false } },
    { TOptSKeyset, { TUncArrKey, TUncArrKey, IterTypes::Count::Any, true, false } },
    { TSKeysetN, { TUncArrKey, TUncArrKey, IterTypes::Count::NonEmpty, false, false } },
    { TOptSKeysetN, { TUncArrKey, TUncArrKey, IterTypes::Count::Any, true, false } },

    { TSArrLike, { TUncArrKey, TInitUnc, IterTypes::Count::Any, false, false } },
    { TOptSArrLike, { TUncArrKey, TInitUnc, IterTypes::Count::Any, true, false } },
    { TSArrLikeN, { TUncArrKey, TInitUnc, IterTypes::Count::NonEmpty, false, false } },
    { TOptSArrLikeN, { TUncArrKey, TInitUnc, IterTypes::Count::Any, true, false } },

    { TVecish, { TInt, TInitCell, IterTypes::Count::Any, false, false } },
    { TOptVecish, { TInt, TInitCell, IterTypes::Count::Any, true, false } },
    { TVecishN, { TInt, TInitCell, IterTypes::Count::NonEmpty, false, false } },
    { TOptVecishN, { TInt, TInitCell, IterTypes::Count::Any, true, false } },

    { TKeyset, { TArrKey, TArrKey, IterTypes::Count::Any, false, false } },
    { TOptKeyset, { TArrKey, TArrKey, IterTypes::Count::Any, true, false } },
    { TKeysetN, { TArrKey, TArrKey, IterTypes::Count::NonEmpty, false, false } },
    { TOptKeysetN, { TArrKey, TArrKey, IterTypes::Count::Any, true, false } },

    { TArrLike, { TArrKey, TInitCell, IterTypes::Count::Any, false, false } },
    { TOptArrLike, { TArrKey, TInitCell, IterTypes::Count::Any, true, false } },
    { TArrLikeN, { TArrKey, TInitCell, IterTypes::Count::NonEmpty, false, false } },
    { TOptArrLikeN, { TArrKey, TInitCell, IterTypes::Count::Any, true, false } },

    { make_specialized_arrval(BSDict, sdict1), { sval(s_A), ival(100), IterTypes::Count::ZeroOrOne, false, false } },
    { make_specialized_arrval(BOptSDict, sdict1), { sval(s_A), ival(100), IterTypes::Count::ZeroOrOne, true, false } },
    { make_specialized_arrval(BSDictN, sdict1), { sval(s_A), ival(100), IterTypes::Count::Single, false, false } },
    { make_specialized_arrval(BOptSDictN, sdict1), { sval(s_A), ival(100), IterTypes::Count::ZeroOrOne, true, false } },

    { make_specialized_arrval(BSDict, sdict2), { TSStr, TInt, IterTypes::Count::Any, false, false } },
    { make_specialized_arrval(BOptSDict, sdict2), { TSStr, TInt, IterTypes::Count::Any, true, false } },
    { make_specialized_arrval(BSDictN, sdict2), { TSStr, TInt, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrval(BOptSDictN, sdict2), { TSStr, TInt, IterTypes::Count::Any, true, false } },

    { make_specialized_arrpackedn(BVecish, TObj), { TInt, TObj, IterTypes::Count::Any, false, false } },
    { make_specialized_arrpackedn(BOptVecish, TObj), { TInt, TObj, IterTypes::Count::Any, true, false } },
    { make_specialized_arrpackedn(BVecishN, TObj), { TInt, TObj, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrpackedn(BOptVecishN, TObj), { TInt, TObj, IterTypes::Count::Any, true, false } },

    { make_specialized_arrpacked(BVecish, {TObj}), { ival(0), TObj, IterTypes::Count::ZeroOrOne, false, false } },
    { make_specialized_arrpacked(BOptVecish, {TObj}), { ival(0), TObj, IterTypes::Count::ZeroOrOne, true, false } },
    { make_specialized_arrpacked(BVecishN, {TObj}), { ival(0), TObj, IterTypes::Count::Single, false, false } },
    { make_specialized_arrpacked(BOptVecishN, {TObj}), { ival(0), TObj, IterTypes::Count::ZeroOrOne, true, false } },

    { make_specialized_arrpacked(BVecish, {TObj,TStr}), { TInt, Type{BObj|BStr}, IterTypes::Count::Any, false, false } },
    { make_specialized_arrpacked(BOptVecish, {TObj,TStr}), { TInt, Type{BObj|BStr}, IterTypes::Count::Any, true, false } },
    { make_specialized_arrpacked(BVecishN, {TObj,TStr}), { TInt, Type{BObj|BStr}, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrpacked(BOptVecishN, {TObj,TStr}), { TInt, Type{BObj|BStr}, IterTypes::Count::Any, true, false } },

    { make_specialized_arrmapn(BDict, TStr, TObj), { TStr, TObj, IterTypes::Count::Any, false, false } },
    { make_specialized_arrmapn(BOptDict, TStr, TObj), { TStr, TObj, IterTypes::Count::Any, true, false } },
    { make_specialized_arrmapn(BDictN, TStr, TObj), { TStr, TObj, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrmapn(BOptDictN, TStr, TObj), { TStr, TObj, IterTypes::Count::Any, true, false } },

    { make_specialized_arrmap(BDict, {elem1}), { sval(s_A), TObj, IterTypes::Count::ZeroOrOne, false, false } },
    { make_specialized_arrmap(BOptDict, {elem1}), { sval(s_A), TObj, IterTypes::Count::ZeroOrOne, true, false } },
    { make_specialized_arrmap(BDictN, {elem1}), { sval(s_A), TObj, IterTypes::Count::Single, false, false } },
    { make_specialized_arrmap(BOptDictN, {elem1}), { sval(s_A), TObj, IterTypes::Count::ZeroOrOne, true, false } },

    { make_specialized_arrmap(BDict, {elem2}), { sval_nonstatic(s_B), TInt, IterTypes::Count::ZeroOrOne, false, false } },
    { make_specialized_arrmap(BOptDict, {elem2}), { sval_nonstatic(s_B), TInt, IterTypes::Count::ZeroOrOne, true, false } },
    { make_specialized_arrmap(BDictN, {elem2}), { sval_nonstatic(s_B), TInt, IterTypes::Count::Single, false, false } },
    { make_specialized_arrmap(BOptDictN, {elem2}), { sval_nonstatic(s_B), TInt, IterTypes::Count::ZeroOrOne, true, false } },

    { make_specialized_arrmap(BDict, {elem1,elem2}), { TStr, Type{BObj|BInt}, IterTypes::Count::Any, false, false } },
    { make_specialized_arrmap(BOptDict, {elem1,elem2}), { TStr, Type{BObj|BInt}, IterTypes::Count::Any, true, false } },
    { make_specialized_arrmap(BDictN, {elem1,elem2}), { TStr, Type{BObj|BInt}, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrmap(BOptDictN, {elem1,elem2}), { TStr, Type{BObj|BInt}, IterTypes::Count::Any, true, false } },

    { make_specialized_arrmap(BDict, {elem1}, TInt, TInt), { union_of(sval(s_A),TInt), Type{BObj|BInt}, IterTypes::Count::Any, false, false } },
    { make_specialized_arrmap(BOptDict, {elem1}, TInt, TInt), { union_of(sval(s_A),TInt), Type{BObj|BInt}, IterTypes::Count::Any, true, false } },
    { make_specialized_arrmap(BDictN, {elem1}, TInt, TInt), { union_of(sval(s_A),TInt), Type{BObj|BInt}, IterTypes::Count::NonEmpty, false, false } },
    { make_specialized_arrmap(BOptDictN, {elem1}, TInt, TInt), { union_of(sval(s_A),TInt), Type{BObj|BInt}, IterTypes::Count::Any, true, false } },
  };

  for (auto const& p : tests) {
    auto const iter = iter_types(p.first);
    EXPECT_EQ(iter.key, p.second.key);
    EXPECT_EQ(iter.value, p.second.value);
    EXPECT_EQ(iter.count, p.second.count) << show(p.first);
    EXPECT_EQ(iter.mayThrowOnInit, p.second.mayThrowOnInit);
    EXPECT_EQ(iter.mayThrowOnNext, p.second.mayThrowOnNext);
  }
}

//////////////////////////////////////////////////////////////////////

}}
