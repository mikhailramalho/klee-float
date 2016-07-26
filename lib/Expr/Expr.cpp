//===-- Expr.cpp ----------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr.h"
#include "klee/Config/Version.h"
#include "klee/Internal/Support/ErrorHandling.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
#include "llvm/ADT/Hashing.h"
#endif
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
// FIXME: We shouldn't need this once fast constant support moves into
// Core. If we need to do arithmetic, we probably want to use APInt.
#include "klee/Internal/Support/IntEvaluation.h"

#include "klee/util/ExprPPrinter.h"

#include <sstream>
#include <fenv.h>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  ConstArrayOpt("const-array-opt",
	 cl::init(false),
	 cl::desc("Enable various optimizations involving all-constant arrays."));
}

/***/

unsigned Expr::count = 0;

ref<Expr> Expr::createTempRead(const Array *array, Expr::Width w) {
  UpdateList ul(array, 0);

  switch (w) {
  default: assert(0 && "invalid width");
  case Expr::Bool: 
    return ZExtExpr::create(ReadExpr::create(ul, 
                                             ConstantExpr::alloc(0, Expr::Int32)),
                            Expr::Bool);
  case Expr::Int8: 
    return ReadExpr::create(ul, 
                            ConstantExpr::alloc(0,Expr::Int32));
  case Expr::Int16: 
    return ConcatExpr::create(ReadExpr::create(ul, 
                                               ConstantExpr::alloc(1,Expr::Int32)),
                              ReadExpr::create(ul, 
                                               ConstantExpr::alloc(0,Expr::Int32)));
  case Expr::Int32: 
    return ConcatExpr::create4(ReadExpr::create(ul, 
                                                ConstantExpr::alloc(3,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(2,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(1,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(0,Expr::Int32)));
  case Expr::Int64: 
    return ConcatExpr::create8(ReadExpr::create(ul, 
                                                ConstantExpr::alloc(7,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(6,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(5,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(4,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(3,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(2,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(1,Expr::Int32)),
                               ReadExpr::create(ul, 
                                                ConstantExpr::alloc(0,Expr::Int32)));
  }
}

// returns 0 if b is structurally equal to *this
int Expr::compare(const Expr &b, ExprEquivSet &equivs) const {
  if (this == &b) return 0;

  const Expr *ap, *bp;
  if (this < &b) {
    ap = this; bp = &b;
  } else {
    ap = &b; bp = this;
  }

  if (equivs.count(std::make_pair(ap, bp)))
    return 0;

  Kind ak = getKind(), bk = b.getKind();
  if (ak!=bk)
    return (ak < bk) ? -1 : 1;

  if (hashValue != b.hashValue) 
    return (hashValue < b.hashValue) ? -1 : 1;

  if (int res = compareContents(b)) 
    return res;

  unsigned aN = getNumKids();
  for (unsigned i=0; i<aN; i++)
    if (int res = getKid(i)->compare(*b.getKid(i), equivs))
      return res;

  equivs.insert(std::make_pair(ap, bp));
  return 0;
}

void Expr::printKind(llvm::raw_ostream &os, Kind k) {
  switch(k) {
#define X(C) case C: os << #C; break
    X(Constant);
    X(NotOptimized);
    X(Read);
    X(Select);
    X(Concat);
    X(Extract);
    X(ZExt);
    X(SExt);
    X(FExt);
    X(FToU);
    X(FToS);
    X(UToF);
    X(SToF);
    X(FAbs);
    X(FpClassify);
    X(FIsFinite);
    X(FIsNan);
    X(FIsInf);
    X(FSqrt);
    X(FNearbyInt);
    X(Add);
    X(Sub);
    X(Mul);
    X(UDiv);
    X(SDiv);
    X(URem);
    X(SRem);
    X(Not);
    X(And);
    X(Or);
    X(Xor);
    X(Shl);
    X(LShr);
    X(AShr);
    X(FAdd);
    X(FSub);
    X(FMul);
    X(FDiv);
    X(FRem);
    X(FMin);
    X(FMax);
    X(Eq);
    X(Ne);
    X(Ult);
    X(Ule);
    X(Ugt);
    X(Uge);
    X(Slt);
    X(Sle);
    X(Sgt);
    X(Sge);
    X(FOrd);
    X(FUno);
    X(FUeq);
    X(FOeq);
    X(FUgt);
    X(FOgt);
    X(FUge);
    X(FOge);
    X(FUlt);
    X(FOlt);
    X(FUle);
    X(FOle);
    X(FUne);
    X(FOne);
#undef X
  default:
    assert(0 && "invalid kind");
    }
}

////////
//
// Simple hash functions for various kinds of Exprs
//
///////

unsigned Expr::computeHash() {
  unsigned res = getKind() * Expr::MAGIC_HASH_CONSTANT;

  int n = getNumKids();
  for (int i = 0; i < n; i++) {
    res <<= 1;
    res ^= getKid(i)->hash() * Expr::MAGIC_HASH_CONSTANT;
  }
  
  hashValue = res;
  return hashValue;
}

unsigned ConstantExpr::computeHash() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
  hashValue = hash_value(value) ^ (getWidth() * MAGIC_HASH_CONSTANT);
#else
  hashValue = value.getHashValue() ^ (getWidth() * MAGIC_HASH_CONSTANT);
#endif
  return hashValue;
}

unsigned CastExpr::computeHash() {
  unsigned res = getWidth() * Expr::MAGIC_HASH_CONSTANT;
  hashValue = res ^ src->hash() * Expr::MAGIC_HASH_CONSTANT;
  return hashValue;
}

unsigned ExtractExpr::computeHash() {
  unsigned res = offset * Expr::MAGIC_HASH_CONSTANT;
  res ^= getWidth() * Expr::MAGIC_HASH_CONSTANT;
  hashValue = res ^ expr->hash() * Expr::MAGIC_HASH_CONSTANT;
  return hashValue;
}

unsigned ReadExpr::computeHash() {
  unsigned res = index->hash() * Expr::MAGIC_HASH_CONSTANT;
  res ^= updates.hash();
  hashValue = res;
  return hashValue;
}

unsigned NotExpr::computeHash() {
  hashValue = expr->hash() * Expr::MAGIC_HASH_CONSTANT * Expr::Not;
  return hashValue;
}

ref<Expr> Expr::createFromKind(Kind k, std::vector<CreateArg> args) {
  unsigned numArgs = args.size();
  (void) numArgs;

  switch(k) {
    case Constant:
    case Extract:
    case Read:
    default:
      assert(0 && "invalid kind");

    case NotOptimized:
      assert(numArgs == 1 && args[0].isExpr() &&
             "invalid args array for given opcode");
      return NotOptimizedExpr::create(args[0].expr);
      
    case Select:
      assert(numArgs == 3 && args[0].isExpr() &&
             args[1].isExpr() && args[2].isExpr() &&
             "invalid args array for Select opcode");
      return SelectExpr::create(args[0].expr,
                                args[1].expr,
                                args[2].expr);

    case Concat: {
      assert(numArgs == 2 && args[0].isExpr() && args[1].isExpr() && 
             "invalid args array for Concat opcode");
      
      return ConcatExpr::create(args[0].expr, args[1].expr);
    }
      
#define CAST_EXPR_CASE(T)                                    \
      case T:                                                \
        assert(numArgs == 2 &&				     \
               args[0].isExpr() && args[1].isWidth() &&      \
               "invalid args array for given opcode");       \
      return T ## Expr::create(args[0].expr, args[1].width); \

#define CAST_RM_EXPR_CASE(T)                                                        \
      case T:                                                                       \
        assert(numArgs == 3 &&				                                              \
               args[0].isExpr() && args[1].isWidth() && args[2].isRoundingMode() && \
               "invalid args array for given opcode");                              \
      return T ## Expr::create(args[0].expr, args[1].width, args[2].rm);            \

#define BINARY_EXPR_CASE(T)                                 \
      case T:                                               \
        assert(numArgs == 2 &&                              \
               args[0].isExpr() && args[1].isExpr() &&      \
               "invalid args array for given opcode");      \
      return T ## Expr::create(args[0].expr, args[1].expr); \

#define BINARY_RM_EXPR_CASE(T)                                                     \
      case T:                                                                      \
        assert(numArgs == 3 &&                                                     \
               args[0].isExpr() && args[1].isExpr() && args[2].isRoundingMode() && \
               "invalid args array for given opcode");                             \
      return T ## Expr::create(args[0].expr, args[1].expr, args[2].rm);            \

#define UNARY_EXPR_CASE(T)                             \
      case T:                                          \
        assert(numArgs == 1 &&                         \
               args[0].isExpr() &&                     \
               "invalid args array for given opcode"); \
      return T ## Expr::create(args[0].expr);          \

#define UNARY_RM_EXPR_CASE(T)                                  \
      case T:                                                  \
        assert(numArgs == 2 &&                                 \
               args[0].isExpr() && args[1].isRoundingMode() && \
               "invalid args array for given opcode");         \
      return T ## Expr::create(args[0].expr, args[1].rm);      \

      CAST_EXPR_CASE(ZExt);
      CAST_EXPR_CASE(SExt);
      CAST_RM_EXPR_CASE(FExt);
      CAST_RM_EXPR_CASE(FToU);
      CAST_RM_EXPR_CASE(FToS);
      CAST_RM_EXPR_CASE(UToF);
      CAST_RM_EXPR_CASE(SToF);
      
      UNARY_EXPR_CASE(FAbs);
      UNARY_EXPR_CASE(FpClassify);
      UNARY_EXPR_CASE(FIsFinite);
      UNARY_EXPR_CASE(FIsNan);
      UNARY_EXPR_CASE(FIsInf);
      
      UNARY_RM_EXPR_CASE(FSqrt);
      UNARY_RM_EXPR_CASE(FNearbyInt);
      
      BINARY_EXPR_CASE(Add);
      BINARY_EXPR_CASE(Sub);
      BINARY_EXPR_CASE(Mul);
      BINARY_EXPR_CASE(UDiv);
      BINARY_EXPR_CASE(SDiv);
      BINARY_EXPR_CASE(URem);
      BINARY_EXPR_CASE(SRem);
      BINARY_EXPR_CASE(And);
      BINARY_EXPR_CASE(Or);
      BINARY_EXPR_CASE(Xor);
      BINARY_EXPR_CASE(Shl);
      BINARY_EXPR_CASE(LShr);
      BINARY_EXPR_CASE(AShr);
      BINARY_RM_EXPR_CASE(FAdd);
      BINARY_RM_EXPR_CASE(FSub);
      BINARY_RM_EXPR_CASE(FMul);
      BINARY_RM_EXPR_CASE(FDiv);
      BINARY_RM_EXPR_CASE(FRem);
      BINARY_EXPR_CASE(FMin);
      BINARY_EXPR_CASE(FMax);
      
      BINARY_EXPR_CASE(Eq);
      BINARY_EXPR_CASE(Ne);
      BINARY_EXPR_CASE(Ult);
      BINARY_EXPR_CASE(Ule);
      BINARY_EXPR_CASE(Ugt);
      BINARY_EXPR_CASE(Uge);
      BINARY_EXPR_CASE(Slt);
      BINARY_EXPR_CASE(Sle);
      BINARY_EXPR_CASE(Sgt);
      BINARY_EXPR_CASE(Sge);

      BINARY_EXPR_CASE(FOrd);
      BINARY_EXPR_CASE(FUno);
      BINARY_EXPR_CASE(FUeq);
      BINARY_EXPR_CASE(FOeq);
      BINARY_EXPR_CASE(FUgt);
      BINARY_EXPR_CASE(FOgt);
      BINARY_EXPR_CASE(FUge);
      BINARY_EXPR_CASE(FOge);
      BINARY_EXPR_CASE(FUlt);
      BINARY_EXPR_CASE(FOlt);
      BINARY_EXPR_CASE(FUle);
      BINARY_EXPR_CASE(FOle);
      BINARY_EXPR_CASE(FUne);
      BINARY_EXPR_CASE(FOne);

#undef UNARY_RM_EXPR_CASE
#undef UNARY_EXPR_CASE
#undef BINARY_RM_EXPR_CASE
#undef BINARY_EXPR_CASE
#undef CAST_RM_EXPR_CASE
#undef CAST_EXPR_CASE
  }
}


void Expr::printWidth(llvm::raw_ostream &os, Width width) {
  switch(width) {
  case Expr::Bool: os << "Expr::Bool"; break;
  case Expr::Int8: os << "Expr::Int8"; break;
  case Expr::Int16: os << "Expr::Int16"; break;
  case Expr::Int32: os << "Expr::Int32"; break;
  case Expr::Int64: os << "Expr::Int64"; break;
  case Expr::Fl80: os << "Expr::Fl80"; break;
  default: os << "<invalid type: " << (unsigned) width << ">";
  }
}

ref<Expr> Expr::createImplies(ref<Expr> hyp, ref<Expr> conc) {
  return OrExpr::create(Expr::createIsZero(hyp), conc);
}

ref<Expr> Expr::createIsZero(ref<Expr> e) {
  return EqExpr::create(e, ConstantExpr::create(0, e->getWidth()));
}

void Expr::print(llvm::raw_ostream &os) const {
  ExprPPrinter::printSingleExpr(os, const_cast<Expr*>(this));
}

void Expr::dump() const {
  this->print(errs());
  errs() << "\n";
}

/***/

ref<Expr> ConstantExpr::fromMemory(void *address, Width width) {
  switch (width) {
  case  Expr::Bool: return ConstantExpr::create(*(( uint8_t*) address), width);
  case  Expr::Int8: return ConstantExpr::create(*(( uint8_t*) address), width);
  case Expr::Int16: return ConstantExpr::create(*((uint16_t*) address), width);
  case Expr::Int32: return ConstantExpr::create(*((uint32_t*) address), width);
  case Expr::Int64: return ConstantExpr::create(*((uint64_t*) address), width);
  // FIXME: what about machines without x87 support?
  default:
    return ConstantExpr::alloc(llvm::APInt(width,
      (width+llvm::integerPartWidth-1)/llvm::integerPartWidth,
      (const uint64_t*)address));
  }
}

void ConstantExpr::toMemory(void *address) {
  switch (getWidth()) {
  default: assert(0 && "invalid type");
  case  Expr::Bool: *(( uint8_t*) address) = getZExtValue(1); break;
  case  Expr::Int8: *(( uint8_t*) address) = getZExtValue(8); break;
  case Expr::Int16: *((uint16_t*) address) = getZExtValue(16); break;
  case Expr::Int32: *((uint32_t*) address) = getZExtValue(32); break;
  case Expr::Int64: *((uint64_t*) address) = getZExtValue(64); break;
  // FIXME: what about machines without x87 support?
  case Expr::Fl80:
    *((long double*) address) = *(const long double*) value.getRawData();
    break;
  }
}

void ConstantExpr::toString(std::string &Res, unsigned radix) const {
  Res = value.toString(radix, false);
}

ref<ConstantExpr> ConstantExpr::Concat(const ref<ConstantExpr> &RHS) {
  Expr::Width W = getWidth() + RHS->getWidth();
  APInt Tmp(value);
  Tmp=Tmp.zext(W);
  Tmp <<= RHS->getWidth();
  Tmp |= APInt(RHS->value).zext(W);

  return ConstantExpr::alloc(Tmp);
}

ref<ConstantExpr> ConstantExpr::Extract(unsigned Offset, Width W) {
  return ConstantExpr::alloc(APInt(value.ashr(Offset)).zextOrTrunc(W));
}

ref<ConstantExpr> ConstantExpr::ZExt(Width W) {
  return ConstantExpr::alloc(APInt(value).zextOrTrunc(W));
}

ref<ConstantExpr> ConstantExpr::SExt(Width W) {
  return ConstantExpr::alloc(APInt(value).sextOrTrunc(W));
}

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width) {
  switch(width) {
  case Expr::Fl32:
    return &llvm::APFloat::IEEEsingle;
  case Expr::Fl64:
    return &llvm::APFloat::IEEEdouble;
  case Expr::Fl80:
    return &llvm::APFloat::x87DoubleExtended;
  default:
    return 0;
  }
}

ref<ConstantExpr> ConstantExpr::FExt(Width W, llvm::APFloat::roundingMode rm) {
  if (!fpWidthToSemantics(W))
      klee_error("Unsupported FExt operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat Res(value);
#endif
  bool losesInfo = false;
  Res.convert(*fpWidthToSemantics(W),
              rm,
              &losesInfo);
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FToU(Width W, llvm::APFloat::roundingMode rm) {
  if (!fpWidthToSemantics(getWidth()) || W > 64)
    klee_error("Unsupported FToU operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Arg(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat Arg(value);
#endif
  uint64_t new_value = 0;
  bool isExact = true;
  // TODO: should this observe rounding mode?
  Arg.convertToInteger(&new_value, W, false,
                       llvm::APFloat::rmTowardZero, &isExact);
  return ConstantExpr::alloc(new_value, W);
}

ref<ConstantExpr> ConstantExpr::FToS(Width W, llvm::APFloat::roundingMode rm) {
  if (!fpWidthToSemantics(getWidth()) || W > 64)
    klee_error("Unsupported FToS operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Arg(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat Arg(value);
#endif
  uint64_t new_value = 0;
  bool isExact = true;
  // TODO: should this observe rounding mode?
  Arg.convertToInteger(&new_value, W, true,
                       llvm::APFloat::rmTowardZero, &isExact);
  return ConstantExpr::alloc(new_value, W);
}

ref<ConstantExpr> ConstantExpr::UToF(Width W, llvm::APFloat::roundingMode rm) {
  const llvm::fltSemantics *semantics = fpWidthToSemantics(W);
  if (!semantics)
    klee_error("Unsupported UToF operation");
  llvm::APFloat f(*semantics, 0);
  f.convertFromAPInt(value, false,
                     rm);

  return ConstantExpr::alloc(f);
}

ref<ConstantExpr> ConstantExpr::SToF(Width W, llvm::APFloat::roundingMode rm) {
  const llvm::fltSemantics *semantics = fpWidthToSemantics(W);
  if (!semantics)
    klee_error("Unsupported SIToFP operation");
  llvm::APFloat f(*semantics, 0);
  f.convertFromAPInt(value, true,
                     rm);

  return ConstantExpr::alloc(f);
}

ref<ConstantExpr> ConstantExpr::FAbs() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat Res(value);
#endif
  Res.clearSign();
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FpClassify() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat AsFloat(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat AsFloat(value);
#endif

  int res;
  if (AsFloat.isNaN())
    res = FP_NAN;
  else if (AsFloat.isInfinity())
    res = FP_INFINITE;
  else if (AsFloat.isZero())
    res = FP_ZERO;
  else if (AsFloat.isDenormal())
    res = FP_SUBNORMAL;
  else
    res = FP_NORMAL;

  return ConstantExpr::alloc(res, sizeof(int) * 8);
}

ref<ConstantExpr> ConstantExpr::FIsFinite() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat AsFloat(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat AsFloat(value);
#endif

  int res = (!AsFloat.isNaN() && !AsFloat.isInfinity());

  return ConstantExpr::alloc(res, sizeof(int) * 8);
}

ref<ConstantExpr> ConstantExpr::FIsNan() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat AsFloat(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat AsFloat(value);
#endif

  int res = AsFloat.isNaN();

  return ConstantExpr::alloc(res, sizeof(int) * 8);
}

ref<ConstantExpr> ConstantExpr::FIsInf() {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat AsFloat(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat AsFloat(value);
#endif

  int res = 0;
  if (AsFloat.isInfinity())
  {
    if (AsFloat.isNegative())
    {
      res = -1;
    }
    else
    {
      res = 1;
    }
  }

  return ConstantExpr::alloc(res, sizeof(int) * 8);
}

ref<ConstantExpr> ConstantExpr::FSqrt(llvm::APFloat::roundingMode rm) {
  // XXX hack, change this when LLVM implements native APFloat sqrt
  int rounding_mode;
  switch (rm)
  {
  case llvm::APFloat::rmNearestTiesToEven:
    rounding_mode = FE_TONEAREST;
    break;
  case llvm::APFloat::rmTowardNegative:
    rounding_mode = FE_DOWNWARD;
    break;
  case llvm::APFloat::rmTowardPositive:
    rounding_mode = FE_UPWARD;
    break;
  case llvm::APFloat::rmTowardZero:
    rounding_mode = FE_TOWARDZERO;
    break;
  default:
    assert(0 && "invalid mode");
  }

  switch (getWidth()) {
  case Fl32: {
    float f = value.bitsToFloat();
    fenv_t env;
    fegetenv(&env);
    fesetround(rounding_mode);
    f = sqrtf(f);
    fesetenv(&env);
    llvm::APFloat Res(f);
    return ConstantExpr::alloc(Res);
  }
  case Fl64: {
    double d = value.bitsToDouble();
    fenv_t env;
    fegetenv(&env);
    fesetround(rounding_mode);
    d = sqrt(d);
    fesetenv(&env);
    llvm::APFloat Res(d);
    return ConstantExpr::alloc(Res);
  }
  case Fl80: {
    long double ld;
    uint64_t* arr = (uint64_t*) &ld; // on x86_64, long doubles are 80 bits, but the actual variable is 128 bits for alignment purposes
    arr[0] = value.getRawData()[0];
    arr[1] = value.getRawData()[1];
    fenv_t env;
    fegetenv(&env);
    fesetround(rounding_mode);
    ld = sqrtl(ld);
    fesetenv(&env);
    llvm::APInt Res(Fl80, ArrayRef<uint64_t>(arr, arr + 2));
    // alloc(APFloat) just does a bitcast to APInt, might as well pass the APInt directly
    return ConstantExpr::alloc(Res);
  }
  default:
    assert(0 && "Unsupported width");
  }
}

ref<ConstantExpr> ConstantExpr::FNearbyInt(llvm::APFloat::roundingMode rm) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
#else
  llvm::APFloat Res(value);
#endif
  Res.roundToIntegral(rm);
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::Add(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value + RHS->value);
}

ref<ConstantExpr> ConstantExpr::Neg() {
  return ConstantExpr::alloc(-value);
}

ref<ConstantExpr> ConstantExpr::Sub(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value - RHS->value);
}

ref<ConstantExpr> ConstantExpr::Mul(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value * RHS->value);
}

ref<ConstantExpr> ConstantExpr::UDiv(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.udiv(RHS->value));
}

ref<ConstantExpr> ConstantExpr::SDiv(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.sdiv(RHS->value));
}

ref<ConstantExpr> ConstantExpr::URem(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.urem(RHS->value));
}

ref<ConstantExpr> ConstantExpr::SRem(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.srem(RHS->value));
}

ref<ConstantExpr> ConstantExpr::And(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value & RHS->value);
}

ref<ConstantExpr> ConstantExpr::Or(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value | RHS->value);
}

ref<ConstantExpr> ConstantExpr::Xor(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value ^ RHS->value);
}

ref<ConstantExpr> ConstantExpr::Shl(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.shl(RHS->value));
}

ref<ConstantExpr> ConstantExpr::LShr(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.lshr(RHS->value));
}

