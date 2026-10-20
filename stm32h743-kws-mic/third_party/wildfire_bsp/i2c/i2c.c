/**
  ******************************************************************************
  * @file    i2c.c
  * @author  fire
  * @version V1.0
  * @date    2015-xx-xx
  * @brief   I2C bus driver for communicating with I2C sensors.
  ******************************************************************************
  * @attention
  *
  * Platform : Wildfire STM32 H743 development board
  * Forum    : http://www.firebbs.cn
  * Shop     : http://firestm32.taobao.com
  *
  ******************************************************************************
  */ 
#include "./i2c/i2c.h"

  
I2C_HandleTypeDef I2C_Handle;					
/*******************************  Function ************************************/

/**
  * @brief  Initialize the I2C bus. Must be called before using I2C.
  * @param  None
  * @retval None
  */
void I2cMaster_Init(void) 
{
	GPIO_InitTypeDef GPIO_InitStructure;

	/* Enable the I2Cx clock */
	SENSORS_I2C_RCC_CLK_ENABLE();

	/* Enable the I2C GPIO clocks */
	SENSORS_I2C_SCL_GPIO_CLK_ENABLE();
	SENSORS_I2C_SDA_GPIO_CLK_ENABLE();

	/* Configure I2Cx pin: SCL ----------------------------------*/
	GPIO_InitStructure.Pin =  SENSORS_I2C_SCL_GPIO_PIN; 
	GPIO_InitStructure.Mode = GPIO_MODE_AF_OD;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStructure.Pull= GPIO_NOPULL;
	GPIO_InitStructure.Alternate=SENSORS_I2C_AF; 
	HAL_GPIO_Init(SENSORS_I2C_SCL_GPIO_PORT, &GPIO_InitStructure);

	/* Configure I2Cx pin: SDA ----------------------------------*/
	GPIO_InitStructure.Pin = SENSORS_I2C_SDA_GPIO_PIN;  
	HAL_GPIO_Init(SENSORS_I2C_SDA_GPIO_PORT, &GPIO_InitStructure); 
	
	if(HAL_I2C_GetState(&I2C_Handle) == HAL_I2C_STATE_RESET)
	{	
		/* Force a reset of the I2C peripheral clock */  
		SENSORS_I2C_FORCE_RESET(); 

		/* Release the I2C peripheral clock reset */  
		SENSORS_I2C_RELEASE_RESET(); 
		
		/* I2C configuration */
		I2C_Handle.Instance = SENSORS_I2C;
		I2C_Handle.Init.Timing           = 0x40604E73;//100KHz
		I2C_Handle.Init.OwnAddress1      = 0;
		I2C_Handle.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
		I2C_Handle.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
		I2C_Handle.Init.OwnAddress2      = 0;
		I2C_Handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
		I2C_Handle.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
		I2C_Handle.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

		/* Initialize I2C */
		HAL_I2C_Init(&I2C_Handle);	
		/* Enable the analog filter */
		HAL_I2CEx_AnalogFilter_Config(&I2C_Handle, I2C_ANALOGFILTER_ENABLE); 
	}
}
/**
  * @brief  Manages error callback by re-initializing I2C.
  * @param  Addr: I2C Address
  * @retval None
  */
static void I2Cx_Error(uint8_t Addr)
{
	/* Restore I2C registers to their default values */
	HAL_I2C_DeInit(&I2C_Handle); 
	/* Re-initialize the I2C peripheral */
	I2cMaster_Init();
}
/**
  * @brief  Write a register (upper-layer interface)
	* @param  slave_addr: slave address
	* @param 	reg_addr: register address
	* @param len: number of bytes to write
	*	@param data_ptr: pointer to the data to write
  * @retval 0 on success, non-zero on failure
  */
int Sensors_I2C_WriteRegister(unsigned char slave_addr,
                                        unsigned char reg_addr,
                                        unsigned short len, 
                                        unsigned char *data_ptr)
{
	HAL_StatusTypeDef status = HAL_OK;
	status = HAL_I2C_Mem_Write(&I2C_Handle, slave_addr, reg_addr, I2C_MEMADD_SIZE_8BIT,data_ptr, len,I2Cx_FLAG_TIMEOUT); 
	/* Check the transfer status */
	if(status != HAL_OK)
	{
		/* Bus error handling */
		I2Cx_Error(slave_addr);
	}
	while (HAL_I2C_GetState(&I2C_Handle) != HAL_I2C_STATE_READY)
	{
		
	}
	/* Wait until the sensor is ready for the next transfer */
	while (HAL_I2C_IsDeviceReady(&I2C_Handle, slave_addr, I2Cx_FLAG_TIMEOUT, I2Cx_FLAG_TIMEOUT) == HAL_TIMEOUT);
	/* Wait for the transfer to finish */
	while (HAL_I2C_GetState(&I2C_Handle) != HAL_I2C_STATE_READY)
	{
		
	}
	return status;
}

/**
  * @brief  Read a register (upper-layer interface)
	* @param  slave_addr: slave address
	* @param 	reg_addr: register address
	* @param len: number of bytes to read
	*	@param data_ptr: pointer to the destination buffer
  * @retval 0 on success, non-zero on failure
  */
int Sensors_I2C_ReadRegister(unsigned char slave_addr,
                                       unsigned char reg_addr,
                                       unsigned short len, 
                                       unsigned char *data_ptr)
{
	HAL_StatusTypeDef status = HAL_OK;
	status =HAL_I2C_Mem_Read(&I2C_Handle,slave_addr,reg_addr,I2C_MEMADD_SIZE_8BIT,data_ptr,len,I2Cx_FLAG_TIMEOUT);    
	/* Check the transfer status */
	if(status != HAL_OK)
	{
		/* Bus error handling */
		I2Cx_Error(slave_addr);
	}
	while (HAL_I2C_GetState(&I2C_Handle) != HAL_I2C_STATE_READY)
	{
		
	}
	/* Wait until the sensor is ready for the next transfer */
	while (HAL_I2C_IsDeviceReady(&I2C_Handle, slave_addr, I2Cx_FLAG_TIMEOUT, I2Cx_FLAG_TIMEOUT) == HAL_TIMEOUT);
	/* Wait for the transfer to finish */
	while (HAL_I2C_GetState(&I2C_Handle) != HAL_I2C_STATE_READY)
	{
		
	}
	return status;
}
