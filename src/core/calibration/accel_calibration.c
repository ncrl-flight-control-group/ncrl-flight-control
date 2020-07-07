#include <math.h>
#include "FreeRTOS.h"
#include "task.h"
#include "delay.h"
#include "../../lib/mavlink_v2/ncrl_mavlink/mavlink.h"
#include "ncrl_mavlink.h"
#include "../mavlink/publisher.h"
#include "imu.h"
#include "sys_time.h"
#include "quadshell.h"
#include "mavlink_task.h"

#define ACCEL_CALIB_SAMPLING_TIMES 2000

enum {
	ACCEL_CALIB_FRONT,
	ACCEL_CALIB_BACK,
	ACCEL_CALIB_UP,
	ACCEL_CALIB_DOWN,
	ACCEL_CALIB_LEFT,
	ACCEL_CALIB_RIGHT,
	ACCEL_CALIB_DIR_UNKNOWN
} ACCEL_CALIB_DIR;

static int detect_accel_orientation(float *accel)
{
	bool x_is_neg = accel[0] < 0.0f ? true : false;
	bool y_is_neg = accel[1] < 0.0f ? true : false;
	bool z_is_neg = accel[2] < 0.0f ? true : false;

	float x_norm = fabs(accel[0]);
	float y_norm = fabs(accel[1]);
	float z_norm = fabs(accel[2]);

	if(x_norm > y_norm && x_norm > z_norm) {
		/* x norm is the biggest */
		return x_is_neg == true ? ACCEL_CALIB_FRONT : ACCEL_CALIB_BACK;
	} else if(y_norm > x_norm && y_norm > z_norm) {
		/* y norm is the biggest */
		return y_is_neg == true ? ACCEL_CALIB_RIGHT : ACCEL_CALIB_LEFT ;
	} else if(z_norm > x_norm && z_norm > y_norm) {
		/* z norm is the biggest */
		return z_is_neg == true ? ACCEL_CALIB_DOWN : ACCEL_CALIB_UP;
	}

	return ACCEL_CALIB_DIR_UNKNOWN;
}

static float capture_accel_gavity_vaule_x(bool cap_neg)
{
	float accel[3];

	float gravity = 0.0f;

	int i;
	for(i = 0; i < ACCEL_CALIB_SAMPLING_TIMES; i++) {
		/* read sensor */
		get_imu_filtered_accel(accel);
		gravity += accel[0] / ACCEL_CALIB_SAMPLING_TIMES;
		freertos_task_delay(2.5);
	}

	return gravity;
}

static float capture_accel_gavity_vaule_y(bool cap_neg)
{
	float accel[3];

	float gravity = 0.0f;

	int i;
	for(i = 0; i < ACCEL_CALIB_SAMPLING_TIMES; i++) {
		/* read sensor */
		get_imu_filtered_accel(accel);
		gravity += accel[1] / ACCEL_CALIB_SAMPLING_TIMES;
		freertos_task_delay(2.5);
	}

	return gravity;
}


static float capture_accel_gavity_vaule_z(bool cap_neg)
{
	float accel[3];

	float gravity = 0.0f;

	int i;
	for(i = 0; i < ACCEL_CALIB_SAMPLING_TIMES; i++) {
		/* read sensor */
		get_imu_filtered_accel(accel);
		gravity += accel[2] / ACCEL_CALIB_SAMPLING_TIMES;
		freertos_task_delay(2.5);
	}

	return gravity;
}

bool detect_accel_motion(float *accel)
{
	static float accel_last[3] = {0.0f};

	float accel_change[3];
	accel_change[0] = accel[0] - accel_last[0];
	accel_change[1] = accel[1] - accel_last[1];
	accel_change[2] = accel[2] - accel_last[2];

	float accel_change_norm = sqrt(accel_change[0] * accel_change[0] +
	                               accel_change[1] * accel_change[1] +
	                               accel_change[2] * accel_change[2]);

	accel_last[0] = accel[0];
	accel_last[1] = accel[1];
	accel_last[2] = accel[2];

	if(accel_change_norm > 0.98) {
		return true;
	} else {
		return false;
	}
}