ref<ConstantExpr> ConstantExpr::AShr(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.ashr(RHS->value));
}

ref<ConstantExpr> ConstantExpr::FAdd(const ref<ConstantExpr> &RHS, llvm::APFloat::roundingMode RM) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FAdd operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
  Res.add(APFloat(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue()), RM);
#else
  llvm::APFloat Res(value);
  Res.add(APFloat(RHS->getAPValue()), RM);
#endif
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FSub(const ref<ConstantExpr> &RHS, llvm::APFloat::roundingMode RM) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FSub operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
  Res.subtract(APFloat(*fpWidthToSemantics(RHS->getWidth()), RHS->getAPValue()), RM);
#else
  llvm::APFloat Res(value);
  Res.subtract(APFloat(RHS->getAPValue()), RM);
#endif
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FMul(const ref<ConstantExpr> &RHS, llvm::APFloat::roundingMode RM) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FMul operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
  Res.multiply(APFloat(*fpWidthToSemantics(RHS->getWidth()), RHS->getAPValue()), RM);
#else
  llvm::APFloat Res(value);
  Res.multiply(APFloat(RHS->getAPValue()), RM);
#endif
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FDiv(const ref<ConstantExpr> &RHS, llvm::APFloat::roundingMode RM) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FDiv operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
  Res.divide(APFloat(*fpWidthToSemantics(RHS->getWidth()), RHS->getAPValue()), RM);
