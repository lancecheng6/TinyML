#include "kws_dsp.h"

#include <math.h>

#include "fft.h"

void kws_logmel(const short* pcm, int n, float* out) {
  static float re[KWS_FFT];
  static float im[KWS_FFT];
  static float mag[KWS_NFFTBINS];
  int t, i, k, m;

  for (t = 0; t < KWS_TFRAMES; ++t) {
    int base = t * KWS_FRAME_STEP;
    for (i = 0; i < KWS_FFT; ++i) {
      re[i] = 0.0f;
      im[i] = 0.0f;
    }
    /* window + zero-pad to FFT length */
    for (i = 0; i < KWS_FRAME_LEN; ++i) {
      int idx = base + i;
      float s = (idx < n) ? ((float)pcm[idx] / 32768.0f) : 0.0f;
      re[i] = s * KWS_HANN[i];
    }

    fft(re, im, KWS_FFT);

    for (k = 0; k < KWS_NFFTBINS; ++k) {
      mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }

    for (m = 0; m < KWS_NMEL; ++m) {
      float acc = 0.0f;
      for (k = 0; k < KWS_NFFTBINS; ++k) {
        acc += mag[k] * KWS_MELMAT[k * KWS_NMEL + m];
      }
      out[t * KWS_NMEL + m] = logf(acc + 1e-6f);
    }
  }
}