void mavlink_accel_calibration_handler(void)
{
	bool front_finished = false;
	bool back_finished = false;
	bool left_finished = false;
	bool right_finished = false;
	bool up_finished = false;
	bool down_finished = false;

	volatile float calib_x_p, calib_x_n;
	volatile float calib_y_p, calib_y_n;
	volatile float calib_z_p, calib_z_n;

	float accel[3] = {0.0f};

	/* trigger qgroundcontrol to show the calibration window */
	send_mavlink_calibration_status_text("[cal] calibration started: 2 accel");

	float curr_time;
	float last_time = get_sys_time_s();

	while(1) {
		/* read sensor data */
		get_imu_filtered_accel(accel);

		/* read current time */
		curr_time = get_sys_time_s();

		/* detect if imu is motionless */
		if(detect_accel_motion(accel) == true) {
			last_time = get_sys_time_s();
		}

		/* collect calibration data if imu is motionless over 5 seconds */
		if((curr_time - last_time) < 5.0f) {
			continue;
		}

		/* update timer */
		last_time = curr_time;

		/* detect imu orientation */
		int orientation = detect_accel_orientation(accel);

		/* collect calibration data */
		switch(orientation) {
		case ACCEL_CALIB_FRONT: {
			if(front_finished == true) break;

			send_mavlink_calibration_status_text("[cal] front orientation detected");
			calib_x_n = capture_accel_gavity_vaule_x(true);
			send_mavlink_calibration_status_text("[cal] front side done, rotate to a different side");

			front_finished = true;
			break;
		}
		case ACCEL_CALIB_BACK: {
			if(back_finished == true) break;

			send_mavlink_calibration_status_text("[cal] back orientation detected");
			calib_x_p = capture_accel_gavity_vaule_x(false);
			send_mavlink_calibration_status_text("[cal] back side done, rotate to a different side");

			back_finished = true;
			break;
		}
		case ACCEL_CALIB_UP: {
			if(up_finished == true) break;

			send_mavlink_calibration_status_text("[cal] up orientation detected");
			calib_z_p = capture_accel_gavity_vaule_z(false);
			send_mavlink_calibration_status_text("[cal] up side done, rotate to a different side");

			up_finished = true;
			break;
		}
		case ACCEL_CALIB_DOWN: {
			if(down_finished == true) break;

			send_mavlink_calibration_status_text("[cal] down orientation detected");
			calib_z_n = capture_accel_gavity_vaule_z(true);
			send_mavlink_calibration_status_text("[cal] down side done, rotate to a different side");

			down_finished = true;
			break;
		}
		case ACCEL_CALIB_LEFT: {
			if(left_finished == true) break;

			send_mavlink_calibration_status_text("[cal] left orientation detected");
			calib_y_p = capture_accel_gavity_vaule_y(false);
			send_mavlink_calibration_status_text("[cal] left side done, rotate to a different side");

			left_finished = true;
			break;
		}
		case ACCEL_CALIB_RIGHT: {
			if(right_finished == true) break;

			send_mavlink_calibration_status_text("[cal] right orientation detected");
			calib_y_n = capture_accel_gavity_vaule_y(true);
			send_mavlink_calibration_status_text("[cal] right side done, rotate to a different side");

			right_finished = true;
			break;
		}
		default:
			break;
		}

		/* apply calibration result if calibration is finished */
		if(front_finished == true && back_finished == true &&
		    up_finished == true && down_finished == true &&
		    left_finished == true && right_finished == true) {
			send_mavlink_calibration_status_text("[cal] calibration done: accel");
			config_imu_accel_scale_calib_setting(calib_x_p, calib_x_n,
			                                     calib_y_p, calib_y_n,
			                                     calib_z_p, calib_z_n);
			return;
		}
	}
}