#else
  llvm::APFloat Res(value);
  Res.divide(APFloat(RHS->getAPValue()), RM);
#endif
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FRem(const ref<ConstantExpr> &RHS, llvm::APFloat::roundingMode RM) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FRem operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  llvm::APFloat Res(*fpWidthToSemantics(getWidth()), value);
  Res.mod(APFloat(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue()), RM);
#else
  llvm::APFloat Res(value);
  Res.mod(APFloat(RHS->getAPValue()), RM);
#endif
  return ConstantExpr::alloc(Res);
}

ref<ConstantExpr> ConstantExpr::FMin(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FMin operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()), RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  if (CmpRes == APFloat::cmpLessThan || right.isNaN())
  {
    return ConstantExpr::alloc(left);
  }
  else
  {
    return ConstantExpr::alloc(right);
  }
}

ref<ConstantExpr> ConstantExpr::FMax(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FMax operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()), RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  if (CmpRes == APFloat::cmpLessThan || left.isNaN())
  {
    return ConstantExpr::alloc(right);
  }
  else
  {
    return ConstantExpr::alloc(left);
  }
}

ref<ConstantExpr> ConstantExpr::Not() {
  return ConstantExpr::alloc(~value);
}

ref<ConstantExpr> ConstantExpr::Eq(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value == RHS->value, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Ne(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value != RHS->value, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Ult(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.ult(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Ule(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.ule(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Ugt(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.ugt(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Uge(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.uge(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Slt(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.slt(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Sle(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.sle(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Sgt(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.sgt(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::Sge(const ref<ConstantExpr> &RHS) {
  return ConstantExpr::alloc(value.sge(RHS->value), Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOrd(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes != APFloat::cmpUnordered;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUno(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUeq(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOeq(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUgt(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpGreaterThan;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOgt(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpGreaterThan;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUge(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOge(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUlt(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpLessThan;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOlt(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpLessThan;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUle(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOle(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FUne(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

ref<ConstantExpr> ConstantExpr::FOne(const ref<ConstantExpr> &RHS) {
  if (!fpWidthToSemantics(getWidth()) ||
      !fpWidthToSemantics(RHS->getWidth()))
    klee_error("Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
  APFloat left(*fpWidthToSemantics(getWidth()), value);
  APFloat right(*fpWidthToSemantics(RHS->getWidth()),RHS->getAPValue());
#else
  APFloat left(value);
  APFloat right(RHS->getAPValue());
#endif
  APFloat::cmpResult CmpRes = left.compare(right);

  bool Result = CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
  return ConstantExpr::alloc(Result, Expr::Bool);
}

/***/

ref<Expr>  NotOptimizedExpr::create(ref<Expr> src) {
  return NotOptimizedExpr::alloc(src);
}

/***/

Array::Array(const std::string &_name, uint64_t _size,
             const ref<ConstantExpr> *constantValuesBegin,
             const ref<ConstantExpr> *constantValuesEnd, Expr::Width _domain,
             Expr::Width _range)
    : name(_name), size(_size), domain(_domain), range(_range),
      constantValues(constantValuesBegin, constantValuesEnd) {

  assert((isSymbolicArray() || constantValues.size() == size) &&
         "Invalid size for constant array!");
  computeHash();
#ifndef NDEBUG
  for (const ref<ConstantExpr> *it = constantValuesBegin;
       it != constantValuesEnd; ++it)
    assert((*it)->getWidth() == getRange() &&
           "Invalid initial constant value!");
#endif // NDEBUG
}

Array::~Array() {
}

unsigned Array::computeHash() {
  unsigned res = 0;
  for (unsigned i = 0, e = name.size(); i != e; ++i)
    res = (res * Expr::MAGIC_HASH_CONSTANT) + name[i];
  res = (res * Expr::MAGIC_HASH_CONSTANT) + size;
  hashValue = res;
  return hashValue; 
}
/***/

ref<Expr> ReadExpr::create(const UpdateList &ul, ref<Expr> index) {
  // rollback index when possible... 

  // XXX this doesn't really belong here... there are basically two
  // cases, one is rebuild, where we want to optimistically try various
  // optimizations when the index has changed, and the other is 
  // initial creation, where we expect the ObjectState to have constructed
  // a smart UpdateList so it is not worth rescanning.

  const UpdateNode *un = ul.head;
  for (; un; un=un->next) {
    ref<Expr> cond = EqExpr::create(index, un->index);
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
      if (CE->isTrue())
        return un->value;
    } else {
      break;
    }
  }

  return ReadExpr::alloc(ul, index);
}

int ReadExpr::compareContents(const Expr &b) const { 
  return updates.compare(static_cast<const ReadExpr&>(b).updates);
}

ref<Expr> SelectExpr::create(ref<Expr> c, ref<Expr> t, ref<Expr> f) {
  Expr::Width kt = t->getWidth();

  assert(c->getWidth()==Bool && "type mismatch");
  assert(kt==f->getWidth() && "type mismatch");

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(c)) {
    return CE->isTrue() ? t : f;
  } else if (t==f) {
    return t;
  } else if (kt==Expr::Bool) { // c ? t : f  <=> (c and t) or (not c and f)
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(t)) {      
      if (CE->isTrue()) {
        return OrExpr::create(c, f);
      } else {
        return AndExpr::create(Expr::createIsZero(c), f);
      }
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(f)) {
      if (CE->isTrue()) {
        return OrExpr::create(Expr::createIsZero(c), t);
      } else {
        return AndExpr::create(c, t);
      }
    }
  }
  
  return SelectExpr::alloc(c, t, f);
}

/***/

ref<Expr> ConcatExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  Expr::Width w = l->getWidth() + r->getWidth();
  
  // Fold concatenation of constants.
  //
  // FIXME: concat 0 x -> zext x ?
  if (ConstantExpr *lCE = dyn_cast<ConstantExpr>(l))
    if (ConstantExpr *rCE = dyn_cast<ConstantExpr>(r))
      return lCE->Concat(rCE);

  // Merge contiguous Extracts
  if (ExtractExpr *ee_left = dyn_cast<ExtractExpr>(l)) {
    if (ExtractExpr *ee_right = dyn_cast<ExtractExpr>(r)) {
      if (ee_left->expr == ee_right->expr &&
          ee_right->offset + ee_right->width == ee_left->offset) {
        return ExtractExpr::create(ee_left->expr, ee_right->offset, w);
      }
    }
  }

  return ConcatExpr::alloc(l, r);
}

/// Shortcut to concat N kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::createN(unsigned n_kids, const ref<Expr> kids[]) {
  assert(n_kids > 0);
  if (n_kids == 1)
    return kids[0];
  
  ref<Expr> r = ConcatExpr::create(kids[n_kids-2], kids[n_kids-1]);
  for (int i=n_kids-3; i>=0; i--)
    r = ConcatExpr::create(kids[i], r);
  return r;
}

/// Shortcut to concat 4 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create4(const ref<Expr> &kid1, const ref<Expr> &kid2,
                              const ref<Expr> &kid3, const ref<Expr> &kid4) {
  return ConcatExpr::create(kid1, ConcatExpr::create(kid2, ConcatExpr::create(kid3, kid4)));
}

/// Shortcut to concat 8 kids.  The chain returned is unbalanced to the right
ref<Expr> ConcatExpr::create8(const ref<Expr> &kid1, const ref<Expr> &kid2,
			      const ref<Expr> &kid3, const ref<Expr> &kid4,
			      const ref<Expr> &kid5, const ref<Expr> &kid6,
			      const ref<Expr> &kid7, const ref<Expr> &kid8) {
  return ConcatExpr::create(kid1, ConcatExpr::create(kid2, ConcatExpr::create(kid3, 
			      ConcatExpr::create(kid4, ConcatExpr::create4(kid5, kid6, kid7, kid8)))));
}

/***/

ref<Expr> ExtractExpr::create(ref<Expr> expr, unsigned off, Width w) {
  unsigned kw = expr->getWidth();
  assert(w > 0 && off + w <= kw && "invalid extract");
  
  if (w == kw) {
    return expr;
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    return CE->Extract(off, w);
  } else {
    // Extract(Concat)
    if (ConcatExpr *ce = dyn_cast<ConcatExpr>(expr)) {
      // if the extract skips the right side of the concat
      if (off >= ce->getRight()->getWidth())
	return ExtractExpr::create(ce->getLeft(), off - ce->getRight()->getWidth(), w);
      
      // if the extract skips the left side of the concat
      if (off + w <= ce->getRight()->getWidth())
	return ExtractExpr::create(ce->getRight(), off, w);

      // E(C(x,y)) = C(E(x), E(y))
      return ConcatExpr::create(ExtractExpr::create(ce->getKid(0), 0, w - ce->getKid(1)->getWidth() + off),
				ExtractExpr::create(ce->getKid(1), off, ce->getKid(1)->getWidth() - off));
    }
  }
  
  return ExtractExpr::alloc(expr, off, w);
}

/***/

ref<Expr> NotExpr::create(const ref<Expr> &e) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE->Not();
  
  return NotExpr::alloc(e);
}


/***/

ref<Expr> ZExtExpr::create(const ref<Expr> &e, Width w) {
  unsigned kBits = e->getWidth();
  if (w == kBits) {
    return e;
  } else if (w < kBits) { // trunc
    return ExtractExpr::create(e, 0, w);
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    return CE->ZExt(w);
  } else {
    return ZExtExpr::alloc(e, w);
  }
}

ref<Expr> SExtExpr::create(const ref<Expr> &e, Width w) {
  unsigned kBits = e->getWidth();
  if (w == kBits) {
    return e;
  } else if (w < kBits) { // trunc
    return ExtractExpr::create(e, 0, w);
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    return CE->SExt(w);
  } else {    
    return SExtExpr::alloc(e, w);
  }
}

/***/

static ref<Expr> AndExpr_create(Expr *l, Expr *r);
static ref<Expr> XorExpr_create(Expr *l, Expr *r);

static ref<Expr> EqExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr);
static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);
static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r);

static ref<Expr> AddExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type==Expr::Bool) {
    return XorExpr_createPartialR(cl, r);
  } else if (cl->isZero()) {
    return r;
  } else {
    Expr::Kind rk = r->getKind();
    if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // A + (B+c) == (A+B) + c
      return AddExpr::create(AddExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // A + (B-c) == (A+B) - c
      return SubExpr::create(AddExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else {
      return AddExpr::alloc(cl, r);
    }
  }
}
static ref<Expr> AddExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return AddExpr_createPartialR(cr, l);
}
static ref<Expr> AddExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();

  if (type == Expr::Bool) {
    return XorExpr_create(l, r);
  } else {
    Expr::Kind lk = l->getKind(), rk = r->getKind();
    if (lk==Expr::Add && isa<ConstantExpr>(l->getKid(0))) { // (k+a)+b = k+(a+b)
      return AddExpr::create(l->getKid(0),
                             AddExpr::create(l->getKid(1), r));
    } else if (lk==Expr::Sub && isa<ConstantExpr>(l->getKid(0))) { // (k-a)+b = k+(b-a)
      return AddExpr::create(l->getKid(0),
                             SubExpr::create(r, l->getKid(1)));
    } else if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // a + (k+b) = k+(a+b)
      return AddExpr::create(r->getKid(0),
                             AddExpr::create(l, r->getKid(1)));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // a + (k-b) = k+(a-b)
      return AddExpr::create(r->getKid(0),
                             SubExpr::create(l, r->getKid(1)));
    } else {
      return AddExpr::alloc(l, r);
    }
  }  
}

static ref<Expr> SubExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type==Expr::Bool) {
    return XorExpr_createPartialR(cl, r);
  } else {
    Expr::Kind rk = r->getKind();
    if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // A - (B+c) == (A-B) - c
      return SubExpr::create(SubExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // A - (B-c) == (A-B) + c
      return AddExpr::create(SubExpr::create(cl, r->getKid(0)),
                             r->getKid(1));
    } else {
      return SubExpr::alloc(cl, r);
    }
  }
}
static ref<Expr> SubExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  // l - c => l + (-c)
  return AddExpr_createPartial(l, 
                               ConstantExpr::alloc(0, cr->getWidth())->Sub(cr));
}
static ref<Expr> SubExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();

  if (type == Expr::Bool) {
    return XorExpr_create(l, r);
  } else if (*l==*r) {
    return ConstantExpr::alloc(0, type);
  } else {
    Expr::Kind lk = l->getKind(), rk = r->getKind();
    if (lk==Expr::Add && isa<ConstantExpr>(l->getKid(0))) { // (k+a)-b = k+(a-b)
      return AddExpr::create(l->getKid(0),
                             SubExpr::create(l->getKid(1), r));
    } else if (lk==Expr::Sub && isa<ConstantExpr>(l->getKid(0))) { // (k-a)-b = k-(a+b)
      return SubExpr::create(l->getKid(0),
                             AddExpr::create(l->getKid(1), r));
    } else if (rk==Expr::Add && isa<ConstantExpr>(r->getKid(0))) { // a - (k+b) = (a-c) - k
      return SubExpr::create(SubExpr::create(l, r->getKid(1)),
                             r->getKid(0));
    } else if (rk==Expr::Sub && isa<ConstantExpr>(r->getKid(0))) { // a - (k-b) = (a+b) - k
      return SubExpr::create(AddExpr::create(l, r->getKid(1)),
                             r->getKid(0));
    } else {
      return SubExpr::alloc(l, r);
    }
  }  
}

static ref<Expr> MulExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  Expr::Width type = cl->getWidth();

  if (type == Expr::Bool) {
    return AndExpr_createPartialR(cl, r);
  } else if (cl->isOne()) {
    return r;
  } else if (cl->isZero()) {
    return cl;
  } else {
    return MulExpr::alloc(cl, r);
  }
}
static ref<Expr> MulExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return MulExpr_createPartialR(cr, l);
}
static ref<Expr> MulExpr_create(Expr *l, Expr *r) {
  Expr::Width type = l->getWidth();
  
  if (type == Expr::Bool) {
    return AndExpr::alloc(l, r);
  } else {
    return MulExpr::alloc(l, r);
  }
}

static ref<Expr> AndExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  if (cr->isAllOnes()) {
    return l;
  } else if (cr->isZero()) {
    return cr;
  } else {
    return AndExpr::alloc(l, cr);
  }
}
static ref<Expr> AndExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  return AndExpr_createPartial(r, cl);
}
static ref<Expr> AndExpr_create(Expr *l, Expr *r) {
  return AndExpr::alloc(l, r);
}

static ref<Expr> OrExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  if (cr->isAllOnes()) {
    return cr;
  } else if (cr->isZero()) {
    return l;
  } else {
    return OrExpr::alloc(l, cr);
  }
}
static ref<Expr> OrExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  return OrExpr_createPartial(r, cl);
}
static ref<Expr> OrExpr_create(Expr *l, Expr *r) {
  return OrExpr::alloc(l, r);
}

static ref<Expr> XorExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {
  if (cl->isZero()) {
    return r;
  } else if (cl->getWidth() == Expr::Bool) {
    return EqExpr_createPartial(r, ConstantExpr::create(0, Expr::Bool));
  } else {
    return XorExpr::alloc(cl, r);
  }
}

static ref<Expr> XorExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {
  return XorExpr_createPartialR(cr, l);
}
static ref<Expr> XorExpr_create(Expr *l, Expr *r) {
  return XorExpr::alloc(l, r);
}

static ref<Expr> UDivExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return l;
  } else{
    return UDivExpr::alloc(l, r);
  }
}

static ref<Expr> SDivExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return l;
  } else{
    return SDivExpr::alloc(l, r);
  }
}

static ref<Expr> URemExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return ConstantExpr::create(0, Expr::Bool);
  } else{
    return URemExpr::alloc(l, r);
  }
}

static ref<Expr> SRemExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // r must be 1
    return ConstantExpr::create(0, Expr::Bool);
  } else{
    return SRemExpr::alloc(l, r);
  }
}

static ref<Expr> ShlExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l & !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else{
    return ShlExpr::alloc(l, r);
  }
}

