//# Random.cc: Random number classes
//# Copyright (C) 1992,1993,1994,1995,1998,1999,2000,2001
//# Associated Universities, Inc. Washington DC, USA.
//#
//# This library is free software; you can redistribute it and/or modify it
//# under the terms of the GNU Library General Public License as published by
//# the Free Software Foundation; either version 2 of the License, or (at your
//# option) any later version.
//#
//# This library is distributed in the hope that it will be useful, but WITHOUT
//# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
//# License for more details.
//#
//# You should have received a copy of the GNU Library General Public License
//# along with this library; if not, write to the Free Software Foundation,
//# Inc., 675 Massachusetts Ave, Cambridge, MA 02139, USA.
//#
//# Correspondence concerning AIPS++ should be addressed as follows:
//#        Internet email: aips2-request@nrao.edu.
//#        Postal address: AIPS++ Project Office
//#                        National Radio Astronomy Observatory
//#                        520 Edgemont Road
//#                        Charlottesville, VA 22903-2475 USA
//#
//# $Id$

#include <aips/Mathematics/Random.h>
#include <aips/Mathematics/Constants.h>
#include <aips/Exceptions/Error.h>
#include <aips/Utilities/Assert.h>
#include <aips/Utilities/String.h>
#include <aips/Utilities/PtrHolder.h>
#include <aips/Arrays/Vector.h>

RNG::~RNG() {
}

Float RNG::asFloat() {
  // used to access floats as unsigned Int's
  union PrivateRNGSingleType {
    Float flt;
    uInt intgr;
  };
  PrivateRNGSingleType result;
  result.flt = 1.0f;
  result.intgr |= (asuInt() & 0x7fffff);
  result.flt -= 1.0;
  AlwaysAssert(result.flt < 1.0f && result.flt >= 0.0f, AipsError);
  return result.flt;
}
        
Double RNG::asDouble() {
  // used to access Doubles as two unsigned integers
  union PrivateRNGDoubleType {
    Double dbl;
    uInt intgr[2];
  };

  PrivateRNGDoubleType result;
  result.dbl = 1.0;
  uInt iMsb = asuInt() & 0xfffff;
  uInt iLsb = asuInt();
#if defined(AIPS_LITTLE_ENDIAN)
  result.intgr[0] |= iLsb;
  result.intgr[1] |= iMsb;
# else
  result.intgr[1] |= iLsb;
  result.intgr[0] |= iMsb;
#endif
  result.dbl -= 1.0;
  AlwaysAssert(result.dbl < 1.0f && result.dbl >= 0.0f, AipsError);
  return result.dbl;
}

//
//      This is an extension of the older implementation of Algorithm M
//      which I previously supplied. The main difference between this
//      version and the old code are:
//
//              + Andres searched high & low for good constants for
//                the LCG.
//
//              + theres more bit chopping going on.
//
//      The following contains his comments.
//
//      agn@UNH.CS.CMU.EDU sez..
//      
//      The generator below is based on 2 well known
//      methods: Linear Congruential (LCGs) and Additive
//      Congruential generators (ACGs).
//      
//      The LCG produces the longest possible sequence
//      of 32 bit random numbers, each being unique in
//      that sequence (it has only 32 bits of state).
//      It suffers from 2 problems: a) Independence
//      isnt great, that is the (n+1)th number is
//      somewhat related to the preceding one, unlike
//      flipping a coin where knowing the past outcomes
//      dont help to predict the next result.  b)
//      Taking parts of a LCG generated number can be
//      quite non-random: for example, looking at only
//      the least significant byte gives a permuted
//      8-bit counter (that has a period length of only
//      256).  The advantage of an LCA is that it is
//      perfectly uniform when run for the entire period
//      length (and very uniform for smaller sequences
//      too, if the parameters are chosen carefully).
//      
//      ACGs have extremly long period lengths and
//      provide good independence.  Unfortunately,
//      uniformity isnt not too great. Furthermore, I
//      didnt find any theoretically analysis of ACGs
//      that addresses uniformity.
//      
//      The RNG given below will return numbers
//      generated by an LCA that are permuted under
//      control of a ACG. 2 permutations take place: the
//      4 bytes of one LCG generated number are
//      subjected to one of 16 permutations selected by
//      4 bits of the ACG. The permutation a such that
//      byte of the result may come from each byte of
//      the LCG number. This effectively destroys the
//      structure within a word. Finally, the sequence
//      of such numbers is permuted within a range of
//      256 numbers. This greatly improves independence.
//      
//
//  Algorithm M as describes in Knuths "Art of Computer Programming",
//      Vol 2. 1969
//  is used with a linear congruential generator (to get a good uniform
//  distribution) that is permuted with a Fibonacci additive congruential
//  generator to get good independence.
//
//  Bit, byte, and word distributions were extensively tested and pass
//  Chi-squared test near perfect scores (>7E8 numbers tested, Uniformity
//  assumption holds with probability > 0.999)
//
//  Run-up tests for on 7E8 numbers confirm independence with
//  probability > 0.97.
//
//  Plotting random points in 2d reveals no apparent structure.
//
//  Autocorrelation on sequences of 5E5 numbers (A(i) = SUM X(n)*X(n-i),
//      i=1..512)
//  results in no obvious structure (A(i) ~ const).
//
//  Except for speed and memory requirements, this generator outperforms
//  random() for all tests. (random() scored rather low on uniformity tests,
//  while independence test differences were less dramatic).
//
//  AGN would like to..
//  thanks to M.Mauldin, H.Walker, J.Saxe and M.Molloy for inspiration & help.
//
//  And I would (DGC) would like to thank Donald Kunth for AGN for letting me
//  use his extensions in this implementation.
//

