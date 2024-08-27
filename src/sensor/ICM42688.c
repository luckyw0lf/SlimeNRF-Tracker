/* 01/14/2022 Copyright Tlera Corporation

    Created by Kris Winer

  This sketch uses SDA/SCL on pins 21/20 (Ladybug default), respectively, and it uses the Ladybug STM32L432 Breakout Board.
  The ICM42688 is a combo sensor with embedded accel and gyro, here used as 6 DoF in a 9 DoF absolute orientation solution.

  Library may be used freely and without limit with attribution.

*/
#include <math.h>

#include <zephyr/drivers/i2c.h>

#include "ICM42688.h"

static float _aRes = 16.0f/32768.0f; // Always 16G
static float _gRes = 2000.0f/32768.0f; // Always 2000dps
uint8_t icm_last_accel_odr = 0xff;
uint8_t icm_last_gyro_odr = 0xff;
const float icm_clock_reference = 32000;
float icm_clock_scale = 1; // ODR is scaled by clock_rate/clock_reference

int icm_init(struct i2c_dt_spec dev_i2c, float clock_rate, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
//	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INT_SOURCE0, 0x00); // disable default interrupt (RESET_DONE)
	if (clock_rate > 0)
	{
		icm_clock_scale = clock_rate / icm_clock_reference;
		i2c_reg_write_byte_dt(&dev_i2c, ICM42688_REG_BANK_SEL, 0x01); // select register bank 1
		i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INTF_CONFIG5, 0x04); // use CLKIN (set PIN9_FUNCTION to CLKIN)
		i2c_reg_write_byte_dt(&dev_i2c, ICM42688_REG_BANK_SEL, 0x00); // select register bank 0
		i2c_reg_update_byte_dt(&dev_i2c, ICM42688_INTF_CONFIG1, 0x04, 0x04); // use CLKIN (set RTC_MODE to require RTC clock input)
//		i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INTF_CONFIG1, 0x91 | 0x04); // use CLKIN (set RTC_MODE to require RTC clock input)
	}
	icm_last_accel_odr = 0xff; // reset last odr
	icm_last_gyro_odr = 0xff; // reset last odr
	int err = icm_update_odr(dev_i2c, accel_time, gyro_time, accel_actual_time, gyro_actual_time);
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_GYRO_ACCEL_CONFIG0, 0x44); // set gyro and accel bandwidth to ODR/10
//	k_msleep(50); // 10ms Accel, 30ms Gyro startup
	k_msleep(1); // fuck i dont wanna wait that long
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_FIFO_CONFIG, 0x00); // FIFO bypass mode
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_FSYNC_CONFIG, 0x00); // disable FSYNC
	i2c_reg_update_byte_dt(&dev_i2c, ICM42688_TMST_CONFIG, 0x02, 0x00); // disable FSYNC
//	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_TMST_CONFIG, (0x23 | 0x02) ^ 0x02); // disable FSYNC
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_FIFO_CONFIG1, 0x02); // enable FIFO gyro only
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_FIFO_CONFIG, 1<<6); // begin FIFO stream
	return (err < 0 ? 0 : err);
}

void icm_shutdown(struct i2c_dt_spec dev_i2c)
{
	icm_last_accel_odr = 0xff; // reset last odr
	icm_last_gyro_odr = 0xff; // reset last odr
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_DEVICE_CONFIG, 0x01); // Don't need to wait for ICM to finish reset
}