static ref<Expr> LShrExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l & !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else{
    return LShrExpr::alloc(l, r);
  }
}

static ref<Expr> AShrExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l
    return l;
  } else{
    return AShrExpr::alloc(l, r);
  }
}

#define BCREATE_R(_e_op, _op, partialL, partialR) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) { \
  assert(l->getWidth()==r->getWidth() && "type mismatch");              \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {                   \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                   \
      return cl->_op(cr);                                               \
    return _e_op ## _createPartialR(cl, r.get());                       \
  } else if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {            \
    return _e_op ## _createPartial(l.get(), cr);                        \
  }                                                                     \
  return _e_op ## _create(l.get(), r.get());                            \
}

#define BCREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) { \
  assert(l->getWidth()==r->getWidth() && "type mismatch");          \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                 \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))               \
      return cl->_op(cr);                                           \
  return _e_op ## _create(l, r);                                    \
}

BCREATE_R(AddExpr, Add, AddExpr_createPartial, AddExpr_createPartialR)
BCREATE_R(SubExpr, Sub, SubExpr_createPartial, SubExpr_createPartialR)
BCREATE_R(MulExpr, Mul, MulExpr_createPartial, MulExpr_createPartialR)
BCREATE_R(AndExpr, And, AndExpr_createPartial, AndExpr_createPartialR)
BCREATE_R(OrExpr, Or, OrExpr_createPartial, OrExpr_createPartialR)
BCREATE_R(XorExpr, Xor, XorExpr_createPartial, XorExpr_createPartialR)
BCREATE(UDivExpr, UDiv)
BCREATE(SDivExpr, SDiv)
BCREATE(URemExpr, URem)
BCREATE(SRemExpr, SRem)
BCREATE(ShlExpr, Shl)
BCREATE(LShrExpr, LShr)
BCREATE(AShrExpr, AShr)

