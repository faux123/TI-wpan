/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2007-2008  Texas Instruments, Inc.
 *  Copyright (C) 2005-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef BLUETI_ENHANCEMENT
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "ant_enabler.h"
#include <cutils/log.h>
#include <fcntl.h>
#include <syslog.h>

#define HCIATTACH_DEBUG

#ifdef HCIATTACH_DEBUG
#define DPRINTF(fmt, x...) printf(fmt, ## x)
#else
#define DPRINTF(fmt, x...)
#endif

#define LOGE(fmt, x...) DPRINTF("%s:%d " fmt "\n",__FUNCTION__,__LINE__, ## x)

#define HCIUARTGETDEVICE	_IOR('U', 202, int)

#define MAKEWORD(a, b)  ((uint16_t)(((uint8_t)(a)) | ((uint16_t)((uint8_t)(b))) << 8))

#define TI_MANUFACTURER_ID	13

#define FIRMWARE_DIRECTORY	"/system/etc/firmware/"

#define ACTION_SEND_COMMAND	1
#define ACTION_WAIT_EVENT	2
#define ACTION_SERIAL		3
#define ACTION_DELAY		4
#define ACTION_RUN_SCRIPT	5
#define ACTION_REMARKS		6

#define BRF_DEEP_SLEEP_OPCODE_BYTE_1	0x0c
#define BRF_DEEP_SLEEP_OPCODE_BYTE_2	0xfd
#define BRF_DEEP_SLEEP_OPCODE		\
        (BRF_DEEP_SLEEP_OPCODE_BYTE_1 | (BRF_DEEP_SLEEP_OPCODE_BYTE_2 << 8))

#define FILE_HEADER_MAGIC	0x42535442

/*
 * BRF Firmware header
 */
struct bts_header {
        uint32_t	magic;
        uint32_t	version;
        uint8_t	future[24];
        uint8_t	actions[0];
}__attribute__ ((packed));

/*
 * BRF Actions structure
 */
struct bts_action {
        uint16_t	type;
        uint16_t	size;
        uint8_t	data[0];
} __attribute__ ((packed));

struct bts_action_send {
        uint8_t data[0];
} __attribute__ ((packed));

struct bts_action_wait {
        uint32_t msec;
        uint32_t size;
        uint8_t data[0];
}__attribute__ ((packed));

struct bts_action_delay {
        uint32_t msec;
}__attribute__ ((packed));

struct bts_action_serial {
        uint32_t baud;
        uint32_t flow_control;
}__attribute__ ((packed));

static FILE *bts_load_script(const char* file_name, uint32_t* version)
{
        struct bts_header header;
        FILE* fp;

        fp = fopen(file_name, "rb");
        if (!fp) {
                perror("can't open firmware file");
                return NULL;
        }

        if (1 != fread(&header, sizeof(struct bts_header), 1, fp)) {
                perror("can't read firmware file");
                goto errclose;
        }

        if (header.magic != FILE_HEADER_MAGIC) {
                fprintf(stderr, "%s not a legal TI firmware file\n", file_name);
                goto errclose;
        }

        if (NULL != version)
                *version = header.version;

        return fp;

errclose:
        fclose(fp);

        return NULL;
}

static unsigned long bts_fetch_action(FILE* fp, unsigned char* action_buf,
                unsigned long buf_size, uint16_t* action_type)
{
        struct bts_action action_hdr;
        unsigned long nread;

        if (!fp)
                return 0;

        if (1 != fread(&action_hdr, sizeof(struct bts_action), 1, fp))
                return 0;

        if (action_hdr.size > buf_size) {
                fprintf(stderr, "bts_next_action: not enough space to read next action\n");
                return 0;
        }

        nread = fread(action_buf, sizeof(uint8_t), action_hdr.size, fp);
        if (nread != (action_hdr.size)) {
                fprintf(stderr, "bts_next_action: fread failed to read next action\n");
                return 0;
        }

        *action_type = action_hdr.type;

        return nread * sizeof(uint8_t);
}