//      Part of the table on page 28 of Knuth, vol II. This allows us
//      to adjust the size of the table at the expense of shorter sequences.
static Int randomStateTable[][3] = {
  {3,7,16}, 
  {4,9, 32}, 
  {3,10, 32},
  {1,11, 32}, 
  {1,15,64}, 
  {3,17,128},
  {7,18,128}, 
  {3,20,128},
  {2,21, 128},
  {1,22, 128},
  {5,23, 128},
  {3,25, 128},
  {2,29, 128},
  {3,31, 128},
  {13,33, 256},
  {2,35, 256},
  {11,36, 256},
  {14,39,256},
  {3,41,256},
  {9,49,256},
  {3,52,256}, 
  {24,55,256}, 
  {7,57, 256},
  {19,58,256}, 
  {38,89,512},
  {17,95,512}, 
  {6,97,512}, 
  {11,98,512}, 
  {-1,-1,-1}
};

// spatial permutation table
//      RANDOM_PERM_SIZE must be a power of two
#define RANDOM_PERM_SIZE 64
uInt randomPermutations[RANDOM_PERM_SIZE] = {
0xffffffff, 0x00000000,  0x00000000,  0x00000000,  // 3210
0x0000ffff, 0x00ff0000,  0x00000000,  0xff000000,  // 2310
0xff0000ff, 0x0000ff00,  0x00000000,  0x00ff0000,  // 3120
0x00ff00ff, 0x00000000,  0xff00ff00,  0x00000000,  // 1230

0xffff0000, 0x000000ff,  0x00000000,  0x0000ff00,  // 3201
0x00000000, 0x00ff00ff,  0x00000000,  0xff00ff00,  // 2301
0xff000000, 0x00000000,  0x000000ff,  0x00ffff00,  // 3102
0x00000000, 0x00000000,  0x00000000,  0xffffffff,  // 2103

0xff00ff00, 0x00000000,  0x00ff00ff,  0x00000000,  // 3012
0x0000ff00, 0x00000000,  0x00ff0000,  0xff0000ff,  // 2013
0x00000000, 0x00000000,  0xffffffff,  0x00000000,  // 1032
0x00000000, 0x0000ff00,  0xffff0000,  0x000000ff,  // 1023

0x00000000, 0xffffffff,  0x00000000,  0x00000000,  // 0321
0x00ffff00, 0xff000000,  0x00000000,  0x000000ff,  // 0213
0x00000000, 0xff000000,  0x0000ffff,  0x00ff0000,  // 0132
0x00000000, 0xff00ff00,  0x00000000,  0x00ff00ff   // 0123
};

//      SEED_TABLE_SIZE must be a power of 2
#define SEED_TABLE_SIZE 32
static uInt seedTable[SEED_TABLE_SIZE] = {
  0xbdcc47e5, 0x54aea45d, 0xec0df859, 0xda84637b,
  0xc8c6cb4f, 0x35574b01, 0x28260b7d, 0x0d07fdbf,
  0x9faaeeb0, 0x613dd169, 0x5ce2d818, 0x85b9e706,
  0xab2469db, 0xda02b0dc, 0x45c60d6e, 0xffe49d10,
  0x7224fea3, 0xf9684fc9, 0xfc7ee074, 0x326ce92a,
  0x366d13b5, 0x17aaa731, 0xeb83a675, 0x7781cb32,
  0x4ec7c92d, 0x7f187521, 0x2cf346b4, 0xad13310f,
  0xb89cff2b, 0x12164de1, 0xa865168d, 0x32b56cdf
};

