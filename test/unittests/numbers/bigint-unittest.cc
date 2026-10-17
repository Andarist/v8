// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/bigint.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using BigIntWithIsolate = TestWithIsolate;

void Compare(DirectHandle<BigInt> x, double value, ComparisonResult expected) {
  CHECK_EQ(expected, BigInt::CompareToDouble(x, value));
}

DirectHandle<BigInt> NewFromInt(Isolate* isolate, int value) {
  DirectHandle<Smi> smi_value(Smi::FromInt(value), isolate);
  return BigInt::FromNumber(isolate, smi_value).ToHandleChecked();
}

TEST_F(BigIntWithIsolate, CompareToDouble) {
  DirectHandle<BigInt> zero = NewFromInt(isolate(), 0);
  DirectHandle<BigInt> one = NewFromInt(isolate(), 1);
  DirectHandle<BigInt> minus_one = NewFromInt(isolate(), -1);

  // Non-finite doubles.
  Compare(zero, std::nan(""), ComparisonResult::kUndefined);
  Compare(one, INFINITY, ComparisonResult::kLessThan);
  Compare(one, -INFINITY, ComparisonResult::kGreaterThan);

  // Unequal sign.
  Compare(one, -1, ComparisonResult::kGreaterThan);
  Compare(minus_one, 1, ComparisonResult::kLessThan);

  // Cases involving zero.
  Compare(zero, 0, ComparisonResult::kEqual);
  Compare(zero, -0, ComparisonResult::kEqual);
  Compare(one, 0, ComparisonResult::kGreaterThan);
  Compare(minus_one, 0, ComparisonResult::kLessThan);
  Compare(zero, 1, ComparisonResult::kLessThan);
  Compare(zero, -1, ComparisonResult::kGreaterThan);

  // Small doubles.
  Compare(zero, 0.25, ComparisonResult::kLessThan);
  Compare(one, 0.5, ComparisonResult::kGreaterThan);
  Compare(one, -0.5, ComparisonResult::kGreaterThan);
  Compare(zero, -0.25, ComparisonResult::kGreaterThan);
  Compare(minus_one, -0.5, ComparisonResult::kLessThan);

  // Different bit lengths.
  DirectHandle<BigInt> four = NewFromInt(isolate(), 4);
  DirectHandle<BigInt> minus_five = NewFromInt(isolate(), -5);
  Compare(four, 3.9, ComparisonResult::kGreaterThan);
  Compare(four, 1.5, ComparisonResult::kGreaterThan);
  Compare(four, 8, ComparisonResult::kLessThan);
  Compare(four, 16, ComparisonResult::kLessThan);
  Compare(minus_five, -4.9, ComparisonResult::kLessThan);
  Compare(minus_five, -4, ComparisonResult::kLessThan);
  Compare(minus_five, -25, ComparisonResult::kGreaterThan);

  // Same bit length, difference in first digit.
  double big_double = 4428155326412785451008.0;
  DirectHandle<BigInt> big =
      BigIntLiteral(isolate(), "0xF10D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0xE00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  double other_double = -13758438578910658560.0;
  DirectHandle<BigInt> other =
      BigIntLiteral(isolate(), "-0xBEEFC1FE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kGreaterThan);
  other = BigIntLiteral(isolate(), "-0xBEEFCBFE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in non-first digit.
  big = BigIntLiteral(isolate(), "0xF00D00000000000001").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0xF00A00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  other = BigIntLiteral(isolate(), "-0xBEEFCAFE00000001").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in fractional part.
  Compare(one, 1.5, ComparisonResult::kLessThan);
  Compare(minus_one, -1.25, ComparisonResult::kGreaterThan);
  big = NewFromInt(isolate(), 0xF00D00);
  Compare(big, 15731968.125, ComparisonResult::kLessThan);
  Compare(big, 15731967.875, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0x123456789AB").ToHandleChecked();
  Compare(big, 1250999896491.125, ComparisonResult::kLessThan);

  // Equality!
  Compare(one, 1, ComparisonResult::kEqual);
  Compare(minus_one, -1, ComparisonResult::kEqual);
  big = BigIntLiteral(isolate(), "0xF00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kEqual);

  DirectHandle<BigInt> two_52 =
      BigIntLiteral(isolate(), "0x10000000000000").ToHandleChecked();
  Compare(two_52, 4503599627370496.0, ComparisonResult::kEqual);
}

namespace {

DirectHandle<BigInt> HexBigInt(Isolate* isolate, const std::string& hex) {
  return BigIntLiteral(isolate, hex.c_str()).ToHandleChecked();
}

// Builds {words} 64-bit words, with {top} in the most significant one,
// {varying} in the one at index {varying_word} (counting from the least
// significant word) and {low} in the least significant one.
std::string HexWords(int words, uint64_t top, int varying_word,
                     uint64_t varying, uint64_t low) {
  std::string hex = "0x";
  for (int word = words - 1; word >= 0; word--) {
    uint64_t value = word == words - 1 ? top : 0;
    if (word == varying_word) value |= varying;
    if (word == 0) value |= low;
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "%016" PRIx64, value);
    hex += buffer;
  }
  return hex;
}

// Keys whose lowest 64 bits take only 16 different values, like fields packed
// into a wide integer, must still spread over the buckets.
void CheckHashSpreadsKeys(Isolate* isolate, int words, uint64_t top,
                          int varying_word, int varying_shift = 0) {
  constexpr int kSamples = 1 << 12;
  constexpr int kBuckets = 1 << 8;
  std::vector<int> buckets(kBuckets, 0);
  for (int i = 0; i < kSamples; i++) {
    HandleScope scope(isolate);
    std::string hex =
        HexWords(words, top, varying_word,
                 static_cast<uint64_t>(i + 1) << varying_shift, i & 15);
    buckets[HexBigInt(isolate, hex)->Hash() % kBuckets]++;
  }
  double mean = static_cast<double>(kSamples) / kBuckets;
  double sum_of_squares = 0;
  for (int count : buckets) {
    sum_of_squares += (count - mean) * (count - mean);
  }
  double variation_coefficient = std::sqrt(sum_of_squares / kBuckets) / mean;
  CHECK_LT(variation_coefficient, 0.4);
}

}  // namespace

TEST_F(BigIntWithIsolate, HashUsesDigitsAboveTheLowest) {
  constexpr uint64_t kTopBit = uint64_t{1} << 63;
  // Two-word keys varying in the upper word: two or four digits.
  CheckHashSpreadsKeys(isolate(), 2, kTopBit, 1);
  // Like above, but only three digits on 32-bit targets.
  CheckHashSpreadsKeys(isolate(), 2, 0, 1);
  // Varying only in the upper half of a word: the fourth digit on 32-bit
  // targets.
  CheckHashSpreadsKeys(isolate(), 2, 0, 1, 32);
  // 64-word keys (hashed with rapidhash) varying in a middle word.
  CheckHashSpreadsKeys(isolate(), 64, kTopBit, 40);
}

TEST_F(BigIntWithIsolate, HashOfLongBigIntsUsesTheMostSignificantDigit) {
  // Values twice as long as the bytes that are hashed in full, which differ
  // only in their most significant digit.
  constexpr size_t kHexCharsPerByte = 2;
  std::string middle(2 * BigInt::kMaxHashedBytes * kHexCharsPerByte, '0');
  DirectHandle<BigInt> one = HexBigInt(isolate(), "0x1" + middle + "1");
  DirectHandle<BigInt> two = HexBigInt(isolate(), "0x2" + middle + "1");
  CHECK_NE(one->Hash(), two->Hash());
}

}  // namespace internal
}  // namespace v8
