#include <stdio.h>
#include <string.h>
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "freertos_config.h"
#include "delay.h"
#include "gpio.h"
#include "uart.h"
#include "spi.h"
#include "timer.h"
#include "pwm.h"
#include "exti.h"
#include "mpu6500.h"
#include "sbus_radio.h"
#include "optitrack.h"
#include "sys_time.h"
#include "motor.h"
#include "debug_link.h"
#include "multirotor_pid_ctrl.h"
#include "fc_task.h"
#include "shell_task.h"
#include "proj_config.h"
#include "mavlink_task.h"
#include "debug_link_task.h"
#include "perf.h"
#include "perf_list.h"
#include "sw_i2c.h"
#include "compass_task.h"
#include "crc.h"

extern SemaphoreHandle_t flight_ctl_semphr;

perf_t perf_list[] = {
	DEF_PERF(PERF_AHRS, "ahrs")
	DEF_PERF(PERF_CONTROLLER, "controller")
	DEF_PERF(PERF_FLIGHT_CONTROL_LOOP, "flight control loop")
	DEF_PERF(PERF_FLIGHT_CONTROL_TRIGGER_TIME, "flight control trigger time")
};

int main(void)
{
	perf_init(perf_list, SIZE_OF_PERF_LIST(perf_list));

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	/* freertos initialization */
	flight_ctl_semphr = xSemaphoreCreateBinary();

	optitrack_init(UAV_ID); //setup tracker id for this MAV

	/* driver initialization */
	crc_init();
	led_init();
	ext_switch_init();
	uart1_init(115200);
	uart3_init(115200); //telem
	uart4_init(100000); //s-bus
	uart6_init(115200);
	uart7_init(115200); //gps or optitrack
	timer12_init(); //system timer and flight controller timer
	pwm_timer1_init(); //motor
	pwm_timer4_init(); //motor
	exti10_init(); //imu ext interrupt
	spi1_init(); //imu
	spi3_init(); //barometer
	//sw_i2c_init(); //XXX

	blocked_delay_ms(1000);

	mavlink_queue_init();

	//xTaskCreate(task_compass, "compass handler", 512, NULL, tskIDLE_PRIORITY + 4, NULL); //XXX

	xTaskCreate(task_flight_ctrl, "flight control", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);

#if (SELECT_TELEM == TELEM_DEBUG_LINK)
	xTaskCreate(task_debug_link, "debug link", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
#elif (SELECT_TELEM == TELEM_MAVLINK)
	xTaskCreate(mavlink_tx_task, "mavlink publisher", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
	xTaskCreate(mavlink_rx_task, "mavlink receiver", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
#elif (SELECT_TELEM == TELEM_SHELL)
	xTaskCreate(shell_task, "shell", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif

	/* start freertos scheduler */
	vTaskStartScheduler();

	return 0;
}