//      The LCG used to scramble the ACG
//
// LC-parameter selection follows recommendations in 
// "Handbook of Mathematical Functions" by Abramowitz & Stegun 10th, edi.
//
// LC_A = 251^2, ~= sqrt(2^32) = 66049
// LC_C = result of a long trial & error series = 3907864577
static const uInt LC_A = 66049;
static const uInt LC_C = 3907864577u;
static inline uInt LCG(uInt x) {
  return x * LC_A + LC_C;
}

ACG::ACG(uInt seed, Int size) 
  :itsInitSeed(seed),
   itsInitTblEntry(0),
   itsStatePtr(0),
   itsAuxStatePtr(0),
   itsStateSize(0),
   itsAuxSize(0),
   lcgRecurr(0),
   itsJ(0),
   itsK(0)
{
  //    Determine the size of the state table
  Int l;
  for (l = 0; 
       randomStateTable[l][0] != -1 && randomStateTable[l][1] < size;
       l++);
  
  if (randomStateTable[l][1] == -1) {
    l--;
  }

  itsInitTblEntry = l;
  itsStateSize = randomStateTable[ itsInitTblEntry ][ 1 ];
  itsAuxSize = randomStateTable[ itsInitTblEntry ][ 2 ];

  //    Allocate the state table & the auxillary table in a single malloc
  itsStatePtr = new uInt[itsStateSize + itsAuxSize];
  AlwaysAssert(itsStatePtr != 0, AipsError);
  itsAuxStatePtr = &itsStatePtr[itsStateSize];
  
  reset();
}

void ACG::reset() {
  uInt u;
  
  if (itsInitSeed < SEED_TABLE_SIZE) {
    u = seedTable[ itsInitSeed ];
  } else {
    u = itsInitSeed ^ seedTable[ itsInitSeed & (SEED_TABLE_SIZE-1) ];
  }

  itsJ = randomStateTable[ itsInitTblEntry ][ 0 ] - 1;
  itsK = randomStateTable[ itsInitTblEntry ][ 1 ] - 1;

  for (Int i = 0; i < itsStateSize; i++) {
    itsStatePtr[i] = u = LCG(u);
  }
  for (Int i = 0; i < itsAuxSize; i++) {
    itsAuxStatePtr[i] = u = LCG(u);
  }
    
  // Get rid of compiler warning - hopefully the authors of this class knew
  // what they were doing
  itsK = static_cast<Short>(u % itsStateSize);
  Int tailBehind = (itsStateSize - randomStateTable[ itsInitTblEntry ][ 0 ]);
  itsJ = itsK - tailBehind;
  if (itsJ < 0) {
    itsJ += itsStateSize;
  }
  lcgRecurr = u;
}

ACG::~ACG() {
  delete[] itsStatePtr; 
  // don't delete itsAuxStatePtr, it's really an alias for itsStatePtr.
  itsAuxStatePtr = itsStatePtr = 0;
}

uInt ACG::asuInt()
{
  uInt result = itsStatePtr[itsK] + itsStatePtr[itsJ];
  itsStatePtr[itsK] = result;
  itsJ = (itsJ <= 0) ? (itsStateSize-1) : (itsJ-1);
  itsK = (itsK <= 0) ? (itsStateSize-1) : (itsK-1);
    
  // Get rid of compiler warning - hopefully the authors of this class knew
  // what they were doing
  Short auxIndex = static_cast<Short>((result >> 24) & (itsAuxSize - 1));
  uInt auxACG = itsAuxStatePtr[auxIndex];
  itsAuxStatePtr[auxIndex] = lcgRecurr = LCG(lcgRecurr);
    
  // 3c is a magic number. We are doing four masks here, so we
  // do not want to run off the end of the permutation table.
  // This insures that we have always got four entries left.
  uInt* perm = &randomPermutations[result & 0x3c];
  result =  *(perm++) & auxACG;
  result |= *(perm++) & ((auxACG << 24)
                         | ((auxACG >> 8)  & 0xffffff));
  result |= *(perm++) & ((auxACG << 16)
                         | ((auxACG >> 16) & 0xffff));
  result |= *(perm++) & ((auxACG <<  8)
                         | ((auxACG >> 24) &   0xff));
  return result;
}

