/*
NOTE:
The modification is appended to initialization of image sensor. 
After sensor initialization, use the function
bool otp_update_wb(unsigned char golden_rg, unsigned char golden_bg),
then the calibration of AWB will be applied. 
After finishing the OTP written, we will provide you the golden_rg and golden_bg settings.
*/

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
	
#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "ov5647mipi_Sensor.h"
#include "ov5647mipi_Camera_Sensor_para.h"
#include "ov5647mipi_CameraCustomized.h"

extern kal_uint16 OV5647MIPI_write_cmos_sensor(kal_uint32 addr, kal_uint32 para);
extern kal_uint16 OV5647MIPI_read_cmos_sensor(kal_uint32 addr);

//#define SUPPORT_FLOATING

#define OTP_DATA_ADDR         0x3D00
#define OTP_LOAD_ADDR         0x3D21

#define OTP_WB_GROUP_ADDR     0x3D05
#define OTP_WB_GROUP_SIZE     9

#define GAIN_RH_ADDR          0x5186
#define GAIN_RL_ADDR          0x5187
#define GAIN_GH_ADDR          0x5188
#define GAIN_GL_ADDR          0x5189
#define GAIN_BH_ADDR          0x518A
#define GAIN_BL_ADDR          0x518B

#define GAIN_DEFAULT_VALUE    0x0400 // 1x gain

#define OTP_MID               0x02


// R/G and B/G of current camera module
unsigned char rg_ratio = 0;
unsigned char bg_ratio = 0;


// Enable OTP read function
void otp_read_enable(void)
{
	OV5647MIPI_write_cmos_sensor(OTP_LOAD_ADDR, 0x01);
	//kal_sleep_task(10); // sleep > 10ms
	mdelay(10); // sleep > 10ms
}

// Disable OTP read function
void otp_read_disable(void)
{
	OV5647MIPI_write_cmos_sensor(OTP_LOAD_ADDR, 0x00);
}

void otp_read(unsigned short otp_addr, unsigned char* otp_data)
{
	otp_read_enable();
	*otp_data = OV5647MIPI_read_cmos_sensor(otp_addr);
	otp_read_disable();
}

/*******************************************************************************
* Function    :  otp_clear
* Description :  Clear OTP buffer 
* Parameters  :  none
* Return      :  none
*******************************************************************************/	
void otp_clear(void)
{
	// After read/write operation, the OTP buffer should be cleared to avoid accident write
	unsigned char i;
	for (i=0; i<32; i++) 
	{
		OV5647MIPI_write_cmos_sensor(OTP_DATA_ADDR+i, 0x00);
	}
}

/*******************************************************************************
* Function    :  otp_check_wb_group
* Description :  Check OTP Space Availability
* Parameters  :  [in] index : index of otp group (0, 1, 2)
* Return      :  0, group index is empty
                 1, group index has invalid data
                 2, group index has valid data
                -1, group index error
*******************************************************************************/	
signed char otp_check_wb_group(unsigned char index)
{   
	unsigned short otp_addr = OTP_WB_GROUP_ADDR + index * OTP_WB_GROUP_SIZE;
	unsigned char  flag;

    if (index > 2)
	{
		SENSORDB("OTP input wb group index %d error\n", index);
		return -1;
	}
		
	otp_read(otp_addr, &flag);
	otp_clear();

	// Check all bytes of a group. If all bytes are '0', then the group is empty. 
	// Check from group 1 to group 2, then group 3.
	if (!flag)
	{
		SENSORDB("wb group %d is empty\n", index);
		return 0;
	}
	else if ((!(flag&0x80)) && (flag&0x7f))
	{
		SENSORDB("wb group %d has valid data\n", index);
		return 2;
	}
	else
	{
	//	SENSORDB("wb group %d has invalid data\n", index);
		return 1;
	}
}