#define UCREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &e) {     \
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(e)) \
      return ce->_op();                             \
  return _e_op ## _create(e);                       \
}

static ref<Expr> FAbsExpr_create(const ref<Expr> &e) { return FAbsExpr::alloc(e); }
static ref<Expr> FpClassifyExpr_create(const ref<Expr> &e) { return FpClassifyExpr::alloc(e); }
static ref<Expr> FIsFiniteExpr_create(const ref<Expr> &e) { return FIsFiniteExpr::alloc(e); }
static ref<Expr> FIsNanExpr_create(const ref<Expr> &e) { return FIsNanExpr::alloc(e); }
static ref<Expr> FIsInfExpr_create(const ref<Expr> &e) { return FIsInfExpr::alloc(e); }

UCREATE(FAbsExpr, FAbs)
UCREATE(FpClassifyExpr, FpClassify)
UCREATE(FIsFiniteExpr, FIsFinite)
UCREATE(FIsNanExpr, FIsNan)
UCREATE(FIsInfExpr, FIsInf)

#define U_RM_CREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &e, llvm::APFloat::roundingMode rm) { \
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(e))                             \
      return ce->_op(rm);                                                       \
  return _e_op ## _create(e, rm);                                               \
}

static ref<Expr> FSqrtExpr_create(const ref<Expr> &e, llvm::APFloat::roundingMode rm) { return FSqrtExpr::alloc(e, rm); }
static ref<Expr> FNearbyIntExpr_create(const ref<Expr> &e, llvm::APFloat::roundingMode rm) { return FNearbyIntExpr::alloc(e, rm); }

