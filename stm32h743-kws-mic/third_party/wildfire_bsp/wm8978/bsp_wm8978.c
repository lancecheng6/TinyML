/*
******************************************************************************
* @file    bsp_led.c
* @author  fire
* @version V1.0
* @date    2015-xx-xx
* @brief   WM8978 driver
******************************************************************************
* @attention
*
* Platform : Wildfire STM32 H743 development board
* Forum    : http://www.chuxue123.com
* Shop     : http://firestm32.taobao.com
*
******************************************************************************
*/
#include "./wm8978/bsp_wm8978.h"  
#include "./usart/bsp_debug_usart.h"
#include "main.h"
#include "./i2c/i2c.h"
//#include "./Recorder/Recorder.h"
#include "./delay/core_delay.h" 
extern I2C_HandleTypeDef I2C_Handle;
uint32_t AudioTotalSize ;         /* total size of the audio file */
uint32_t AudioRemSize;            /* remaining data to be written to the file */
uint16_t *CurrentPos;             /* current position of the audio data pointer */


DMA_HandleTypeDef hdma_spi2_tx;
DMA_HandleTypeDef hdma_spi2_rx;
/**
	*******************************************************************************************************
	*	                     WM8978 I2C control / configuration
	*******************************************************************************************************
	*/
static uint8_t WM8978_I2C_WriteRegister(uint8_t RegisterAddr, uint16_t RegisterValue);
static uint16_t wm8978_ReadReg(uint8_t _ucRegAddr);
static uint8_t wm8978_WriteReg(uint8_t _ucRegAddr, uint16_t _usValue);
/*
	WM8978 register cache
	The WM8978 2-wire (I2C) interface does not support read operations, so the
	register values are cached in memory: writes update both the device and the
	cache, and reads return the cached value.
	Register map: WM8978(V4.5_2011).pdf page 89; register address is 7 bits and
	register data is 9 bits.
*/
static uint16_t wm8978_RegCash[] = {
	0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
	0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
	0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
	0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
	0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
	0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
	0x001, 0x001
};
/**
  * @brief  Write a byte to the given codec register over I2C
  * @param  RegisterAddr: register address
  * @param  RegisterValue: value to write
  * @retval 1 on success, 0 on failure
  */
static uint8_t WM8978_I2C_WriteRegister(uint8_t RegisterAddr, uint16_t RegisterValue)
{	
	uint16_t tmp;

	tmp  = (RegisterValue&0xff) << 8;
	tmp |= ((RegisterAddr << 1) & 0xFE) | ((RegisterValue >> 8) & 0x1);
	if(HAL_I2C_Master_Transmit(&I2C_Handle,WM8978_SLAVE_ADDRESS,(uint8_t *)&tmp,2,WM8978_I2C_FLAG_TIMEOUT)==HAL_OK)
	{
		return 1; 
	}
	else 		
	return 0;  
}

/**
	* @brief  Read back a WM8978 register from the cache
	* @param  _ucRegAddr : register address
	* @retval register value
	*/
static uint16_t wm8978_ReadReg(uint8_t _ucRegAddr)
{
	return wm8978_RegCash[_ucRegAddr];
}

/**
	* @brief  Write a WM8978 register
	* @param  _ucRegAddr : register address
	* @param  _usValue : register value
	* @retval 0: write failed
    *         1: write succeeded
	*/
static uint8_t wm8978_WriteReg(uint8_t _ucRegAddr, uint16_t _usValue)
{
	uint8_t res;
	res=WM8978_I2C_WriteRegister(_ucRegAddr,_usValue);
	wm8978_RegCash[_ucRegAddr] = _usValue;
	return res;
}

/**
	* @brief  Configure the I2C GPIOs and check that the WM8978 responds
	* @param  None
	* @retval 1: init OK
	*         0: init failed
	*/
uint8_t wm8978_Init(void)
{
	uint8_t res;
	
	I2cMaster_Init();		/* initialize the I2C interface */
	res=wm8978_Reset();		/* hardware-reset all WM8978 registers to defaults */
	wm8978_CtrlGPIO1(1);	/* set WM8978 GPIO1 to playback mode */
	return res;
}

/**
	* @brief  Set the output channel 1 volume
	* @param  _ucVolume : volume, 0-63
	* @retval None
	*/