static void bts_unload_script(FILE* fp)
{
        if (fp)
                fclose(fp);
}

static void brf_delay(struct bts_action_delay *delay)
{
        usleep(1000 * delay->msec);
}

static int brf_send_command_socket(int fd, struct bts_action_send* send_action)
{
        char response[1024] = {0};
        hci_command_hdr *cmd = (hci_command_hdr *) send_action->data;
        uint16_t opcode = cmd->opcode;

        struct hci_request rq;
        memset(&rq, 0, sizeof(rq));
        rq.ogf    = cmd_opcode_ogf(opcode);
        rq.ocf    = cmd_opcode_ocf(opcode);
        rq.event  = EVT_CMD_COMPLETE;
        rq.cparam = &send_action->data[3];
        rq.clen   = send_action->data[2];
        rq.rparam = response;
        rq.rlen   = sizeof(response);

        if (hci_send_req(fd, &rq, 1000) < 0) { //sundeep
                perror("Cannot send hci command to socket");
                return -1;
        }

        /* verify success */
        if (response[0]) {
                errno = EIO;
                return -1;
        }

        return 0;
}

static int brf_send_command_file(int fd, struct bts_action_send* send_action, long size)
{
        unsigned char response[1024] = {0};
        long ret = 0;

        /* send command */
        if (size != write(fd, send_action, size)) {
                perror("Texas: Failed to write action command");
                return -1;
        }

        /* read response */
        ret = read_hci_event(fd, response, sizeof(response));
        if (ret < 0) {
                perror("texas: failed to read command response");
                return -1;
        }

        /* verify success */
        if (ret < 7 || 0 != response[6]) {
                fprintf( stderr, "TI init command failed.\n" );
                errno = EIO;
                return -1;
        }

        return 0;
}


static int brf_send_command(int fd, struct bts_action_send* send_action, long size, int hcill_installed)
{
        int ret = 0;
        char *fixed_action;
        hcill_installed =1;

        /* remove packet type when giving to socket API */
        if (hcill_installed) {
                fixed_action = ((char *) send_action) + 1;
                ret = brf_send_command_socket(fd, (struct bts_action_send *) fixed_action);
        } else {
                ret = brf_send_command_file(fd, send_action, size);
        }

        return ret;
}

static int brf_do_action(uint16_t brf_type, uint8_t *brf_action, long brf_size,
                int fd, struct termios *ti, int hcill_installed)
{
        int ret = 0;

        switch (brf_type) {
                case ACTION_SEND_COMMAND:
                        //LOGE("W");
                        ret = brf_send_command(fd, (struct bts_action_send*) brf_action, brf_size, hcill_installed);
                        break;
                case ACTION_WAIT_EVENT:
                        //LOGE("R");
                        break;
                case ACTION_SERIAL:
                        //LOGE("S");
                        //ret = brf_set_serial_params((struct bts_action_serial *) brf_action, fd, ti);
                        break;
                case ACTION_DELAY:
                        //LOGE("D");
                        brf_delay((struct bts_action_delay *) brf_action);
                        break;
                case ACTION_REMARKS:
                        //LOGE("C");
                        break;
                default:
                        fprintf(stderr, "brf_init: unknown firmware action type (%d)\n", brf_type);
                        break;
        }

        return ret;
}

/*
 * tests whether a given brf action is a HCI_VS_Sleep_Mode_Configurations cmd
 */
static int brf_action_is_deep_sleep(uint8_t *brf_action, long brf_size,
                uint16_t brf_type)
{
        uint16_t opcode;

        if (brf_type != ACTION_SEND_COMMAND)
                return 0;

        if (brf_size < 3)
                return 0;

        if (brf_action[0] != HCI_COMMAND_PKT)
                return 0;

        /* HCI data is little endian */
        opcode = brf_action[1] | (brf_action[2] << 8);

        if (opcode != BRF_DEEP_SLEEP_OPCODE)
                return 0;

        /* action is deep sleep configuration command ! */
        return 1;
}