U_RM_CREATE(FSqrtExpr, FSqrt)
U_RM_CREATE(FNearbyIntExpr, FNearbyInt)

#define CMPCREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) { \
  assert(l->getWidth()==r->getWidth() && "type mismatch");              \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                     \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                   \
      return cl->_op(cr);                                               \
  return _e_op ## _create(l, r);                                        \
}

#define CMPCREATE_T(_e_op, _op, _reflexive_e_op, partialL, partialR) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r) {    \
  assert(l->getWidth()==r->getWidth() && "type mismatch");             \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l)) {                  \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                  \
      return cl->_op(cr);                                              \
    return partialR(cl, r.get());                                      \
  } else if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r)) {           \
    return partialL(l.get(), cr);                                      \
  } else {                                                             \
    return _e_op ## _create(l.get(), r.get());                         \
  }                                                                    \
}
  

static ref<Expr> EqExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l == r) {
    return ConstantExpr::alloc(1, Expr::Bool);
  } else {
    return EqExpr::alloc(l, r);
  }
}


/// Tries to optimize EqExpr cl == rd, where cl is a ConstantExpr and
/// rd a ReadExpr.  If rd is a read into an all-constant array,
/// returns a disjunction of equalities on the index.  Otherwise,
/// returns the initial equality expression. 
static ref<Expr> TryConstArrayOpt(const ref<ConstantExpr> &cl, 
				  ReadExpr *rd) {
  if (rd->updates.root->isSymbolicArray() || rd->updates.getSize())
    return EqExpr_create(cl, rd);

  // Number of positions in the array that contain value ct.
  unsigned numMatches = 0;

  // for now, just assume standard "flushing" of a concrete array,
  // where the concrete array has one update for each index, in order
  ref<Expr> res = ConstantExpr::alloc(0, Expr::Bool);
  for (unsigned i = 0, e = rd->updates.root->size; i != e; ++i) {
    if (cl == rd->updates.root->constantValues[i]) {
      // Arbitrary maximum on the size of disjunction.
      if (++numMatches > 100)
        return EqExpr_create(cl, rd);
      
      ref<Expr> mayBe = 
        EqExpr::create(rd->index, ConstantExpr::alloc(i, 
                                                      rd->index->getWidth()));
      res = OrExpr::create(res, mayBe);
    }
  }

  return res;
}