MLCG::MLCG(Int seed1, Int seed2):
  itsInitSeedOne(seed1),
  itsInitSeedTwo(seed2)
{
  reset();
}

MLCG::~MLCG() {
}

void MLCG::reset() {
  Int seed1 = itsInitSeedOne;
  Int seed2 = itsInitSeedTwo;
  //  Most people pick stupid seed numbers that do not have enough
  //  bits. In this case, if they pick a small seed number, we
  //  map that to a specific seed.
  //
  if (seed1 < 0) {
    seed1 = (seed1 + 2147483561);
    seed1 = (seed1 < 0) ? -seed1 : seed1;
  }

  if (seed2 < 0) {
    seed2 = (seed2 + 2147483561);
    seed2 = (seed2 < 0) ? -seed2 : seed2;
  }

  if (seed1 > -1 && seed1 < SEED_TABLE_SIZE) {
    itsSeedOne = seedTable[seed1];
  } else {
    itsSeedOne = seed1 ^ seedTable[seed1 & (SEED_TABLE_SIZE-1)];
  }

  if (seed2 > -1 && seed2 < SEED_TABLE_SIZE) {
    itsSeedTwo = seedTable[seed2];
  } else {
    itsSeedTwo = seed2 ^ seedTable[ seed2 & (SEED_TABLE_SIZE-1) ];
  }
  itsSeedOne = (itsSeedOne % 2147483561) + 1;
  itsSeedTwo = (itsSeedTwo % 2147483397) + 1;
}

uInt MLCG::asuInt()
{
  Int k = itsSeedOne % 53668;
  
  itsSeedOne = 40014 * (itsSeedOne-k * 53668) - k * 12211;
  if (itsSeedOne < 0) {
    itsSeedOne += 2147483563;
  }

  k = itsSeedTwo % 52774;
  itsSeedTwo = 40692 * (itsSeedTwo - k * 52774) - k * 3791;
  if (itsSeedTwo < 0) {
    itsSeedTwo += 2147483399;
  }

  Int z = itsSeedOne - itsSeedTwo;
  if (z < 1) {
    z += 2147483562;
  }
  return static_cast<uInt>(z);
}

Random::~Random() {
}

String Random::asString(Random::Types type) {
  switch (type) {
  case BINOMIAL:
    return String("BINOMIAL");
  case DISCRETEUNIFORM:
    return String("DISCRETEUNIFORM");
  case ERLANG:
    return String("ERLANG");
  case GEOMETRIC:
    return String("GEOMETRIC");
  case HYPERGEOMETRIC:
    return String("HYPERGEOMETRIC");
  case NORMAL:
    return String("NORMAL");
  case LOGNORMAL:
    return String("LOGNORMAL");
  case NEGATIVEEXPONENTIAL:
    return String("NEGATIVEEXPONENTIAL");
  case POISSON:
    return String("POISSON");
  case UNIFORM:
    return String("UNIFORM");
  case WEIBULL:
    return String("WEIBULL");
  case UNKNOWN:
    return String("UNKNOWN");
  case NUMBER_TYPES: 
    throw(AipsError("NUMBER_TYPES has no string equivalent"));
  default:
    throw(AipsError("Unknown Random::Types enumerator"));
  }
}

Random::Types Random::asType(const String& str) {
  String canonicalCase(str);
  canonicalCase.upcase();
  Random::Types t;
  String s2;
  for (uInt i = 0; i < NUMBER_TYPES; i++) {
    t = static_cast<Random::Types>(i);
    s2 = Random::asString(t);
    if (s2.matches(canonicalCase)) {
      return t;
    }
  }
  return Random::UNKNOWN;
}

Random* Random::construct(Random::Types type, RNG* gen) {
  switch (type) {
  case BINOMIAL:
    return new Binomial(gen);
  case DISCRETEUNIFORM:
    return new DiscreteUniform(gen);
  case ERLANG:
    return new Erlang(gen);
  case GEOMETRIC:
    return new Geometric(gen);
  case HYPERGEOMETRIC:
    return new HyperGeometric(gen);
  case NORMAL:
    return new Normal(gen);
  case LOGNORMAL:
    return new LogNormal(gen);
  case NEGATIVEEXPONENTIAL:
    return new NegativeExpntl(gen);
  case POISSON:
    return new Poisson(gen);
  case UNIFORM:
    return new Uniform(gen);
  case WEIBULL:
    return new Weibull(gen);
  case UNKNOWN:
  case NUMBER_TYPES:
  default:
    return 0;
  }
}

