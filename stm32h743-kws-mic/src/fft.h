#ifndef KWS_FFT_H
#define KWS_FFT_H

#ifdef __cplusplus
extern "C" {
#endif

/* In-place radix-2 complex FFT for power-of-two n. */
void fft(float* re, float* im, int n);

#ifdef __cplusplus
}
#endif

#endif