/*
 * This function is called twice.
 * The first time it is called, it loads the brf script, and executes its
 * commands until it reaches a deep sleep command (or its end).
 * The second time it is called, it assumes HCILL protocol is set up,
 * and sends rest of brf script via the supplied socket.
 */
static int brf_do_script(int fd, struct termios *ti, const char *bts_file)
{
        int ret = 0,  hcill_installed = bts_file ? 0 : 1;
        uint32_t vers;
        static FILE *brf_script_file = NULL;
        static uint8_t brf_action[512];
        static long brf_size;
        static uint16_t brf_type;

        /* is it the first time we are called ? */
        if (0 == hcill_installed) {
                LOGE("Sending script to serial device\n");
                brf_script_file = bts_load_script(bts_file, &vers );
                if (!brf_script_file) {
                        fprintf(stderr, "Warning: cannot find BTS file: %s\n",
                                        bts_file);
                        return 0;
                }

                fprintf( stderr, "Loaded BTS script version %u\n", vers );

                brf_size = bts_fetch_action(brf_script_file, brf_action,
                                sizeof(brf_action), &brf_type);
                if (brf_size == 0) {
                        fprintf(stderr, "Warning: BTS file is empty !");
                        return 0;
                }
        }
        else {
                LOGE("Sending script to bluetooth socket\n");
        }

        /* execute current action and continue to parse brf script file */
        while (brf_size != 0) {
                ret = brf_do_action(brf_type, brf_action, brf_size,
                                fd, ti, hcill_installed);
                if (ret == -1)
                        break;

                brf_size = bts_fetch_action(brf_script_file, brf_action,
                                sizeof(brf_action), &brf_type);

                /* if this is the first time we run (no HCILL yet) */
                /* and a deep sleep command is encountered */
                /* we exit */
                if (!hcill_installed &&
                                brf_action_is_deep_sleep(brf_action,
                                        brf_size, brf_type))
                        return 0;
        }

        bts_unload_script(brf_script_file);
        brf_script_file = NULL;
        LOGE("");

        return ret;
}