void wm8978_SetOUT1Volume(uint8_t _ucVolume)
{
	uint16_t regL;
	uint16_t regR;

	if (_ucVolume > 0x3F)
	{
		_ucVolume = 0x3F;
	}

	regL = _ucVolume;
	regR = _ucVolume;

	/*
		R52	LOUT1 Volume control
		R53	ROUT1 Volume control
	*/
	/* Update the left channel cache first */
	wm8978_WriteReg(52, regL | 0x00);

	/* Then update both channels together */
	wm8978_WriteReg(53, regR | 0x100);	/* 0x180 updates at zero-crossing to avoid "pop" noise */
}

/**
	* @brief  Set the output channel 2 volume
	* @param  _ucVolume : volume, 0-63
	* @retval None
	*/
void wm8978_SetOUT2Volume(uint8_t _ucVolume)
{
	uint16_t regL;
	uint16_t regR;

	if (_ucVolume > 0x3F)
	{
		_ucVolume = 0x3F;
	}

	regL = _ucVolume;
	regR = _ucVolume;

	/*
		R54	LOUT2 (SPK) Volume control
		R55	ROUT2 (SPK) Volume control
	*/
	/* Update the left channel cache first */
	wm8978_WriteReg(54, regL | 0x00);

	/* Then update both channels together */
	wm8978_WriteReg(55, regR | 0x100);	/* update at zero-crossing to avoid "pop" noise */
}

/**
	* @brief  Read the output channel 1 volume
	* @param  None
	* @retval current volume
	*/
uint8_t wm8978_ReadOUT1Volume(void)
{
	return (uint8_t)(wm8978_ReadReg(52) & 0x3F );
}

/**
	* @brief  Read the output channel 2 volume
	* @param  None
	* @retval current volume
	*/
uint8_t wm8978_ReadOUT2Volume(void)
{
	return (uint8_t)(wm8978_ReadReg(54) & 0x3F );
}

/**
	* @brief  Mute the output
	* @param  _ucMute: mode
	*         @arg 1: mute
	*         @arg 0: unmute
	* @retval None
	*/
void wm8978_OutMute(uint8_t _ucMute)
{
	uint16_t usRegValue;

	if (_ucMute == 1) /* mute */
	{
		usRegValue = wm8978_ReadReg(52); /* Left Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(52, usRegValue);

		usRegValue = wm8978_ReadReg(53); /* Left Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(53, usRegValue);

		usRegValue = wm8978_ReadReg(54); /* Right Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(54, usRegValue);

		usRegValue = wm8978_ReadReg(55); /* Right Mixer Control */
		usRegValue |= (1u << 6);
		wm8978_WriteReg(55, usRegValue);
	}
	else	/* unmute */
	{
		usRegValue = wm8978_ReadReg(52);
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(52, usRegValue);

		usRegValue = wm8978_ReadReg(53); /* Left Mixer Control */
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(53, usRegValue);

		usRegValue = wm8978_ReadReg(54);
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(54, usRegValue);

		usRegValue = wm8978_ReadReg(55); /* Left Mixer Control */
		usRegValue &= ~(1u << 6);
		wm8978_WriteReg(55, usRegValue);
	}
}

/**
	* @brief  Set the microphone PGA gain
	* @param  _ucGain : gain, 0-63
	* @retval None
	*/
void wm8978_SetMicGain(uint8_t _ucGain)
{
	if (_ucGain > GAIN_MAX)
	{
		_ucGain = GAIN_MAX;
	}

	/* PGA volume control R45/R46, WM8978(V4.5_2011).pdf page 25
		Bit8	INPPGAUPDATE
		Bit7	INPPGAZCL		update at zero crossing
		Bit6	INPPGAMUTEL		PGA mute
		Bit5:0	gain value; 010000 = 0 dB
	*/
	wm8978_WriteReg(45, _ucGain);
	wm8978_WriteReg(46, _ucGain | (1 << 8));
}

/**
	* @brief  Set the line-input channel gain
	* @param  _ucGain : gain 0-7 (7 = max, 0 = min; can attenuate or amplify)
	* @retval None
	*/