/*******************************************************************************
* Function    :  otp_read_wb_group
* Description :  Read group value and store it in OTP Struct 
* Parameters  :  [in] index : index of otp group (0, 1, 2)
* Return      :  group index (0, 1, 2)
                 -1, error
*******************************************************************************/	
signed char otp_read_wb_group(signed char index)
{
	unsigned short otp_addr;
	unsigned char  mid;

	if (index == -1)
	{
		// Check first OTP with valid data
		for (index=0; index<3; index++)
		{
			if (otp_check_wb_group(index) == 2)
			{
			//	SENSORDB("read wb from group %d", index);
				break;
			}
		}

		if (index > 2)
		{
		//	SENSORDB("no group has valid data\n");
			return -1;
		}
	}
	else
	{
		if (otp_check_wb_group(index) != 2)
		{
		//	SENSORDB("read wb from group %d failed\n", index);
			return -1;
		}
	}

	otp_addr = OTP_WB_GROUP_ADDR + index * OTP_WB_GROUP_SIZE;

	otp_read(otp_addr, &mid);
	if ((mid&0x7f) != OTP_MID)
	{
		return -1;
	}

	otp_read(otp_addr+2, &rg_ratio);
	otp_read(otp_addr+3, &bg_ratio);
	otp_clear();

	SENSORDB("read wb finished\n");
	return index;
}

#ifdef SUPPORT_FLOATING //Use this if support floating point values
/*******************************************************************************
* Function    :  otp_apply_wb
* Description :  Calcualte and apply R, G, B gain to module
* Parameters  :  [in] golden_rg : R/G of golden camera module
                 [in] golden_bg : B/G of golden camera module
* Return      :  1, success; 0, fail
*******************************************************************************/	
bool otp_apply_wb(unsigned char golden_rg, unsigned char golden_bg)
{
	unsigned short gain_r = GAIN_DEFAULT_VALUE;
	unsigned short gain_g = GAIN_DEFAULT_VALUE;
	unsigned short gain_b = GAIN_DEFAULT_VALUE;

	double ratio_r, ratio_g, ratio_b;
	double cmp_rg, cmp_bg;

	if (!golden_rg || !golden_bg)
	{
		SENSORDB("golden_rg / golden_bg can not be zero\n");
		return 0;
	}

	// Calcualte R, G, B gain of current module from R/G, B/G of golden module
    // and R/G, B/G of current module
	cmp_rg = 1.0 * rg_ratio / golden_rg;
	cmp_bg = 1.0 * bg_ratio / golden_bg;

	if ((cmp_rg<1) && (cmp_bg<1))
	{
		// R/G < R/G golden, B/G < B/G golden
		ratio_g = 1;
		ratio_r = 1 / cmp_rg;
		ratio_b = 1 / cmp_bg;
	}
	else if (cmp_rg > cmp_bg)
	{
		// R/G >= R/G golden, B/G < B/G golden
		// R/G >= R/G golden, B/G >= B/G golden
		ratio_r = 1;
		ratio_g = cmp_rg;
		ratio_b = cmp_rg / cmp_bg;
	}
	else
	{
		// B/G >= B/G golden, R/G < R/G golden
		// B/G >= B/G golden, R/G >= R/G golden
		ratio_b = 1;
		ratio_g = cmp_bg;
		ratio_r = cmp_bg / cmp_rg;
	}

	// write sensor wb gain to registers
	// 0x0400 = 1x gain
	if (ratio_r != 1)
	{
		gain_r = (unsigned short)(GAIN_DEFAULT_VALUE * ratio_r);
		OV5647MIPI_write_cmos_sensor(GAIN_RH_ADDR, gain_r >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_RL_ADDR, gain_r & 0x00ff);
	}

	if (ratio_g != 1)
	{
		gain_g = (unsigned short)(GAIN_DEFAULT_VALUE * ratio_g);
		OV5647MIPI_write_cmos_sensor(GAIN_GH_ADDR, gain_g >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_GL_ADDR, gain_g & 0x00ff);
	}

	if (ratio_b != 1)
	{
		gain_b = (unsigned short)(GAIN_DEFAULT_VALUE * ratio_b);
		OV5647MIPI_write_cmos_sensor(GAIN_BH_ADDR, gain_b >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_BL_ADDR, gain_b & 0x00ff);
	}

	SENSORDB("cmp_rg=%f, cmp_bg=%f\n", cmp_rg, cmp_bg);
	SENSORDB("ratio_r=%f, ratio_g=%f, ratio_b=%f\n", ratio_r, ratio_g, ratio_b);
	SENSORDB("gain_r=0x%x, gain_g=0x%x, gain_b=0x%x\n", gain_r, gain_g, gain_b);
	return 1;
}

#else //Use this if not support floating point values

