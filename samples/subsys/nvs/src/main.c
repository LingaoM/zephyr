/*
 * NVS Sample for Zephyr using high level API, the sample illustrates the usage
 * of NVS for storing data of different kind (strings, binary blobs, unsigned
 * 32 bit integer) and also how to read them back from flash. The reading of
 * data is illustrated for both a basic read (latest added value) as well as
 * reading back the history of data (previously added values). Next to reading
 * and writing data it also shows how data can be deleted from flash.
 *
 * The sample stores the following items:
 * 1. A string representing an IP-address: stored at id=1, data="192.168.1.1"
 * 2. A binary blob representing a key: stored at id=2, data=FF FE FD FC FB FA
 *    F9 F8
 * 3. A reboot counter (32bit): stored at id=3, data=reboot_counter
 * 4. A string: stored at id=4, data="DATA" (used to illustrate deletion of
 * items)
 *
 * At first boot the sample checks if the data is available in flash and adds
 * the items if they are not in flash.
 *
 * Every reboot increases the values of the reboot_counter and updates it in
 * flash.
 *
 * At the 10th reboot the string item with id=4 is deleted (or marked for
 * deletion).
 *
 * At the 11th reboot the string item with id=4 can no longer be read with the
 * basic nvs_read() function as it has been deleted. It is possible to read the
 * value with nvs_read_hist()
 *
 * At the 78th reboot the first sector is full and a new sector is taken into
 * use. The data with id=1, id=2 and id=3 is copied to the new sector. As a
 * result of this the history of the reboot_counter will be removed but the
 * latest values of address, key and reboot_counter is kept.
 *
 * Copyright (c) 2018 Laczen
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

static struct nvs_fs fs;

#define NVS_PARTITION		storage_partition
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define ADDRESS_ID 1
#define KEY_ID 2
#define RBT_CNT_ID 3
#define STRING_ID 4
#define LONG_ID 5

static uint8_t raw_data[4096] = { 0x00, };

bool nvs_power_lose_flag;

int main(void)
{
	int rc = 0, cnt = 0, cnt_his = 0;
	char buf[16];
	uint8_t key[8], longarray[128];
	uint32_t reboot_counter = 0U, reboot_counter_his;
	struct flash_pages_info info;

	/* define the nvs file system by settings with:
	 *	sector_size equal to the pagesize,
	 *	3 sectors
	 *	starting at NVS_PARTITION_OFFSET
	 */
	fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		printk("Flash device %s is not ready\n", fs.flash_device->name);
		return 0;
	}
	fs.offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		printk("Unable to get page info, rc=%d\n", rc);
		return 0;
	}
	fs.sector_size = info.size;
	fs.sector_count = 3U;

	rc = nvs_mount(&fs);
	if (rc) {
		printk("Flash Init failed, rc=%d\n", rc);
		return 0;
	}

        rc = nvs_clear(&fs);
	if (rc) {
		printk("Flash clear failed, rc=%d\n", rc);
		return 0;
	}

/*
 * The following write is used to simulate a power-loss scenario at the
 * end of a 4096-byte NVS sector.
 *
 * Sector size: 4096 bytes
 *
 * Memory layout after nvs_mount:
 *
 *   0x0000 ┌────────────────────────────────────────┐
 *          | Erased (unused)                        |
 *          │ Erased (unused)                        |
 *   0x0FF0 ├────────────────────────────────────────┤
 *          │ ATE GC                                 │
 *          ├────────────────────────────────────────┤
 *          │ ATE Close                              │
 *   0x1000 └────────────────────────────────────────┘
 */
	rc = nvs_mount(&fs);
	if (rc) {
		printk("Flash Init failed, rc=%d\n", rc);
		return 0;
	}

/*
 * The following write is used to simulate a power-loss scenario at the
 * end of a 4096-byte NVS sector.
 *
 * Sector size: 4096 bytes
 *
 * Memory layout after nvs_write(&fs, raw_data, 2048):
 *
 *   0x0000 ┌────────────────────────────────────────┐
 *          | Data #1                                |
 *   0x0800 ├────────────────────────────────────────┤
 *          │ Erased (unused)                        │
 *   0x0FE8 ├────────────────────────────────────────┤
 *          │ ATE Data #1                            │
 *   0x0FF0 ├────────────────────────────────────────┤
 *          │ ATE GC                                 │
 *          ├────────────────────────────────────────┤
 *          │ ATE Close                              │
 *   0x1000 └────────────────────────────────────────┘
 */
        rc = nvs_write(&fs, 1, raw_data, 2048);
	if (rc < 0) {
		printk("Flash write failed, rc=%d\n", rc);
		return 0;
	}
        
/*
 * Step 2: Write a second data entry that intentionally consumes
 * almost all remaining data space, leaving room for exactly
 * one additional ATE.
 *
 * Available data space before this write:
 *   data_wra = 0x0800
 *   ate_wra  = 0x0FE8
 *   usable   = 0x0FE8 - 0x0800
 *
 * This write subtracts:
 *   - 8 bytes for the ATE of this data entry
 *   - 8 bytes reserved for the next (delete / close) ATE
 *
 * After this write:
 *
 *   0x0000 ┌────────────────────────────────────────┐
 *          │ Data #1                                │
 *   0x0800 ├────────────────────────────────────────┤
 *          │ Data #2 (fills almost all space)       │
 *   0x0FD8 ├────────────────────────────────────────┤
 *          │ ATE Reserved for delete                │
 *   0x0FE0 ├────────────────────────────────────────┤
 *          │ ATE Data #2                            │
 *   0x0FE8 ├────────────────────────────────────────┤
 *          │ ATE Data #1                            │
 *   0x0FF0 ├────────────────────────────────────────┤
 *          │ ATE GC                                 │
 *          ├────────────────────────────────────────┤
 *          │ ATE Close                              │
 *   0x1000 └────────────────────────────────────────┘
 *
 * The sector now has:
 *   - No space for further data entries
 *   - Only space left for a single ATE
 *
 * This sets up the power-loss scenario where the next ATE write
 * may be interrupted, leaving an invalid ATE and no erased space
 * between data_wra and ate_wra.
 */

	nvs_power_lose_flag = true;

        rc = nvs_write(&fs, 1, raw_data, 0x0FE8 - 0x0800 - 8 - 8);
	if (rc < 0) {
		printk("Flash write failed, rc=%d\n", rc);
		return 0;
	}
}