Vector<Double> Random::defaultParameters (Random::Types type) {
  MLCG gen;
  const PtrHolder<Random> ranPtr(construct(type, &gen));
  if (ranPtr.ptr() == 0) {
    return Vector<Double>();
  } else {
    return ranPtr.ptr()->parameters();
  }
}

Binomial::~Binomial() {
}

Binomial::Binomial(RNG* gen, uInt n, Double p)
  :Random(gen),
   itsN(n),
   itsP(p)
{
  AlwaysAssert( p >= 0.0 && p <= 1.0 && n > 0, AipsError);
}

Double Binomial::operator()() {
  return static_cast<Double>(asInt());
}

uInt Binomial::asInt() {
  uInt result = 0;
  for (uInt i = 0; i < itsN; i++) {
    if (itsRNG->asDouble() < itsP) {
      result++;
    }
  }
  return result;
}

void Binomial::n(uInt newN) {
  AlwaysAssert(newN > 0, AipsError);
  itsN = newN;
}

void Binomial::n(Double newN) {
  AlwaysAssert(newN >= 0.5, AipsError);
  n(static_cast<uInt>(newN));
}

void Binomial::p(Double newP) {
  AlwaysAssert(newP >= 0.0 && newP <= 1.0, AipsError);
  itsP = newP;
}

void Binomial::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  n(pars(0));
  p(pars(1));
}

Vector<Double> Binomial::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = n();
  retVal(1) = p();
  return retVal;
}

Bool Binomial::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    pars(0) >= 0.5 && 
    pars(1) >= 0.0 && pars(1) <= 1.0;
}

DiscreteUniform::DiscreteUniform(RNG* gen, Int low, Int high)
  :Random(gen),
   itsLow(low),
   itsHigh(high),
   itsDelta(calcDelta(itsLow, itsHigh))
{
  AlwaysAssert(itsLow <= itsHigh, AipsError);
}

DiscreteUniform::~DiscreteUniform() {
}

Double DiscreteUniform::operator()() {
  return static_cast<Double>(asInt());
}

Int DiscreteUniform::asInt() {
  return itsLow + static_cast<Int>(floor(itsDelta * itsRNG->asDouble()));
}

void DiscreteUniform::low(Int x) {
  AlwaysAssert(x <= itsHigh, AipsError);
  itsLow = x;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void DiscreteUniform::high(Int x) {
  AlwaysAssert(itsLow <= x, AipsError);
  itsHigh = x;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void DiscreteUniform::range(Int low, Int high) {
  AlwaysAssert(low <= high, AipsError);
  itsLow = low;
  itsHigh = high;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void DiscreteUniform::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  range(static_cast<Int>(pars(0)), static_cast<Int>(pars(1)));
}

Vector<Double> DiscreteUniform::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = low();
  retVal(1) = high();
  return retVal;
}

Bool DiscreteUniform::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && pars(0) <= pars(1);
}

Double DiscreteUniform::calcDelta(Int low, Int high) {
  return static_cast<Double>((high - low) + 1);
}

Erlang::~Erlang() {
}

void Erlang::setState() {
  AlwaysAssert(!nearAbs(itsMean, 0.0), AipsError);
  AlwaysAssert(itsVariance > 0, AipsError);
  itsK = static_cast<Int>((itsMean * itsMean ) / itsVariance + 0.5 );
  itsK = (itsK > 0) ? itsK : 1;
  itsA = itsK / itsMean;
}

Double Erlang::operator()() {
  Double prod = 1.0;
  for (Int i = 0; i < itsK; i++) {
    prod *= itsRNG->asDouble();
  }
  return -log(prod)/itsA;
}

void Erlang::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
  variance(pars(1));
}

Vector<Double> Erlang::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = mean();
  retVal(1) = variance();
  return retVal;
}

Bool Erlang::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    !nearAbs(pars(0), 0.0) && 
    pars(1) > 0.0;
}

Geometric::Geometric(RNG* gen, Double mean) 
  :Random(gen),
   itsMean(mean)
{
  AlwaysAssert(itsMean >= 0.0 && itsMean < 1.0, AipsError);
}

