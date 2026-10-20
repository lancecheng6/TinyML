#ifndef KWS_DSP_H
#define KWS_DSP_H

#include "kws_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compute log-mel features: out[t*KWS_NMEL + m], shape (KWS_TFRAMES, KWS_NMEL).
 * pcm: 16-bit mono samples at KWS_SR. Matches kws_features.py (tf.signal.stft pipeline). */
void kws_logmel(const short* pcm, int n, float* out);

#ifdef __cplusplus
}
#endif

#endif
