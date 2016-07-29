/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "./vp10_rtcd.h"
#include "./vpx_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/transform_test_base.h"
#include "test/util.h"
#include "vpx_ports/mem.h"

using libvpx_test::ACMRandom;

namespace {
typedef void (*IhtFunc)(const tran_low_t *in, uint8_t *out, int stride,
                        int tx_type);
using std::tr1::tuple;
using libvpx_test::FhtFunc;
typedef tuple<FhtFunc, IhtFunc, int, vpx_bit_depth_t, int> Ht16x16Param;

void fht16x16_ref(const int16_t *in, tran_low_t *out, int stride,
                int tx_type) {
  vp10_fht16x16_c(in, out, stride, tx_type);
}

#if CONFIG_VPX_HIGHBITDEPTH
typedef void (*IHbdHtFunc)(const tran_low_t *in, uint8_t *out, int stride,
                           int tx_type, int bd);
typedef void (*HbdHtFunc)(const int16_t *input, int32_t *output, int stride,
                          int tx_type, int bd);

// Target optimized function, tx_type, bit depth
typedef tuple<HbdHtFunc, int, int> HighbdHt16x16Param;

void highbd_fht16x16_ref(const int16_t *in, int32_t *out, int stride,
                         int tx_type, int bd) {
  vp10_fwd_txfm2d_16x16_c(in, out, stride, tx_type, bd);
}
#endif  // CONFIG_VPX_HIGHBITDEPTH

class VP10Trans16x16HT
    : public libvpx_test::TransformTestBase,
      public ::testing::TestWithParam<Ht16x16Param> {
 public:
  virtual ~VP10Trans16x16HT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    inv_txfm_ = GET_PARAM(1);
    tx_type_  = GET_PARAM(2);
    pitch_    = 16;
    fwd_txfm_ref = fht16x16_ref;
    bit_depth_ = GET_PARAM(3);
    mask_ = (1 << bit_depth_) - 1;
    num_coeffs_ = GET_PARAM(4);
  }
  virtual void TearDown() { libvpx_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride, tx_type_);
  }

  void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) {
    inv_txfm_(out, dst, stride, tx_type_);
  }

  FhtFunc fwd_txfm_;
  IhtFunc inv_txfm_;
};

TEST_P(VP10Trans16x16HT, CoeffCheck) {
  RunCoeffCheck();
}

#if CONFIG_VPX_HIGHBITDEPTH
class VP10HighbdTrans16x16HT
    : public ::testing::TestWithParam<HighbdHt16x16Param> {
 public:
  virtual ~VP10HighbdTrans16x16HT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    fwd_txfm_ref_ = highbd_fht16x16_ref;
    tx_type_  = GET_PARAM(1);
    bit_depth_ = GET_PARAM(2);
    mask_ = (1 << bit_depth_) - 1;
    num_coeffs_ = 256;

    input_ = reinterpret_cast<int16_t *>(
        vpx_memalign(16, sizeof(int16_t) * num_coeffs_));
    output_ = reinterpret_cast<int32_t *>(
        vpx_memalign(16, sizeof(int32_t) * num_coeffs_));
    output_ref_ = reinterpret_cast<int32_t *>(
        vpx_memalign(16, sizeof(int32_t) * num_coeffs_));
  }

  virtual void TearDown() {
    vpx_free(input_);
    vpx_free(output_);
    vpx_free(output_ref_);
    libvpx_test::ClearSystemState();
  }

 protected:
  void RunBitexactCheck();

 private:
  HbdHtFunc fwd_txfm_;
  HbdHtFunc fwd_txfm_ref_;
  int tx_type_;
  int bit_depth_;
  int mask_;
  int num_coeffs_;
  int16_t *input_;
  int32_t *output_;
  int32_t *output_ref_;
};

void VP10HighbdTrans16x16HT::RunBitexactCheck() {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  int i, j;
  const int stride = 16;
  const int num_tests = 1000;

  for (i = 0; i < num_tests; ++i) {
    for (j = 0; j < num_coeffs_; ++j) {
      input_[j] = (rnd.Rand16() & mask_) - (rnd.Rand16() & mask_);
    }

    fwd_txfm_ref_(input_, output_ref_, stride, tx_type_, bit_depth_);
    ASM_REGISTER_STATE_CHECK(fwd_txfm_(input_, output_, stride, tx_type_,
                                       bit_depth_));

    for (j = 0; j < num_coeffs_; ++j) {
      EXPECT_EQ(output_ref_[j], output_[j])
          << "Not bit-exact result at index: " << j
          << " at test block: " << i;
    }
  }
}

TEST_P(VP10HighbdTrans16x16HT, HighbdCoeffCheck) {
  RunBitexactCheck();
}
#endif  // CONFIG_VPX_HIGHBITDEPTH

using std::tr1::make_tuple;

#if HAVE_SSE2
const Ht16x16Param kArrayHt16x16Param_sse2[] = {
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 0,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 1,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 2,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 3,
                 VPX_BITS_8, 256),
#if CONFIG_EXT_TX
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 4,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 5,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 6,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 7,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 8,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 10,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 11,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 12,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 13,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 14,
                 VPX_BITS_8, 256),
      make_tuple(&vp10_fht16x16_sse2, &vp10_iht16x16_256_add_sse2, 15,
                 VPX_BITS_8, 256)
#endif  // CONFIG_EXT_TX
};
INSTANTIATE_TEST_CASE_P(
    SSE2, VP10Trans16x16HT,
    ::testing::ValuesIn(kArrayHt16x16Param_sse2));
#endif  // HAVE_SSE2

#if HAVE_SSE4_1 && CONFIG_VPX_HIGHBITDEPTH
const HighbdHt16x16Param kArrayHBDHt16x16Param_sse4_1[] = {
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 0, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 0, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 1, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 1, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 2, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 2, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 3, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 3, 12),
#if CONFIG_EXT_TX
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 4, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 4, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 5, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 5, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 6, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 6, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 7, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 7, 12),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 8, 10),
    make_tuple(&vp10_fwd_txfm2d_16x16_sse4_1, 8, 12),
#endif  // CONFIG_EXT_TX
};
INSTANTIATE_TEST_CASE_P(
    SSE4_1, VP10HighbdTrans16x16HT,
    ::testing::ValuesIn(kArrayHBDHt16x16Param_sse4_1));
#endif  // HAVE_SSE4_1 && CONFIG_VPX_HIGHBITDEPTH

}  // namespace