Geometric::~Geometric() {
}

Double Geometric::operator()() {
  return static_cast<Double>(asInt());
}

uInt Geometric::asInt() {
  uInt samples;
  for (samples = 1; itsRNG->asDouble() < itsMean; samples++);
  return samples;
}

void Geometric::mean(Double x) {
  itsMean = x; 
  AlwaysAssert(itsMean >= 0.0 && itsMean < 1.0, AipsError);
}

void Geometric::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
}

Vector<Double> Geometric::parameters() const {
  return Vector<Double>(1, mean());
}

Bool Geometric::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 1 && 
    pars(0) >= 0.0 && pars(0) < 1.0;
}

HyperGeometric::~HyperGeometric() {
}

Double HyperGeometric::operator()() {
  const Double d = (itsRNG->asDouble() > itsP) ? (1.0 - itsP) :  itsP;
  return -itsMean * log(itsRNG->asDouble()) / (2.0 * d);
}

void HyperGeometric::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
  variance(pars(1));
}

Vector<Double> HyperGeometric::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = mean();
  retVal(1) = variance();
  return retVal;
}

Bool HyperGeometric::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    !nearAbs(pars(0), 0.0) && 
    pars(1) > 0.0 &&
    square(pars(0)) <=  pars(1);
}

void HyperGeometric::setState() {
  AlwaysAssert(itsVariance > 0.0, AipsError);
  AlwaysAssert(!near(itsMean, 0.0), AipsError);
  AlwaysAssert(itsMean*itsMean <= itsVariance, AipsError);
  const Double z = itsVariance / (itsMean * itsMean);
  itsP = 0.5 * (1.0 - sqrt((z - 1.0) / ( z + 1.0 )));
}

Normal::Normal(RNG* gen, Double mean, Double variance)
  :Random(gen),
   itsMean(mean),
   itsVariance(variance),
   itsCached(False),
   itsCachedValue(0)
{
  AlwaysAssert(itsVariance > 0.0, AipsError);
  itsStdDev = sqrt(itsVariance);
}

Normal::~Normal() {
}

//      See Simulation, Modelling & Analysis by Law & Kelton, pp259
//      This is the ``polar'' method.
//      We actually generate two IID normal distribution variables.
//      We cache the one & return the other.
Double Normal::operator()() {
  if (itsCached) {
    itsCached = False;
    return itsCachedValue * itsStdDev + itsMean;
  }

  for(;;) {
    const Double u1 = itsRNG->asDouble();
    const Double u2 = itsRNG->asDouble();
    const Double v1 = 2 * u1 - 1;
    const Double v2 = 2 * u2 - 1;
    const Double w = (v1 * v1) + (v2 * v2);
    if (w <= 1) {
      const Double y = sqrt( (-2 * log(w)) / w);
      const Double x1 = v1 * y;
      itsCachedValue = v2 * y;
      itsCached = True;
      return x1 * itsStdDev + itsMean;
    }
  }
}

void Normal::mean(Double x) {
  itsMean = x;
}

void Normal::variance(Double x) {
  itsVariance = x;
  AlwaysAssert(itsVariance > 0.0, AipsError);
  itsStdDev = sqrt(itsVariance);
}

void Normal::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
  variance(pars(1));
}

Vector<Double> Normal::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = mean();
  retVal(1) = variance();
  return retVal;
}

Bool Normal::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    pars(1) > 0.0;
}

LogNormal::LogNormal(RNG* gen, Double mean, Double variance)
  :Normal(gen),
   itsLogMean(mean),
   itsLogVar(variance)
{
  setState();
}

LogNormal::~LogNormal() {
}

//      See Simulation, Modelling & Analysis by Law & Kelton, pp260
Double LogNormal::operator()() {
  return pow(C::e, this->Normal::operator()() );
}

void LogNormal::mean(Double x) {
  itsLogMean = x;
  setState();
}

void LogNormal::variance(Double x) {
  itsLogVar = x;
  setState();
}

void LogNormal::setState() {
  const Double m2 = itsLogMean * itsLogMean;
  AlwaysAssert(!near(m2, 0.0), AipsError);
  this->Normal::mean(log(m2 / sqrt(itsLogVar + m2) ));
  AlwaysAssert(!near(m2+itsLogVar, 0.0), AipsError);
  this->Normal::variance(log((itsLogVar + m2)/m2 )); 
}