int read_hci_event(int fd, unsigned char* buf, int size)
{
        int remain, r;
        int count = 0;
        LOGE( "ant_enabler: HCI device read_hci_event  size %d\n", size);
        if (size <= 0)
                return -1;
        while (1) {
                LOGE( "ant_enabler: HCI device read_hci_event--\n");
                r = read(fd, buf, 1);
                LOGE( "ant_enabler: HCI device read_hci_event  read r %d\n", r);
                if (r <= 0)
                        return -1;
                if (buf[0] == 0x04)
                        break;
        }
        count++;
        while (count < 3) {
                r = read(fd, buf + count, 3 - count);
                LOGE( "ant_enabler: HCI device read_hci_event  read second r %d\n", r);
                if (r <= 0)
                        return -1;
                count += r;
        }
        if (buf[2] < (size - 3))
                remain = buf[2];
        else
                remain = size - 3;
        while ((count - 3) < remain) {
                r = read(fd, buf + count, remain - (count - 3));
                LOGE( "ant_enabler: HCI device read_hci_event read  3 r %d\n", r);
                if (r <= 0)
                        return -1;
                count += r;
        }
        LOGE( "ant_enabler: HCI device read_hci_event  read count %d\n", count);
        return count;
}
int script_download_init(int ant_enable)
{
        unsigned char resp[100];		/* Response */
        int n = 0, dd;
        int dev_id = 0;
        struct hci_version ver;
        uint16_t version = 0, chip = 0, min_ver = 0, maj_ver = 0;
        char ant_fw_path[256];
        char ble_fw_path[256];

        memset(resp,'\0', 100);

        dd = hci_open_dev(dev_id);
        if (dd < 0) {
                LOGE("ant_enabler: Can't open device hci%d: %s (%d)\n",
                                dev_id, strerror(errno), errno);
                return -1;
        }
        LOGE( "ant_enabler: HCI device open successfull\n");

        if (hci_read_local_version(dd, &ver, 1000) < 0) {
                fprintf(stderr, "Can't read version info for hci%d: %s (%d)\n",
                                dev_id, strerror(errno), errno);
                hci_close_dev(dd);
                return -1;
        }
        LOGE( "ant_enabler: HCI device read_local_version successfull\n");
        LOGE("ant_enabler: Manufacturer:   %s (%d)\n",
                        bt_compidtostr(ver.manufacturer), ver.manufacturer);

        LOGE("ant_enabler: hci_rev:   %d, hci_ver= %d, lmp_ver=%d, lmp_subver=%d\n",
                        ver.hci_rev, ver.hci_ver, ver.lmp_ver, ver.lmp_subver);

        //version = MAKEWORD(ver.lmp_ver, ver.lmp_subver);
        version = ver.lmp_subver;

        LOGE("ant_enabler: version 0x%04x",version);

        chip =  (version & 0x7C00) >> 10;
        min_ver = (version & 0x007F);
        maj_ver = (version & 0x0380) >> 7;

        if (version & 0x8000)
                maj_ver |= 0x0008;

        LOGE("ant_enabler: chip:   %d, min_ver= %d, maj_ver=%d\n",
                        chip, min_ver, maj_ver);


        memset(ant_fw_path,0,sizeof(ant_fw_path));
        sprintf(ant_fw_path,"/system/etc/firmware/ANT_%d.%d.bts",chip,maj_ver);
        LOGE("ant_enabler: ant firmware path: %s",ant_fw_path);

        memset(ble_fw_path,0,sizeof(ble_fw_path));
        sprintf(ble_fw_path,"/system/etc/firmware/BLE_%d.%d.bts",chip,maj_ver);
        LOGE("ant_enabler: ble firmware path: %s",ble_fw_path);

        switch(maj_ver) {
                case 2:
                        switch(chip){
                                case 6:
                                case 7:
                                        if (ant_enable) {
                                                LOGE("ant_enabler: downloading ANT!\n");
                                                n = brf_do_script(dd, NULL,ant_fw_path);
                                        }else {
                                                n = 0;
                                        }
                                        break;
                                default:
                                        LOGE("ant_enabler: chip is not 6 or 7 Default!\n");
                                        break;
                        }
                        break;
                case 6:
                        switch(chip){
                                case 6:
                                case 7:
                                case 8:
                                case 10:
                                        if (ant_enable) {
                                                n = brf_do_script(dd, NULL, ant_fw_path);
                                                LOGE("ant_enabler: Downloaded ANT: %d!\n", n);
                                        } else {
                                                n = brf_do_script(dd, NULL, ble_fw_path);
                                                LOGE("ant_enabler: Downloaded BLE: %d\n", n);

                                        }
                                        break;
                                default:
                                        LOGE("ant_enabler: chip is not 6,7 or 10 Default!\n");
                                        break;
                        }
                        break;
                default:
                        LOGE("ant_enabler: min_ver is not 2 or 6 Default!\n");
                        break;
        }

        hci_close_dev(dd);

        return n;
}


int download_ant_firmware(int ant_enable)
{
        LOGE( "ant_enabler: inside download_ant_firmware\n");
        return script_download_init(ant_enable);
}

int main(int argc, char** argv)
{
        LOGE( "ant_enabler: main argc:%d",argc);
        if (argc != 2 ) {
                LOGE( "no arguments");
                return -1;
        }

        LOGE( "argv[1]:%s",argv[1]);

        if(strncmp("ble",argv[1],3) == 0) {
                LOGE( "enabling ble");
                return download_ant_firmware(0);
        } else {
                LOGE( "enabling ant");
                return download_ant_firmware(1);
        }
        return 0;
}

#endif