int icm_update_odr(struct i2c_dt_spec dev_i2c, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int ODR;
	uint8_t Ascale = AFS_16G; // set highest
	uint8_t Gscale = GFS_2000DPS; // set highest
	uint8_t aMode;
	uint8_t gMode;
	uint8_t AODR;
	uint8_t GODR;

	// Calculate accel
	if (accel_time <= 0 || accel_time == INFINITY) // off, standby interpreted as off
	{
		aMode = aMode_OFF;
		ODR = 0;
	}
	else
	{
		aMode = aMode_LN;
		ODR = 1 / accel_time;
		ODR /= icm_clock_scale; // scale clock
	}

	if (aMode != aMode_LN)
	{
		AODR = 0;
		accel_time = 0; // off
	}
	else if (ODR > 16000) // TODO: this is absolutely awful
	{
		AODR = AODR_32kHz;
		accel_time = 1.0 / 32000;
	}
	else if (ODR > 8000)
	{
		AODR = AODR_16kHz;
		accel_time = 1.0 / 16000;
	}
	else if (ODR > 4000)
	{
		AODR = AODR_8kHz;
		accel_time = 1.0 / 8000;
	}
	else if (ODR > 2000)
	{
		AODR = AODR_4kHz;
		accel_time = 1.0 / 4000;
	}
	else if (ODR > 1000)
	{
		AODR = AODR_2kHz;
		accel_time = 1.0 / 2000;
	}
	else if (ODR > 500)
	{
		AODR = AODR_1kHz;
		accel_time = 1.0 / 1000;
	}
	else if (ODR > 200)
	{
		AODR = AODR_500Hz;
		accel_time = 1.0 / 500;
	}
	else if (ODR > 100)
	{
		AODR = AODR_200Hz;
		accel_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		AODR = AODR_100Hz;
		accel_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		AODR = AODR_50Hz;
		accel_time = 1.0 / 50;
	}
	else if (ODR > 12)
	{
		AODR = AODR_25Hz;
		accel_time = 1.0 / 25;
	}
	else
	{
		AODR = AODR_12_5Hz;
		accel_time = 1.0 / 12.5;
	}
	accel_time *= icm_clock_scale; // scale clock

	// Calculate gyro
	if (gyro_time <= 0) // off
	{
		gMode = gMode_OFF;
		ODR = 0;
	}
	else if (gyro_time == INFINITY) // standby
	{
		gMode = gMode_SBY;
		ODR = 0;
	}
	else
	{
		gMode = gMode_LN;
		ODR = 1 / gyro_time;
		ODR /= icm_clock_scale; // scale clock
	}

	if (gMode != gMode_LN)
	{
		GODR = 0;
		gyro_time = 0; // off
	}
	else if (ODR > 16000) // TODO: this is absolutely awful
	{
		GODR = GODR_32kHz;
		gyro_time = 1.0 / 32000;
	}
	else if (ODR > 8000)
	{
		GODR = GODR_16kHz;
		gyro_time = 1.0 / 16000;
	}
	else if (ODR > 4000)
	{
		GODR = GODR_8kHz;
		gyro_time = 1.0 / 8000;
	}
	else if (ODR > 2000)
	{
		GODR = GODR_4kHz;
		gyro_time = 1.0 / 4000;
	}
	else if (ODR > 1000)
	{
		GODR = GODR_2kHz;
		gyro_time = 1.0 / 2000;
	}
	else if (ODR > 500)
	{
		GODR = GODR_1kHz;
		gyro_time = 1.0 / 1000;
	}
	else if (ODR > 200)
	{
		GODR = GODR_500Hz;
		gyro_time = 1.0 / 500;
	}
	else if (ODR > 100)
	{
		GODR = GODR_200Hz;
		gyro_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		GODR = GODR_100Hz;
		gyro_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		GODR = GODR_50Hz;
		gyro_time = 1.0 / 50;
	}
	else if (ODR > 12)
	{
		GODR = GODR_25Hz;
		gyro_time = 1.0 / 25;
	}
	else
	{
		GODR = GODR_12_5Hz;
		gyro_time = 1.0 / 12.5;
	}
	gyro_time *= icm_clock_scale; // scale clock

	if (icm_last_accel_odr == AODR && icm_last_gyro_odr == GODR) // if both were already configured
		return 1;

	// only if the power mode has changed
	if ((icm_last_accel_odr == 0 ? 0 : 1) != (AODR == 0 ? 0 : 1) || (icm_last_gyro_odr == 0 ? 0 : 1) != (GODR == 0 ? 0 : 1))
	{ // TODO: can't tell difference between gyro off and gyro standby
		i2c_reg_write_byte_dt(&dev_i2c, ICM42688_PWR_MGMT0, gMode << 2 | aMode); // set accel and gyro modes
		k_busy_wait(250); // wait >200us (datasheet 14.36)
	}
	icm_last_accel_odr = AODR;
	icm_last_gyro_odr = GODR;

	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_ACCEL_CONFIG0, Ascale << 5 | AODR); // set accel ODR and FS
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_GYRO_CONFIG0, Gscale << 5 | GODR); // set gyro ODR and FS

	*accel_actual_time = accel_time;
	*gyro_actual_time = gyro_time;

	return 0;
}

uint16_t icm_fifo_read(struct i2c_dt_spec dev_i2c, uint8_t *data)
{
	uint8_t rawCount[2];
	i2c_burst_read_dt(&dev_i2c, ICM42688_FIFO_COUNTH, &rawCount[0], 2);
	uint16_t count = (uint16_t)(rawCount[0] << 8 | rawCount[1]); // Turn the 16 bits into a unsigned 16-bit value
	count += 32; // Add a few read buffer packets, since the FIFO may contain more data than when we begin reading (allowing 4ms of transaction time for 1000hz ODR)
	uint16_t packets = count / 8; // FIFO packet size is 8 bytes for Packet 2
	uint16_t offset = 0;
	uint8_t addr = ICM42688_FIFO_DATA;
	i2c_write_dt(&dev_i2c, &addr, 1); // Start read buffer
	while (count > 0)
	{
		i2c_read_dt(&dev_i2c, &data[offset], count > 248 ? 248 : count); // Read less than 255 at a time (for nRF52832)
		offset += 248;
		count = count > 248 ? count - 248 : 0;
	}
	return packets;
}