void wm8978_SetLineGain(uint8_t _ucGain)
{
	uint16_t usRegValue;

	if (_ucGain > 7)
	{
		_ucGain = 7;
	}

	/*
		Mic input channel gain is controlled by PGABOOSTL / PGABOOSTR.
		Aux input gain is controlled by AUXL2BOOSTVO[2:0] / AUXR2BOOSTVO[2:0].
		Line input gain is controlled by LIP2BOOSTVOL[2:0] / RIP2BOOSTVOL[2:0].
	*/
	/*	WM8978(V4.5_2011).pdf page 29, R47 (left) / R48 (right), MIC gain control
		R47 (R48 is identical)
		B8		PGABOOSTL	= 1: 0 = MIC signal bypassed (no gain), 1 = +20 dB (bootstrap)
		B7		= 0, reserved
		B6:4	L2_2BOOSTVOL = x: 0 = disabled, 1-7 = -12 dB .. +6 dB (attenuate or amplify)
		B3		= 0, reserved
		B2:0`	AUXL2BOOSTVOL = x: 0 = disabled, 1-7 = -12 dB .. +6 dB (attenuate or amplify)
	*/

	usRegValue = wm8978_ReadReg(47);
	usRegValue &= 0x8F;/* clear bits 6:4   1000 1111*/
	usRegValue |= (_ucGain << 4);
	wm8978_WriteReg(47, usRegValue);	/* write the left input gain register */

	usRegValue = wm8978_ReadReg(48);
	usRegValue &= 0x8F;/* clear bits 6:4   1000 1111*/
	usRegValue |= (_ucGain << 4);
	wm8978_WriteReg(48, usRegValue);	/* write the right input gain register */
}

/**
	* @brief  Power down the WM8978 (low-power mode)
	* @param  None
	* @retval None
	*/
void wm8978_PowerDown(void)
{
	wm8978_Reset();			/* hardware-reset all WM8978 registers to defaults */
}

/**
	* @brief  Configure the WM8978 audio interface (I2S)
	* @param  _usStandard : interface standard (Philips, MSB or LSB justified)
	* @param  _ucWordLen : word length, 16/24/32 (20-bit is not supported here)
	* @retval None
	*/
void wm8978_CfgAudioIF(uint16_t _usStandard, uint8_t _ucWordLen)
{
	uint16_t usReg;

	/* WM8978(V4.5_2011).pdf page 73, register list */

	/*	REG R4, audio interface control
		B8		BCP	 = X, BCLK polarity: 0 = normal, 1 = inverted
		B7		LRCP = x, LRC polarity: 0 = normal, 1 = inverted
		B6:5	WL = x, word length: 00=16, 01=20, 10=24, 11=32 (right-justified max 24)
		B4:3	FMT = x, data format: 00=right-justified, 01=left-justified, 10=I2S, 11=PCM
		B2		DACLRSWAP = x, place DAC data on the left or right of LRC
		B1 		ADCLRSWAP = x, place ADC data on the left or right of LRC
		B0		MONO	= 0, 0 = stereo, 1 = mono (left channel only)
	*/
	usReg = 0;
	if (_usStandard == SAI_I2S_STANDARD)	/* Philips I2S standard */
	{
		usReg |= (2 << 3);
	}
	else if (_usStandard == SAI_I2S_MSBJUSTIFIED)	/* MSB justified (left) */
	{
		usReg |= (1 << 3);
	}
	else if (_usStandard == SAI_I2S_LSBJUSTIFIED)	/* LSB justified (right) */
	{
		usReg |= (0 << 3);
	}
	else	/* PCM standard (16-bit channel frame with long/short sync, or 16-bit data in a 32-bit frame) */
	{
		usReg |= (3 << 3);;
	}

	if (_ucWordLen == 24)
	{
		usReg |= (2 << 5);
	}
	else if (_ucWordLen == 32)
	{
		usReg |= (3 << 5);
	}
	else
	{
		usReg |= (0 << 5);		/* 16-bit */
	}
	wm8978_WriteReg(4, usReg);

	/*
		R6, clock generation control
		MS = 0: WM8978 is clocked externally (MCLK provided by the MCU)
	*/
	wm8978_WriteReg(6, 0x000);
}


/**
	* @brief  Configure the WM8978 audio path
	* @param  _InPath : audio input path configuration
	* @param  _OutPath : audio output path configuration
	* @retval None
	*/