void LogNormal::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
  variance(pars(1));
}

Vector<Double> LogNormal::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = mean();
  retVal(1) = variance();
  return retVal;
}

Bool LogNormal::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    !nearAbs(pars(0), 0.0) && 
    pars(1) > 0.0;
}

NegativeExpntl::NegativeExpntl(RNG* gen, Double mean)
  :Random(gen)
{
  itsMean = mean;
}

NegativeExpntl::~NegativeExpntl() {
}

Double NegativeExpntl::operator()() {
  return -itsMean * log(itsRNG->asDouble());
}

void NegativeExpntl::mean(Double x) {
  itsMean = x;
}

void NegativeExpntl::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
}

Vector<Double> NegativeExpntl::parameters() const {
  return Vector<Double>(1, mean());
}

Bool NegativeExpntl::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 1; 
}

Poisson::Poisson(RNG* gen, Double mean)
  :Random(gen) 
{
  AlwaysAssert(mean >= 0.0, AipsError);
  itsMean = mean;
}

Poisson::~Poisson() {
}

Double Poisson::operator()() {
  return static_cast<Double>(asInt());
}

uInt Poisson::asInt() {
  const Double bound = exp(-1.0 * itsMean);
  uInt count = 0;
  
  for (Double product = 1.0; product >= bound; product *= itsRNG->asDouble()) {
    count++;
  }
  return count - 1;
}

void Poisson::mean(Double x) {
  AlwaysAssert(x >= 0.0, AipsError);
  itsMean = x;
}

void Poisson::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  mean(pars(0));
}

Vector<Double> Poisson::parameters() const {
  return Vector<Double>(1, mean());
}

Bool Poisson::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 1 && pars(0) > 0.0;
}

Uniform::Uniform(RNG* gen, Double low, Double high)
  :Random(gen),
   itsLow(low),
   itsHigh(high),
   itsDelta(calcDelta(itsLow, itsHigh))
{
  AlwaysAssert(itsLow < itsHigh, AipsError);
}

Uniform::~Uniform() {
}

Double Uniform::operator()() {
  return itsLow + itsDelta * itsRNG->asDouble();
}

void Uniform::low(Double x) {
  AlwaysAssert(x < itsHigh, AipsError);
  itsLow = x;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void Uniform::high(Double x) {
  AlwaysAssert(itsLow < x, AipsError);
  itsHigh = x;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void Uniform::range(Double low, Double high) {
  AlwaysAssert(low < high, AipsError);
  itsHigh = high;
  itsLow = low;
  itsDelta = calcDelta(itsLow, itsHigh);
}

void Uniform::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  range(pars(0), pars(1));
}

Vector<Double> Uniform::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = low();
  retVal(1) = high();
  return retVal;
}

Bool Uniform::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    pars(0) < pars(1);
}

Double Uniform::calcDelta(Double low, Double high) {
  return static_cast<Double>(high - low);
}

Weibull::~Weibull() {
}

//      See Simulation, Modelling & Analysis by Law & Kelton, pp259
//      This is the ``polar'' method.
Weibull::Weibull(RNG* gen, Double alpha, Double beta)
  :Random(gen),
   itsAlpha(alpha),
   itsBeta(beta),
   itsInvAlpha(0)
{
  setState();
}

Double Weibull::operator()() {
  return pow(itsBeta * ( - log(1.0 - itsRNG->asDouble()) ), itsInvAlpha);
}

void Weibull::alpha(Double x) {
  itsAlpha = x;
  setState();
}

void Weibull::beta(Double x) {
  itsBeta = x;
}

void Weibull::setParameters(const Vector<Double>& pars) {
  AlwaysAssert(checkParameters(pars), AipsError);
  alpha(pars(0));
  beta(pars(1));
}

Vector<Double> Weibull::parameters() const {
  Vector<Double> retVal(2);
  retVal(0) = alpha();
  retVal(1) = beta();
  return retVal;
}

Bool Weibull::checkParameters(const Vector<Double>& pars) const {
  return pars.nelements() == 2 && 
    !nearAbs(pars(0), 0.0) && 
    pars(1) > 0.0;
}

void Weibull::setState() {
  AlwaysAssert(!near(itsAlpha, 0.0), AipsError);
  itsInvAlpha = 1.0 / itsAlpha;
}
    
// Local Variables: 
// compile-command: "gmake XLIBLIST=0 Random"
// End: 
