#include "fft.h"

#include <math.h>

/* Iterative radix-2 complex FFT, in-place. n must be a power of two. */
void fft(float* re, float* im, int n) {
  int i, j, len, k;
  const float PI = 3.14159265358979323846f;

  j = 0;
  for (i = 1; i < n; ++i) {
    int bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      float tr = re[i]; re[i] = re[j]; re[j] = tr;
      float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }

  for (len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * PI / (float)len;
    float wr = cosf(ang), wi = sinf(ang);
    for (i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (k = 0; k < len / 2; ++k) {
        int a = i + k;
        int b = i + k + len / 2;
        float xr = re[b] * cr - im[b] * ci;
        float xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr;
        im[b] = im[a] - xi;
        re[a] += xr;
        im[a] += xi;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}