void wm8978_CfgAudioPath(uint16_t _InPath, uint16_t _OutPath)
{
	uint16_t usReg;

	/* See the REGISTER MAP section of the WM8978 datasheet, page 89 */

	if ((_InPath == IN_PATH_OFF) && (_OutPath == OUT_PATH_OFF))
	{
		wm8978_PowerDown();
		return;
	}

	/* --------------------------- Step 1: configure registers from the input path -----------------------*/
	/*
		R1 Power manage 1
		Bit8    BUFDCOPEN,  Output stage 1.5xAVDD/2 driver enable
 		Bit7    OUT4MIXEN, OUT4 mixer enable
		Bit6    OUT3MIXEN, OUT3 mixer enable
		Bit5    PLLEN	(not used)
		Bit4    MICBEN	, Microphone bias enable
		Bit3    BIASEN	, Analogue amplifier bias control (must be 1)
		Bit2    BUFIOEN , Unused input/output tie-off buffer enable
		Bit1:0  VMIDSEL, must be non-zero for the analog amplifier to work
	*/
	usReg = (1 << 3) | (3 << 0);
	if (_OutPath & OUT3_4_ON) 	/* enable OUT3/OUT4 (e.g. to a GSM module) */
	{
		usReg |= ((1 << 7) | (1 << 6));
	}
	if ((_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= (1 << 4);
	}
	wm8978_WriteReg(1, usReg);	/* write register */

	/*
		R2 Power manage 2
		Bit8	ROUT1EN,	ROUT1 output enable (headphone right)
		Bit7	LOUT1EN,	LOUT1 output enable (headphone left)
		Bit6	SLEEP, 		0 = normal operation, 1 = reduced current in standby
		Bit5	BOOSTENR,	Right channel input BOOST enable (required for PGA gain)
		Bit4	BOOSTENL,	Left channel input BOOST enable
		Bit3	INPGAENR,	Right channel input PGA enable
		Bit2	INPGAENL,	Left channel input PGA enable
		Bit1	ADCENR,		Enable ADC right channel
		Bit0	ADCENL,		Enable ADC left channel
	*/
	usReg = 0;
	if (_OutPath & EAR_LEFT_ON)
	{
		usReg |= (1 << 7);
	}
	if (_OutPath & EAR_RIGHT_ON)
	{
		usReg |= (1 << 8);
	}
	if (_InPath & MIC_LEFT_ON)
	{
		usReg |= ((1 << 4) | (1 << 2));
	}
	if (_InPath & MIC_RIGHT_ON)
	{
		usReg |= ((1 << 5) | (1 << 3));
	}
	if (_InPath & LINE_ON)
	{
		usReg |= ((1 << 4) | (1 << 5));
	}
	if (_InPath & MIC_RIGHT_ON)
	{
		usReg |= ((1 << 5) | (1 << 3));
	}
	if (_InPath & ADC_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(2, usReg);	/* write register */

	/*
		R3 Power manage 3
		Bit8	OUT4EN,		OUT4 enable
		Bit7	OUT3EN,		OUT3 enable
		Bit6	LOUT2EN,	LOUT2 output enable
		Bit5	ROUT2EN,	ROUT2 output enable
		Bit4	0,
		Bit3	RMIXEN,		Right mixer enable
		Bit2	LMIXEN,		Left mixer enable
		Bit1	DACENR,		Right channel DAC enable
		Bit0	DACENL,		Left channel DAC enable
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= ((1 << 8) | (1 << 7));
	}
	if (_OutPath & SPK_ON)
	{
		usReg |= ((1 << 6) | (1 << 5));
	}
	if (_OutPath != OUT_PATH_OFF)
	{
		usReg |= ((1 << 3) | (1 << 2));
	}
	if (_InPath & DAC_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(3, usReg);	/* write register */

	/*
		R44 Input ctrl

		Bit8	MBVSEL, 		Microphone bias voltage: 0 = 0.9*AVDD, 1 = 0.6*AVDD
		Bit7	0
		Bit6	R2_2INPPGA,		Connect R2 to right channel input PGA positive terminal
		Bit5	RIN2INPPGA,		Connect RIN to right channel input PGA negative terminal
		Bit4	RIP2INPPGA,		Connect RIP to right channel input PGA amplifier positive terminal
		Bit3	0
		Bit2	L2_2INPPGA,		Connect L2 to left channel input PGA positive terminal
		Bit1	LIN2INPPGA,		Connect LIN to left channel input PGA negative terminal
		Bit0	LIP2INPPGA,		Connect LIP to left channel input PGA amplifier positive terminal
	*/
	usReg = 0 << 8;
	if (_InPath & LINE_ON)
	{
		usReg |= ((1 << 6) | (1 << 2));
	}
	if (_InPath & MIC_RIGHT_ON)
	{
		usReg |= ((1 << 5) | (1 << 4));
	}
	if (_InPath & MIC_LEFT_ON)
	{
		usReg |= ((1 << 1) | (1 << 0));
	}
	wm8978_WriteReg(44, usReg);	/* write register */


	/*
		R14 ADC Control
		Optional high-pass filter, WM8978(V4.5_2011).pdf pages 31-32
		Bit8 	HPFEN,	High-pass filter enable (0 = disabled, 1 = enabled)
		BIt7 	HPFAPP,	Audio mode or application mode (0 = audio mode)
		Bit6:4	HPFCUT,	Application-mode cutoff frequency (000-111)
		Bit3 	ADCOSR,	ADC oversample rate: 0 = 64x (low power), 1 = 128x (best performance)
		Bit2   	0
		Bit1 	ADC right channel polarity: 0 = normal, 1 = inverted
		Bit0 	ADC left channel polarity: 0 = normal, 1 = inverted
	*/
	if (_InPath & ADC_ON)
	{
		usReg = (1 << 3) | (0 << 8) | (4 << 0);		/* disable the ADC high-pass filter, set cutoff frequency */
	}
	else
	{
		usReg = 0;
	}
	wm8978_WriteReg(14, usReg);	/* write register */

	/* Notch filter (mainly to suppress microphone feedback/howling); disabled for now.
		R27, R28, R29, R30 control the notch filter, WM8978(V4.5_2011).pdf page 33.
		R7 bit7 NFEN: 0 = disabled, 1 = enabled.
	*/
	if (_InPath & ADC_ON)
	{
		usReg = (0 << 7);
		wm8978_WriteReg(27, usReg);	/* write register */
		usReg = 0;
		wm8978_WriteReg(28, usReg);	/* write 0 (disabled, so this is optional) */
		wm8978_WriteReg(29, usReg);	/* write 0 (disabled, so this is optional) */
		wm8978_WriteReg(30, usReg);	/* write 0 (disabled, so this is optional) */
	}

	/* Automatic level control (ALC), R32-R34, WM8978(V4.5_2011).pdf page 36 */
	{
		usReg = 0;		/* disable ALC */
		wm8978_WriteReg(32, usReg);
		wm8978_WriteReg(33, usReg);
		wm8978_WriteReg(34, usReg);
	}

	/*  R35 ALC Noise Gate Control
		Bit3	NGATEN, Noise gate enable
		Bit2:0	Noise gate threshold
	*/
	usReg = (3 << 1) | (7 << 0);		/* ALC disabled */
	wm8978_WriteReg(35, usReg);

	/*
		Mic input channel gain is controlled by PGABOOSTL / PGABOOSTR.
		Aux input gain is controlled by AUXL2BOOSTVO[2:0] / AUXR2BOOSTVO[2:0].
		Line input gain is controlled by LIP2BOOSTVOL[2:0] / RIP2BOOSTVOL[2:0].
	*/
	/*	WM8978(V4.5_2011).pdf page 29, R47 (left) / R48 (right), MIC gain control
		R47 (R48 is identical)
		B8		PGABOOSTL	= 1: 0 = MIC bypassed (no gain), 1 = +20 dB (bootstrap)
		B7		= 0, reserved
		B6:4	L2_2BOOSTVOL = x: 0 = disabled, 1-7 = -12 dB .. +6 dB
		B3		= 0, reserved
		B2:0`	AUXL2BOOSTVOL = x: 0 = disabled, 1-7 = -12 dB .. +6 dB
	*/
	usReg = 0;
	if ((_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= (1 << 8);	/* MIC gain +20 dB */
	}
	if (_InPath & AUX_ON)
	{
		usReg |= (3 << 0);	/* Aux gain fixed at 3 (adjust as needed) */
	}
	if (_InPath & LINE_ON)
	{
		usReg |= (3 << 4);	/* Line gain fixed at 3 (adjust as needed) */
	}
	wm8978_WriteReg(47, usReg);	/* write the left input gain register */
	wm8978_WriteReg(48, usReg);	/* write the right input gain register */

	/* Digital ADC volume, datasheet page 35
		R15 controls left ADC volume, R16 controls right ADC volume
		Bit8 	ADCVU  = 1 updates both channels together
		Bit7:0 	gain: 0000 0000 = mute
					  0000 0001 = -127 dB
					  0000 0010 = -12.5 dB (0.5 dB steps)
					  1111 1111 = 0 dB (no attenuation)
	*/
	usReg = 0xFF;
	wm8978_WriteReg(15, usReg);	/* select 0 dB; cache left channel first */
	usReg = 0x1FF;
	wm8978_WriteReg(16, usReg);	/* update both channels together */

	/* Use wm8978_SetMicGain() to set the mic PGA gain */

	/*	R43 AUXR - ROUT2 BEEP mixer function
		B8:6 = 0

		B5	 MUTERPGA2INV,	Mute input to INVROUT2 mixer
		B4	 INVROUT2,  Invert ROUT2 output (push-pull speaker drive)
		B3:1 BEEPVOL = 7;	AUXR input to ROUT2 inverter gain
		B0	 BEEPEN = 1;	Enable AUXR beep input

	*/
	usReg = 0;
	if (_OutPath & SPK_ON)
	{
		usReg |= (1 << 4);	/* invert ROUT2 to drive the speaker */
	}
	if (_InPath & AUX_ON)
	{
		usReg |= ((7 << 1) | (1 << 0));
	}
	wm8978_WriteReg(43, usReg);

	/* R49 Output ctrl
		B8:7	0
		B6		DACL2RMIX,	Left DAC output to right output mixer
		B5		DACR2LMIX,	Right DAC output to left output mixer
		B4		OUT4BOOST,	0 = OUT4 gain -1 (DC = AVDD/2); 1 = +1.5 (DC = 1.5*AVDD/2)
		B3		OUT3BOOST,	0 = OUT3 gain -1 (DC = AVDD/2); 1 = +1.5 (DC = 1.5*AVDD/2)
		B2		SPKBOOST,	0 = speaker gain -1 (DC = AVDD/2); 1 = +1.5 (DC = 1.5*AVDD/2)
		B1		TSDEN,   Thermal shutdown enable (default 1)
		B0		VROI,	Disabled outputs to VREF resistance
	*/
	usReg = 0;
	if (_InPath & DAC_ON)
	{
		usReg |= ((1 << 6) | (1 << 5));
	}
	if (_OutPath & SPK_ON)
	{
		usReg |=  ((1 << 2) | (1 << 1));	/* SPK 1.5x gain, thermal protection enabled */
	}
	if (_OutPath & OUT3_4_ON)
	{
		usReg |=  ((1 << 4) | (1 << 3));	/* OUT3/OUT4 1.5x gain */
	}
	wm8978_WriteReg(49, usReg);

	/*	REG 50 (50 = left, 51 = right; same layout) WM8978(V4.5_2011).pdf page 56
		B8:6	AUXLMIXVOL = 111	AUX (e.g. FM radio) input volume
		B5		AUXL2LMIX = 1		Left auxiliary input to left channel
		B4:2	BYPLMIXVOL			volume
		B1		BYPL2LMIX = 0;		Left bypass path (left input boost output) to left output mixer
		B0		DACL2LMIX = 1;		Left DAC output to left output mixer
	*/
	usReg = 0;
	if (_InPath & AUX_ON)
	{
		usReg |= ((7 << 6) | (1 << 5));
	}
	if ((_InPath & LINE_ON) || (_InPath & MIC_LEFT_ON) || (_InPath & MIC_RIGHT_ON))
	{
		usReg |= ((7 << 2) | (1 << 1));
	}
	if (_InPath & DAC_ON)
	{
		usReg |= (1 << 0);
	}
	wm8978_WriteReg(50, usReg);
	wm8978_WriteReg(51, usReg);

	/*	R56 OUT3 mixer ctrl
		B8:7	0
		B6		OUT3MUTE,  	0 = output stage outputs OUT3 mixer; 1 = muted (drives VMID)
		B5:4	0
		B3		BYPL2OUT3,	OUT4 mixer output to OUT3 (inverted)
		B4		0
		B2		LMIX2OUT3,	Left ADC input to OUT3
		B1		LDAC2OUT3,	Left DAC mixer to OUT3
		B0		LDAC2OUT3,	Left DAC output to OUT3
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= (1 << 3);
	}
	wm8978_WriteReg(56, usReg);

	/* R57		OUT4 (MONO) mixer ctrl
		B8:7	0
		B6		OUT4MUTE,	0 = output stage outputs OUT4 mixer; 1 = muted (drives VMID)
		B5		HALFSIG,	0 = OUT4 normal output, 1 = OUT4 attenuated by 6 dB
		B4		LMIX2OUT4,	Left DAC mixer to OUT4
		B3		LDAC2UT4,	Left DAC to OUT4
		B2		BYPR2OUT4,	Right ADC input to OUT4
		B1		RMIX2OUT4,	Right DAC mixer to OUT4
		B0		RDAC2OUT4,	Right DAC output to OUT4
	*/
	usReg = 0;
	if (_OutPath & OUT3_4_ON)
	{
		usReg |= ((1 << 4) |  (1 << 1));
	}
	wm8978_WriteReg(57, usReg);


	/* R11, R12 DAC digital volume
		R11		Left DAC digital volume
		R12		Right DAC digital volume
	*/
	if (_InPath & DAC_ON)
	{
		wm8978_WriteReg(11, 255);
		wm8978_WriteReg(12, 255 | 0x100);
	}
	else
	{
		wm8978_WriteReg(11, 0);
		wm8978_WriteReg(12, 0 | 0x100);
	}

	/*	R10 DAC Control
		B8	0
		B7	0
		B6	SOFTMUTE,	Soft mute enable
		B5	0
		B4	0
		B3	DACOSR128,	DAC oversampling: 0 = 64x (lowest power), 1 = 128x (best performance)
		B2	AMUTE,		Automute enable
		B1	DACPOLR,	Right DAC output polarity
		B0	DACPOLL,	Left DAC output polarity
	*/
	if (_InPath & DAC_ON)
	{
		wm8978_WriteReg(10, 0);
	}
	;
}

/**
	* @brief  Configure the notch filter (suppresses microphone feedback/howling)
	* @param  NFA0[13:0] and NFA1[13:0]
	* @retval None
	*/
void wm8978_NotchFilter(uint16_t _NFA0, uint16_t _NFA1)
{
	uint16_t usReg;

	/*  page 26
		A programmable notch filter is provided. This filter has a variable centre frequency and bandwidth,
		programmable via two coefficients, a0 and a1. a0 and a1 are represented by the register bits
		NFA0[13:0] and NFA1[13:0]. Because these coefficient values require four register writes to setup
		there is an NFU (Notch Filter Update) flag which should be set only when all four registers are setup.
	*/
	usReg = (1 << 7) | (_NFA0 & 0x3F);
	wm8978_WriteReg(27, usReg);	/* write register */

	usReg = ((_NFA0 >> 7) & 0x3F);
	wm8978_WriteReg(28, usReg);	/* write register */

	usReg = (_NFA1 & 0x3F);
	wm8978_WriteReg(29, usReg);	/* write register */

	usReg = (1 << 8) | ((_NFA1 >> 7) & 0x3F);
	wm8978_WriteReg(30, usReg);	/* write register */
}


/**
	* @brief  Drive the WM8978 GPIO1 pin to 0 or 1 to switch the analog path
	*		  1 = playback
	*		  0 = recording
	* @param  _ucValue : GPIO1 output value (0 or 1)
	* @retval None
	*/
void wm8978_CtrlGPIO1(uint8_t _ucValue)
{
	uint16_t usRegValue;

	/* R8, datasheet page 62 */
	if (_ucValue == 0) /* output 0 */
	{
		usRegValue = 6; /* B2:0 = 110 */
	}
	else
	{
		usRegValue = 7; /* B2:0 = 111 */
	}
	wm8978_WriteReg(8, usRegValue);
}

/**
	* @brief  Reset the WM8978; all registers return to their default values
	* @param  None
	* @retval 1: reset OK
	* 				0: reset failed
	*/
uint8_t wm8978_Reset(void)
{
	/* WM8978 register defaults */
	const uint16_t reg_default[] = {
	0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
	0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
	0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
	0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
	0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
	0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
	0x001, 0x001
	};
	uint8_t res;
	uint8_t i;

	res=wm8978_WriteReg(0x00, 0);

	for (i = 0; i < sizeof(reg_default) / 2; i++)
	{
		wm8978_RegCash[i] = reg_default[i];
	}
	return res;
}


/***************************** (END OF FILE) *********************************/