static ref<Expr> EqExpr_createPartialR(const ref<ConstantExpr> &cl, Expr *r) {  
  Expr::Width width = cl->getWidth();

  Expr::Kind rk = r->getKind();
  if (width == Expr::Bool) {
    if (cl->isTrue()) {
      return r;
    } else {
      // 0 == ...
      
      if (rk == Expr::Eq) {
        const EqExpr *ree = cast<EqExpr>(r);

        // eliminate double negation
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ree->left)) {
          // 0 == (0 == A) => A
          if (CE->getWidth() == Expr::Bool &&
              CE->isFalse())
            return ree->right;
        }
      } else if (rk == Expr::Or) {
        const OrExpr *roe = cast<OrExpr>(r);

        // transform not(or(a,b)) to and(not a, not b)
        return AndExpr::create(Expr::createIsZero(roe->left),
                               Expr::createIsZero(roe->right));
      }
    }
  } else if (rk == Expr::SExt) {
    // (sext(a,T)==c) == (a==c)
    const SExtExpr *see = cast<SExtExpr>(r);
    Expr::Width fromBits = see->src->getWidth();
    ref<ConstantExpr> trunc = cl->ZExt(fromBits);

    // pathological check, make sure it is possible to
    // sext to this value *from any value*
    if (cl == trunc->SExt(width)) {
      return EqExpr::create(see->src, trunc);
    } else {
      return ConstantExpr::create(0, Expr::Bool);
    }
  } else if (rk == Expr::ZExt) {
    // (zext(a,T)==c) == (a==c)
    const ZExtExpr *zee = cast<ZExtExpr>(r);
    Expr::Width fromBits = zee->src->getWidth();
    ref<ConstantExpr> trunc = cl->ZExt(fromBits);
    
    // pathological check, make sure it is possible to
    // zext to this value *from any value*
    if (cl == trunc->ZExt(width)) {
      return EqExpr::create(zee->src, trunc);
    } else {
      return ConstantExpr::create(0, Expr::Bool);
    }
  } else if (rk==Expr::Add) {
    const AddExpr *ae = cast<AddExpr>(r);
    if (isa<ConstantExpr>(ae->left)) {
      // c0 = c1 + b => c0 - c1 = b
      return EqExpr_createPartialR(cast<ConstantExpr>(SubExpr::create(cl, 
                                                                      ae->left)),
                                   ae->right.get());
    }
  } else if (rk==Expr::Sub) {
    const SubExpr *se = cast<SubExpr>(r);
    if (isa<ConstantExpr>(se->left)) {
      // c0 = c1 - b => c1 - c0 = b
      return EqExpr_createPartialR(cast<ConstantExpr>(SubExpr::create(se->left, 
                                                                      cl)),
                                   se->right.get());
    }
  } else if (rk == Expr::Read && ConstArrayOpt) {
    return TryConstArrayOpt(cl, static_cast<ReadExpr*>(r));
  }
    
  return EqExpr_create(cl, r);
}