#define OTP_MULTIPLE_FAC	10000
bool otp_apply_wb(unsigned char golden_rg, unsigned char golden_bg)
{
	unsigned short gain_r = GAIN_DEFAULT_VALUE;
	unsigned short gain_g = GAIN_DEFAULT_VALUE;
	unsigned short gain_b = GAIN_DEFAULT_VALUE;

	unsigned short ratio_r, ratio_g, ratio_b;
	unsigned short cmp_rg, cmp_bg;

	if (!golden_rg || !golden_bg)
	{
		SENSORDB("golden_rg / golden_bg can not be zero\n");
		return 0;
	}

	// Calcualte R, G, B gain of current module from R/G, B/G of golden module
    // and R/G, B/G of current module
	cmp_rg = OTP_MULTIPLE_FAC * rg_ratio / golden_rg;
	cmp_bg = OTP_MULTIPLE_FAC * bg_ratio / golden_bg;

	if ((cmp_rg < 1 * OTP_MULTIPLE_FAC) && (cmp_bg < 1 * OTP_MULTIPLE_FAC))
	{
		// R/G < R/G golden, B/G < B/G golden
		ratio_g = 1 * OTP_MULTIPLE_FAC;
		ratio_r = 1 * OTP_MULTIPLE_FAC * OTP_MULTIPLE_FAC / cmp_rg;
		ratio_b = 1 * OTP_MULTIPLE_FAC * OTP_MULTIPLE_FAC / cmp_bg;
	}
	else if (cmp_rg > cmp_bg)
	{
		// R/G >= R/G golden, B/G < B/G golden
		// R/G >= R/G golden, B/G >= B/G golden
		ratio_r = 1 * OTP_MULTIPLE_FAC;
		ratio_g = cmp_rg;
		ratio_b = OTP_MULTIPLE_FAC * cmp_rg / cmp_bg;
	}
	else
	{
		// B/G >= B/G golden, R/G < R/G golden
		// B/G >= B/G golden, R/G >= R/G golden
		ratio_b = 1 * OTP_MULTIPLE_FAC;
		ratio_g = cmp_bg;
		ratio_r = OTP_MULTIPLE_FAC * cmp_bg / cmp_rg;
	}

	// write sensor wb gain to registers
	// 0x0400 = 1x gain
	if (ratio_r != 1 * OTP_MULTIPLE_FAC)
	{
		gain_r = GAIN_DEFAULT_VALUE * ratio_r / OTP_MULTIPLE_FAC;
		OV5647MIPI_write_cmos_sensor(GAIN_RH_ADDR, gain_r >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_RL_ADDR, gain_r & 0x00ff);
	}

	if (ratio_g != 1 * OTP_MULTIPLE_FAC)
	{
		gain_g = GAIN_DEFAULT_VALUE * ratio_g / OTP_MULTIPLE_FAC;
		OV5647MIPI_write_cmos_sensor(GAIN_GH_ADDR, gain_g >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_GL_ADDR, gain_g & 0x00ff);
	}

	if (ratio_b != 1 * OTP_MULTIPLE_FAC)
	{
		gain_b = GAIN_DEFAULT_VALUE * ratio_b / OTP_MULTIPLE_FAC;
		OV5647MIPI_write_cmos_sensor(GAIN_BH_ADDR, gain_b >> 8);
		OV5647MIPI_write_cmos_sensor(GAIN_BL_ADDR, gain_b & 0x00ff);
	}

	SENSORDB("cmp_rg=%d, cmp_bg=%d\n", cmp_rg, cmp_bg);
	SENSORDB("ratio_r=%d, ratio_g=%d, ratio_b=%d\n", ratio_r, ratio_g, ratio_b);
	SENSORDB("gain_r=0x%x, gain_g=0x%x, gain_b=0x%x\n", gain_r, gain_g, gain_b);
	return 1;
}
#endif /* SUPPORT_FLOATING */

/*******************************************************************************
* Function    :  otp_update_wb
* Description :  Update white balance settings from OTP
* Parameters  :  [in] golden_rg : R/G of golden camera module
                 [in] golden_bg : B/G of golden camera module
* Return      :  1, success; 0, fail
*******************************************************************************/	
bool otp_update_wb(unsigned char golden_rg, unsigned char golden_bg) 
{
	SENSORDB("start wb update\n");

	if (otp_read_wb_group(-1) != -1)
	{
		if (otp_apply_wb(golden_rg, golden_bg) == 1)
		{
			SENSORDB("wb update finished\n");
			return 1;
		}
	}

	SENSORDB("wb update failed\n");
	return 0;
}

