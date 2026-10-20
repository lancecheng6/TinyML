#ifndef __RECORD_H__
#define __RECORD_H__

#include <inttypes.h>

/* RECBUFFER_SIZE determines the size of the two buffers; each buffer uses
 * RECBUFFER_SIZE*2 bytes of RAM. For a 16 kHz sample rate, RECBUFFER_SIZE of
 * 1024*4 is enough for smooth recording/playback. For higher sample rates,
 * increase it accordingly (e.g. 1024*8 for 44.1 kHz). The SD card read/write
 * speed also affects recording quality.
 */
#define RECBUFFER_SIZE  1024*8

/* Recorder states */
enum
{
	STA_IDLE = 0,	/* idle */
	STA_RECORDING,	/* recording */
	STA_PLAYING,	/* playing */
	STA_ERR,			/*  error  */
};

typedef struct
{
	uint8_t ucInput;			/* input source: 0 = MIC, 1 = line-in */
	uint8_t ucFmtIdx;			/* audio format: standard, word length, sample rate */
	uint8_t ucVolume;			/* current playback volume */
	uint8_t ucGain;			  /* current gain */
	uint8_t ucStatus;			/* recorder state: 0 = idle, 1 = recording, 2 = playing */
}REC_TYPE;

/* WAV file header */
typedef __packed struct
{
	uint32_t	riff;							/* = "RIFF"	0x46464952*/
	uint32_t	size_8;						/* total bytes from the next field to the end of file	*/
	uint32_t	wave;							/* = "WAVE" 0x45564157*/

	uint32_t	fmt;							/* = "fmt " 0x20746d66*/
	uint32_t	fmtSize;					/* size of the next struct (usually 16) */
	uint16_t	wFormatTag;				/* encoding format, usually 1	*/
	uint16_t	wChannels;				/* channels: 1 = mono, 2 = stereo */
	uint32_t	dwSamplesPerSec;	/* sample rate */
	uint32_t	dwAvgBytesPerSec;	/* bytes per second (= sample rate x bytes per sample) */
	uint16_t	wBlockAlign;			/* block align (= bits per sample / 8 * channels) */
	uint16_t	wBitsPerSample;		/* bits per sample */

	uint32_t	data;							/* = "data" 0x61746164*/
	uint32_t	datasize;					/* raw data length */
} WavHead;
void StartPlay(const char *filename);
void RecorderDemo(void);
void MusicPlayer_I2S_DMA_TX_Callback(void);
void Recorder_I2S_DMA_RX_Callback(void);
void MusicPlayer_SAI_DMA_TX_Callback(void);
void MusicPlayer_SAI_DMA_RX_Callback(void);
#endif  /* __RECORD_H__   */