static ref<Expr> EqExpr_createPartial(Expr *l, const ref<ConstantExpr> &cr) {  
  return EqExpr_createPartialR(cr, l);
}
  
ref<Expr> NeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return EqExpr::create(ConstantExpr::create(0, Expr::Bool),
                        EqExpr::create(l, r));
}

ref<Expr> UgtExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return UltExpr::create(r, l);
}
ref<Expr> UgeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return UleExpr::create(r, l);
}

ref<Expr> SgtExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return SltExpr::create(r, l);
}
ref<Expr> SgeExpr::create(const ref<Expr> &l, const ref<Expr> &r) {
  return SleExpr::create(r, l);
}

static ref<Expr> UltExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  Expr::Width t = l->getWidth();
  if (t == Expr::Bool) { // !l && r
    return AndExpr::create(Expr::createIsZero(l), r);
  } else {
    return UltExpr::alloc(l, r);
  }
}

static ref<Expr> UleExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // !(l && !r)
    return OrExpr::create(Expr::createIsZero(l), r);
  } else {
    return UleExpr::alloc(l, r);
  }
}

static ref<Expr> SltExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // l && !r
    return AndExpr::create(l, Expr::createIsZero(r));
  } else {
    return SltExpr::alloc(l, r);
  }
}

static ref<Expr> SleExpr_create(const ref<Expr> &l, const ref<Expr> &r) {
  if (l->getWidth() == Expr::Bool) { // !(!l && r)
    return OrExpr::create(l, Expr::createIsZero(r));
  } else {
    return SleExpr::alloc(l, r);
  }
}

CMPCREATE_T(EqExpr, Eq, EqExpr, EqExpr_createPartial, EqExpr_createPartialR)
CMPCREATE(UltExpr, Ult)
CMPCREATE(UleExpr, Ule)
CMPCREATE(SltExpr, Slt)
CMPCREATE(SleExpr, Sle)

#define CAST_RM_CREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &e, Width w, llvm::APFloat::roundingMode rm) { \
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(e))                             \
    return ce->_op(w, rm);                                                      \
  return _e_op ## _create(e, w, rm);                                            \
}

static ref<Expr> FExtExpr_create(const ref<Expr> &e, Expr::Width w, llvm::APFloat::roundingMode rm) { return FExtExpr::alloc(e,w, rm); }
static ref<Expr> FToUExpr_create(const ref<Expr> &e, Expr::Width w, llvm::APFloat::roundingMode rm) { return FToUExpr::alloc(e,w, rm); }
static ref<Expr> FToSExpr_create(const ref<Expr> &e, Expr::Width w, llvm::APFloat::roundingMode rm) { return FToSExpr::alloc(e,w, rm); }
static ref<Expr> UToFExpr_create(const ref<Expr> &e, Expr::Width w, llvm::APFloat::roundingMode rm) { return UToFExpr::alloc(e,w, rm); }
static ref<Expr> SToFExpr_create(const ref<Expr> &e, Expr::Width w, llvm::APFloat::roundingMode rm) { return SToFExpr::alloc(e,w, rm); }

CAST_RM_CREATE(FExtExpr, FExt)
CAST_RM_CREATE(FToUExpr, FToU)
CAST_RM_CREATE(FToSExpr, FToS)
CAST_RM_CREATE(UToFExpr, UToF)
CAST_RM_CREATE(SToFExpr, SToF)

#define B_RM_CREATE(_e_op, _op) \
ref<Expr>  _e_op ::create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { \
  assert(l->getWidth()==r->getWidth() && "type mismatch");                                 \
  if (ConstantExpr *cl = dyn_cast<ConstantExpr>(l))                                        \
    if (ConstantExpr *cr = dyn_cast<ConstantExpr>(r))                                      \
      return cl->_op(cr, rm);                                                              \
  return _e_op ## _create(l, r, rm);                                                       \
}

static ref<Expr> FAddExpr_create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { return FAddExpr::alloc(l,r, rm); }
static ref<Expr> FSubExpr_create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { return FSubExpr::alloc(l,r, rm); }
static ref<Expr> FMulExpr_create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { return FMulExpr::alloc(l,r, rm); }
static ref<Expr> FDivExpr_create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { return FDivExpr::alloc(l,r, rm); }
static ref<Expr> FRemExpr_create(const ref<Expr> &l, const ref<Expr> &r, llvm::APFloat::roundingMode rm) { return FRemExpr::alloc(l,r, rm); }
static ref<Expr> FMinExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FMinExpr::alloc(l, r); }
static ref<Expr> FMaxExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FMaxExpr::alloc(l, r); }

B_RM_CREATE(FAddExpr, FAdd)
B_RM_CREATE(FSubExpr, FSub)
B_RM_CREATE(FMulExpr, FMul)
B_RM_CREATE(FDivExpr, FDiv)
B_RM_CREATE(FRemExpr, FRem)
BCREATE(FMinExpr, FMin)
BCREATE(FMaxExpr, FMax)

static ref<Expr> FOrdExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOrdExpr::alloc(l,r); }
static ref<Expr> FUnoExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUnoExpr::alloc(l,r); }
static ref<Expr> FUeqExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUeqExpr::alloc(l,r); }
static ref<Expr> FOeqExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOeqExpr::alloc(l,r); }
static ref<Expr> FUgtExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUgtExpr::alloc(l,r); }
static ref<Expr> FOgtExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOgtExpr::alloc(l,r); }
static ref<Expr> FUgeExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUgeExpr::alloc(l,r); }
static ref<Expr> FOgeExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOgeExpr::alloc(l,r); }
static ref<Expr> FUltExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUltExpr::alloc(l,r); }
static ref<Expr> FOltExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOltExpr::alloc(l,r); }
static ref<Expr> FUleExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUleExpr::alloc(l,r); }
static ref<Expr> FOleExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOleExpr::alloc(l,r); }
static ref<Expr> FUneExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FUneExpr::alloc(l,r); }
static ref<Expr> FOneExpr_create(const ref<Expr> &l, const ref<Expr> &r) { return FOneExpr::alloc(l,r); }

CMPCREATE(FOrdExpr, FOrd)
CMPCREATE(FUnoExpr, FUno)
CMPCREATE(FUeqExpr, FUeq)
CMPCREATE(FOeqExpr, FOeq)
CMPCREATE(FUgtExpr, FUgt)
CMPCREATE(FOgtExpr, FOgt)
CMPCREATE(FUgeExpr, FUge)
CMPCREATE(FOgeExpr, FOge)
CMPCREATE(FUltExpr, FUlt)
CMPCREATE(FOltExpr, FOlt)
CMPCREATE(FUleExpr, FUle)
CMPCREATE(FOleExpr, FOle)
CMPCREATE(FUneExpr, FUne)
CMPCREATE(FOneExpr, FOne)
