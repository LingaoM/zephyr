/* board.c - Generic HW interaction hooks */

/*
 * Copyright (c) 2021 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>
#include "board.h"


int board_init(struct k_work *button_pressed)
{
	return 0;
}

void board_led_set(bool val)
{

}

void board_output_number(bt_mesh_output_action_t action, uint32_t number)
{
}

void board_prov_complete(void)
{
}