int icm_fifo_process(uint16_t index, uint8_t *data, float g[3])
{
	index *= 8; // Packet size 8 bytes
	if ((data[index] & 0x80) == 0x80)
		return 1; // Skip empty packets
	// combine into 16 bit values
	float raw[3];
	for (int i = 0; i < 3; i++) { // gx, gy, gz
		raw[i] = (int16_t)((((int16_t)data[index + (i * 2) + 1]) << 8) | data[index + (i * 2) + 2]);
		raw[i] *= _gRes;
	}
	// data[index + 7] is temperature
	// but it is lower precision
	// Temperature in Degrees Centigrade = (FIFO_TEMP_DATA / 2.07) + 25
	if (raw[0] < -32766 || raw[1] < -32766 || raw[2] < -32766)
		return 1; // Skip invalid data
	memcpy(g, raw, sizeof(raw));
	return 0;
}

void icm_accel_read(struct i2c_dt_spec dev_i2c, float a[3])
{
	uint8_t rawAccel[6];
	i2c_burst_read_dt(&dev_i2c, ICM42688_ACCEL_DATA_X1, &rawAccel[0], 6);
	float accel0 = (int16_t)((((int16_t)rawAccel[0]) << 8) | rawAccel[1]);
	float accel1 = (int16_t)((((int16_t)rawAccel[2]) << 8) | rawAccel[3]);
	float accel2 = (int16_t)((((int16_t)rawAccel[4]) << 8) | rawAccel[5]);
	a[0] = accel0 * _aRes;
	a[1] = accel1 * _aRes;
	a[2] = accel2 * _aRes;
}

void icm_gyro_read(struct i2c_dt_spec dev_i2c, float g[3])
{
	uint8_t rawGyro[6];
	i2c_burst_read_dt(&dev_i2c, ICM42688_GYRO_DATA_X1, &rawGyro[0], 6);
	float gyro0 = (int16_t)((((int16_t)rawGyro[0]) << 8) | rawGyro[1]);
	float gyro1 = (int16_t)((((int16_t)rawGyro[2]) << 8) | rawGyro[3]);
	float gyro2 = (int16_t)((((int16_t)rawGyro[4]) << 8) | rawGyro[5]);
	g[0] = gyro0 * _gRes;
	g[1] = gyro1 * _gRes;
	g[2] = gyro2 * _gRes;
}

float icm_temp_read(struct i2c_dt_spec dev_i2c)
{
	uint8_t rawTemp[2];
	i2c_burst_read_dt(&dev_i2c, ICM42688_TEMP_DATA0, &rawTemp[0], 2);
	// Temperature in Degrees Centigrade = (TEMP_DATA / 132.48) + 25
	float temp = (int16_t)((((int16_t)rawTemp[0]) << 8) | rawTemp[1]);
	temp /= 132.48;
	temp += 25;
	return temp;
}

void icm_setup_WOM(struct i2c_dt_spec dev_i2c)
{
	uint8_t interrupts;
	i2c_reg_read_byte_dt(&dev_i2c, ICM42688_INT_STATUS, &interrupts); // clear reset done int flag
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INT_SOURCE0, 0x00); // disable default interrupt (RESET_DONE)
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_ACCEL_CONFIG0, AFS_8G << 5 | AODR_200Hz); // set accel ODR and FS
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_PWR_MGMT0, aMode_LP); // set accel and gyro modes
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INTF_CONFIG1, 0x00); // set low power clock
	k_busy_wait(1000);
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_REG_BANK_SEL, 0x04); // select register bank 4
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_ACCEL_WOM_X_THR, 0x08); // set wake thresholds // 80 x 3.9 mg is ~312 mg
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_ACCEL_WOM_Y_THR, 0x08); // set wake thresholds
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_ACCEL_WOM_Z_THR, 0x08); // set wake thresholds
	k_busy_wait(1000);
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_REG_BANK_SEL, 0x00); // select register bank 0
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_INT_SOURCE1, 0x07); // enable WOM interrupt
	k_busy_wait(50000);
	i2c_reg_write_byte_dt(&dev_i2c, ICM42688_SMD_CONFIG, 0x01); // enable WOM feature
}

const sensor_imu_t sensor_imu_icm42688 = {
	*icm_init,
	*icm_shutdown,

	*icm_update_odr,

	*icm_fifo_read,
	*icm_fifo_process,
	*icm_accel_read,
	*icm_gyro_read,
	*icm_temp_read,

	*icm_setup_WOM
};