void shell_accel_calibration_handler(void)
{
	bool front_finished = false;
	bool back_finished = false;
	bool left_finished = false;
	bool right_finished = false;
	bool up_finished = false;
	bool down_finished = false;

	volatile float calib_x_p, calib_x_n;
	volatile float calib_y_p, calib_y_n;
	volatile float calib_z_p, calib_z_n;

	float accel[3] = {0.0f};

	/* trigger qgroundcontrol to show the calibration window */
	shell_puts("[cal] calibration started: 2 accel\n\r");

	float curr_time;
	float last_time = get_sys_time_s();

	while(1) {
		/* read sensor data */
		get_imu_filtered_accel(accel);

		/* read current time */
		curr_time = get_sys_time_s();

		/* detect if imu is motionless */
		if(detect_accel_motion(accel) == true) {
			last_time = get_sys_time_s();
		}

		/* collect calibration data if imu is motionless over 5 seconds */
		if((curr_time - last_time) < 5.0f) {
			continue;
		}

		/* update timer */
		last_time = curr_time;

		/* detect imu orientation */
		int orientation = detect_accel_orientation(accel);

		/* collect calibration data */
		switch(orientation) {
		case ACCEL_CALIB_FRONT: {
			if(front_finished == true) break;

			shell_puts("[cal] front orientation detected\n\r");
			calib_x_n = capture_accel_gavity_vaule_x(true);
			shell_puts("[cal] front side done, rotate to a different side\n\r");

			front_finished = true;
			break;
		}
		case ACCEL_CALIB_BACK: {
			if(back_finished == true) break;

			shell_puts("[cal] back orientation detected\n\r");
			calib_x_p = capture_accel_gavity_vaule_x(false);
			shell_puts("[cal] back side done, rotate to a different side\n\r");

			back_finished = true;
			break;
		}
		case ACCEL_CALIB_UP: {
			if(up_finished == true) break;

			shell_puts("[cal] up orientation detected\n\r");
			calib_z_p = capture_accel_gavity_vaule_z(false);
			shell_puts("[cal] up side done, rotate to a different side\n\r");

			up_finished = true;
			break;
		}
		case ACCEL_CALIB_DOWN: {
			if(down_finished == true) break;

			shell_puts("[cal] down orientation detected\n\r");
			calib_z_n = capture_accel_gavity_vaule_z(true);
			shell_puts("[cal] down side done, rotate to a different side\n\r");

			down_finished = true;
			break;
		}
		case ACCEL_CALIB_LEFT: {
			if(left_finished == true) break;

			shell_puts("[cal] left orientation detected\n\r");
			calib_y_p = capture_accel_gavity_vaule_y(false);
			shell_puts("[cal] left side done, rotate to a different side\n\r");

			left_finished = true;
			break;
		}
		case ACCEL_CALIB_RIGHT: {
			if(right_finished == true) break;

			shell_puts("[cal] right orientation detected\n\r");
			calib_y_n = capture_accel_gavity_vaule_y(true);
			shell_puts("[cal] right side done, rotate to a different side\n\r");

			right_finished = true;
			break;
		}
		default:
			break;
		}

		/* apply calibration result if calibration is finished */
		if(front_finished == true && back_finished == true &&
		    up_finished == true && down_finished == true &&
		    left_finished == true && right_finished == true) {
			char s[300] = {0};
			sprintf(s, "[cal] calibration done: accel\n\r"
			        "x max:%.2f, min:%f\n\r"
			        "y max:%.2f, min:%f\n\r"
			        "z max:%.2f, min:%f\n\r",
			        calib_x_p, calib_x_n,
			        calib_y_p, calib_y_n,
			        calib_z_p, calib_z_n);

			shell_puts(s);
			config_imu_accel_scale_calib_setting(calib_x_p, calib_x_n,
			                                     calib_y_p, calib_y_n,
			                                     calib_z_p, calib_z_n);
			return;
		}
	}
}