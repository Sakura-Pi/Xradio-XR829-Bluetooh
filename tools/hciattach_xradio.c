/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2018 Xradio Technology Co., Ltd.. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <signal.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"

#include "hciattach.h"

#ifndef BT_FW_PATH_NAME
#define BT_FW_PATH_NAME "/lib/firmware/fw_xr829_bt.bin"
#endif

#ifndef XR_BT_CONF_PATH_NAME
#define XR_BT_CONF_PATH_NAME "/etc/bluetooth/xr_bt.conf"
#endif

#define UNUSED(x)	(void)(x)

#define SHOW_LOG		0
#define SAVE_TXRX_DATA	0

#define SWAP16(d) (((d & 0xff) << 8) | ((d & 0xff00) >> 8))
#define SWAP32(d) (((d & 0xff) << 24) | ((d & 0xff00) << 8)  \
                | ((d & 0xff0000) >> 8) | ((d & 0xff000000) >> 24))

#define FILL_HEADER_MAGIC(h) do { \
    (h)->magic[0] = 'B'; \
    (h)->magic[1] = 'R'; \
    (h)->magic[2] = 'O'; \
    (h)->magic[3] = 'M'; \
} while(0)

#define HEADER_MAGIC_VALID(h) ( \
    (h)->magic[0] == 'B' && \
    (h)->magic[1] == 'R' && \
    (h)->magic[2] == 'O' && \
    (h)->magic[3] == 'M' )

#define FILL_HEADER_CHECKSUM(h, cs) do { \
    (h)->checksum = cs; \
    } while(0)

#define NO_SYNC_MAX_CNT 0xF
#define UART_BUF_RXCLEAR	(1<<0)
#define	UART_BUF_TXCLEAR	(1<<1)

#define SZ_512		(0x00000200U    )
#define SZ_1K		(0x00000400U    )
#define SZ_2K		(0x00000800U    )
#define SZ_4K		(0x00001000U    )
#define SZ_8K		(0x00002000U    )
#define SZ_16K		(0x00004000U    )
#define SZ_32K		(0x00008000U    )
#define SZ_64K		(0x00010000U    )
#define SZ_128K		(0x00020000U    )
#define SZ_256K		(0x00040000U    )
#define SZ_512K		(0x00080000U    )
#define SZ_1M		(0x00100000U    )
#define SZ_2M		(0x00200000U    )
#define SZ_4M		(0x00400000U    )
#define SZ_8M		(0x00800000U    )
#define SZ_16M		(0x01000000U    )
#define SZ_32M		(0x02000000U    )
#define SZ_64M		(0x04000000U    )
#define SZ_128M		(0x08000000U    )
#define SZ_256M		(0x10000000U    )
#define SZ_512M		(0x20000000U    )
#define SZ_1G		(0x40000000U    )
#define SZ_2G		(0x80000000U    )
#define SZ_4G		(0x0100000000ULL)
#define SZ_8G		(0x0200000000ULL)
#define SZ_16G		(0x0400000000ULL)
#define SZ_32G		(0x0800000000ULL)
#define SZ_64G		(0x1000000000ULL)

#define CMD_REVESION		0x0000 /* 0.0.0.0 */
#define CMD_SYNC_WORD		0x55
#define CMD_ID(group, key)	(((group) << 3) | (key))

/*----------------------------*/
/*   COMMANDS FORM PC TO MCU  */
/*----------------------------*/
#define CMD_ID_MEMRW	0x00
#define CMD_ID_SEQRQ	0x01
#define CMD_ID_SYSCTL	0x02
#define CMD_ID_FLASH	0x03
/* memory access commands */
#define CMD_ID_READ1	CMD_ID(CMD_ID_MEMRW, 0)
#define CMD_ID_WRITE1	CMD_ID(CMD_ID_MEMRW, 1)
#define CMD_ID_READ2	CMD_ID(CMD_ID_MEMRW, 2)
#define CMD_ID_WRITE2	CMD_ID(CMD_ID_MEMRW, 3)
#define CMD_ID_READ4	CMD_ID(CMD_ID_MEMRW, 4)
#define CMD_ID_WRITE4	CMD_ID(CMD_ID_MEMRW, 5)
#define CMD_ID_READ8	CMD_ID(CMD_ID_MEMRW, 6)
#define CMD_ID_WRITE8	CMD_ID(CMD_ID_MEMRW, 7)

#define CMD_ID_SEQRD	CMD_ID(CMD_ID_SEQRQ, 0)
#define CMD_ID_SEQWR	CMD_ID(CMD_ID_SEQRQ, 1)
/* uart commands */
#define CMD_ID_SETUART	CMD_ID(CMD_ID_SYSCTL, 0)
#define CMD_ID_SETJTAG	CMD_ID(CMD_ID_SYSCTL, 1)
#define CMD_ID_REBOOT	CMD_ID(CMD_ID_SYSCTL, 2)
#define CMD_ID_SETPC	CMD_ID(CMD_ID_SYSCTL, 3)
#define CMD_ID_SETCKCS	CMD_ID(CMD_ID_SYSCTL, 4)
/* flash operation commands */
#define CMD_ID_FLASH_GETINFO	CMD_ID(CMD_ID_FLASH, 0)
#define CMD_ID_FLASH_ERASE		CMD_ID(CMD_ID_FLASH, 1)
#define CMD_ID_FLASH_READ		CMD_ID(CMD_ID_FLASH, 2)
#define CMD_ID_FLASH_WRITE		CMD_ID(CMD_ID_FLASH, 3)

/*----------------------------*/
/*   COMMANDS FORM MCU TO PC  */
/*----------------------------*/
/* message output */
#define CMD_ID_SENDMSG	CMD_ID(0, 0)

#pragma pack(1)
/* command header
 *
 *    byte 0    byte 1    byte 2   byte 3     byte 4    byte 5    byte 6 -7          byte 8-11
 *  ___________________________________________________________________________________________________
 * |         |         |         |         |         |         |                   |                   |
 * |   'B'   |   'R'   |   'O'   |   'M'   |  Flags  |Reserved | Checksum          | Playload Length   |
 * |_________|_________|_________|_________|_________|_________|__________ ________|___________________|
 */
typedef struct {
    unsigned char magic[4]; /* magic "BROM" */
        #define CMD_BROM_MAGIC "BROM"
    unsigned char flags;
        #define CMD_HFLAG_ERROR   (0x1U << 0)
        #define CMD_HFLAG_ACK     (0x1U << 1)
        #define CMD_HFLAG_CHECK   (0x1U << 2)
        #define CMD_HFLAG_RETRY   (0x1U << 3)
        #define CMD_HFLAG_EXE     (0x1U << 4)
    unsigned char version:4;
    unsigned char reserved:4;
    unsigned short checksum;
    unsigned int payload_len;
} __attribute__((packed)) cmd_header_t;
#define MB_CMD_HEADER_SIZE (sizeof(cmd_header_t))

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
} __attribute__((packed)) cmd_header_id_t;
#define MB_HEADER_TO_ID(h) (((cmd_header_id_t*)(h))->cmdid)

/* acknownledge structure */
typedef struct {
    cmd_header_t h;
    unsigned char err;
} __attribute__((packed)) cmd_ack_t;

/* memory read/write command structure */
#define CMD_RW_DATA_POS (MB_CMD_HEADER_SIZE + 5)
#define CMD_RW_DATA_LEN(id) (1 << ((id >> 1) & 0x3))
typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int addr;
} __attribute__((packed)) cmd_rw_t;

/* sequence read/write command structure */
#define CMD_SEQRW_DATA_POS (MB_CMD_HEADER_SIZE + 5)
typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int addr;
    unsigned int dlen;
    unsigned short dcs;
} __attribute__((packed)) cmd_seq_wr_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int addr;
    unsigned int dlen;
} __attribute__((packed)) cmd_seq_rd_t;

/* io change command structure */
#define CMD_SYS_DATA_POS (MB_CMD_HEADER_SIZE + 1)
typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int val;
} __attribute__((packed)) cmd_sys_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int lcr;
} __attribute__((packed)) cmd_sys_setuart_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned char mode;
} __attribute__((packed)) cmd_sys_setjtag_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned char mode;
} __attribute__((packed)) cmd_sys_setcs_t;

/* flash command structure */
typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned char erase_cmd;
    unsigned int addr;
} __attribute__((packed)) cmd_flash_er_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int sector;
    unsigned int num;
    unsigned short dcs;
} __attribute__((packed)) cmd_flash_wr_t;

typedef struct {
    cmd_header_t h;
    unsigned char cmdid;
    unsigned int sector;
    unsigned int num;
} __attribute__((packed)) cmd_flash_rd_t;

#pragma pack()

/* response error type */
#define MB_ERR_UNKNOWNCMD	(1)
#define MB_ERR_TIMEOUT		(2)
#define MB_ERR_CHECKSUM		(3)
#define MB_ERR_INVALID		(4)
#define MB_ERR_NOMEM		(5)

#define UPIO_BT_POWER_OFF	0
#define UPIO_BT_POWER_ON	1

/* UPIO signals */
enum {
	UPIO_BT_WAKE = 0,
	UPIO_HOST_WAKE,
	UPIO_LPM_MODE,
	UPIO_MAX_COUNT
};

/* UPIO assertion/deassertion */
enum {
	UPIO_UNKNOWN = 0,
	UPIO_DEASSERT,
	UPIO_ASSERT
};

#define BT_WAKE_VIA_PROC	1

/* BT_WAKE Polarity - 0=Active Low, 1= Active High */
#ifndef LPM_BT_WAKE_POLARITY
#define LPM_BT_WAKE_POLARITY            1    /* maguro */
#endif

/* BT_WAKE Lock - 2=LOCK, 3= UNLOCK */
#ifndef LPM_BT_WAKE_LOCK
#define LPM_BT_WAKE_LOCK            2
#endif

#ifndef LPM_BT_WAKE_UNLOCK
#define LPM_BT_WAKE_UNLOCK          3
#endif

/* proc fs node for enable/disable lpm mode */
#ifndef VENDOR_LPM_PROC_NODE
#define VENDOR_LPM_PROC_NODE	"/proc/bluetooth/sleep/lpm"
#endif

/* proc fs node for sleep bt device */
#ifndef VENDOR_BTWRITE_PROC_NODE
#define VENDOR_BTWRITE_PROC_NODE	"/proc/bluetooth/sleep/btwrite"
#endif

#ifndef VENDOR_BTWAKE_PROC_NODE
#define VENDOR_BTWAKE_PROC_NODE "/proc/bluetooth/sleep/btwake"
#endif

/*
 * Maximum btwrite assertion holding time without consecutive btwrite kicking.
 * This value is correlative(shorter) to the in-activity timeout period set in
 * the bluesleep LPM code. The current value used in bluesleep is 10sec.
 */
#ifndef PROC_BTWAKE_TIMER_TIMEOUT_MS
#define PROC_BTWAKE_TIMER_TIMEOUT_MS	10000
#endif

/* lpm proc control block */
typedef struct
{
	uint8_t  btwrite_active;
	uint8_t  timer_created;
	timer_t  timer_id;
	uint32_t timeout_ms;
} vnd_lpm_proc_cb_t;

static unsigned char upio_state[UPIO_MAX_COUNT];
static int rfkill_id = -1;
static char *rfkill_state_path = NULL;
static vnd_lpm_proc_cb_t lpm_proc_cb;

/* for friendly debugging outpout string */
static char *lpm_mode[] = {
	"UNKNOWN",
	"disabled",
	"enabled"
};

static char *lpm_state[] = {
	"UNKNOWN",
	"de-asserted",
	"asserted"
};

static unsigned int g_chip_name = 1;			/* 1:AW1722	2:AW1732 */
static unsigned int g_load_addr = 0x0000;
static unsigned int g_jump_addr = 0x0000;
static unsigned int g_port_num = 1;
static unsigned int g_dump_debug = 1;
static unsigned int g_hciup_flag = 1;
static unsigned int g_startup_reset_flag =1;
static unsigned int g_update_hcirate_flag = 1;
static unsigned int g_bdaddr_flag = 1;

static unsigned char g_trans_buf[1024];

static unsigned char hci_reset[] = { 0x01, 0x03, 0x0c, 0x00 };
static unsigned char hci_update_baud_rate[] = { 0x01, 0x18, 0xfc, 0x04, 0x60, 0xE3, 0x16, 0x00};
static unsigned char hci_write_bd_addr[] = { 0x01, 0x0a, 0xfc, 0x09, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void proc_btwrite_timeout(union sigval arg)
{
	UNUSED(arg);
	printf("%s.\n", __FUNCTION__);
	lpm_proc_cb.btwrite_active = 0;
}

static void ms_delay (uint32_t timeout)
{
	struct timespec delay;
	int err;

	if (timeout == 0)
		return;

	delay.tv_sec = timeout / 1000;
	delay.tv_nsec = 1000 * 1000 * (timeout%1000);

	/* [u]sleep can't be used because it uses SIGALRM */
	do {
		err = nanosleep(&delay, &delay);
	} while (err < 0 && errno == EINTR);
}

static unsigned short check_sum16(unsigned char *data, unsigned int len)
{
    unsigned short cs = 0;
    unsigned short *p = (unsigned short*)data;

    while(len > 1) {
        cs += *p++;
        len -= 2;
    }
    if (len) {
        cs += *(unsigned char*)p;
    }

    return cs;
}

static void debug_dump(unsigned char *out, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		if (i && !(i % 16))
		{
			fprintf(stderr, "\n");
		}

		fprintf(stderr, "%02x ", out[i]);
	}

	fprintf(stderr, "\n");
}

static void debug_dump_data(unsigned char *prompt, unsigned char *buf, unsigned int len)
{
#if SHOW_LOG
	unsigned int i, j;
	unsigned int head = 0;

	printf("Dump(%s): len %d\n", prompt, len);
	for (i = 0; i < len;) {
		printf("0x%02x, ", buf[i]&0xff);
		if (i % 8 == 7 || i==len-1) {
			printf("\n");
		}

		i++;
	}
	printf("\n");
#endif
}

static void debug_write_txdata(unsigned char *buf, unsigned int len)
{
#if SAVE_TXRX_DATA
	unsigned int i;
	static unsigned int txidx = 0;
	static unsigned int txlen = 0;
	cmd_rw_t *cmd = (cmd_rw_t *)buf;
	char name[16] = "txdata.hex";
	FILE *fd;

	txidx ++;
	txlen = 0;

	txlen += len;
	sprintf(&name[strlen("txdata.hex")], "%d", txidx);
	fd = fopen(name, "wb+");

    fprintf(fd, "%02x\n", txlen & 0xff);
	fprintf(fd, "%02x\n", (txlen>>8) & 0xff);
	fprintf(fd, "%02x\n", (txlen>>16) & 0xff);
	fprintf(fd, "%02x\n", (txlen>>24) & 0xff);

	for (i=0; i<len; i++)
        fprintf(fd, "%02x\n", buf[i]);
	fclose(fd);
#endif
}

static void debug_write_rxdata(unsigned char *buf, unsigned int len)
{
#if SAVE_TXRX_DATA
	unsigned int i;
	static unsigned int rxidx = 0;
	static unsigned int rxlen = 0;
	cmd_rw_t *cmd = (cmd_rw_t *)buf;
	char name[16] = "rxdata.hex";
	FILE *fd;

	if (HEADER_MAGIC_VALID(&cmd->h)) {
	    rxidx ++;
	    rxlen = 0;
	}
    rxlen += len;
	sprintf(&name[strlen("rxdata.hex")], "%d", rxidx);
	if (HEADER_MAGIC_VALID(&cmd->h)) {
	    fd = fopen(name, "wb+");
        fprintf(fd, "%02x\n", rxlen & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>8) & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>16) & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>24) & 0xff);
	} else {
	    fd = fopen(name, "ab+");
	}
	for (i=0; i<len; i++)
        fprintf(fd, "%02x\n", buf[i]);
    if (!HEADER_MAGIC_VALID(&cmd->h)) {
        fclose(fd);
	    fd = fopen(name, "rb+");
        fseek(fd, 0, SEEK_SET);
        fprintf(fd, "%02x\n", rxlen & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>8) & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>16) & 0xff);
        fprintf(fd, "%02x\n", (rxlen>>24) & 0xff);
    }

	fclose(fd);
#endif
}

static int upio_set_bluetooth_power(int on);

void brom_no_sync_workaround()
{
    static uint32_t no_sync_cnt = 0;
    no_sync_cnt++;
    if(no_sync_cnt <= NO_SYNC_MAX_CNT)
        return ;
	no_sync_cnt = 0;
    if (upio_set_bluetooth_power(UPIO_BT_POWER_OFF))
    {
        printf("bt power off fail\n");
    }
    usleep(500000);
    if (upio_set_bluetooth_power(UPIO_BT_POWER_ON))
    {
        printf("bt power on fail\n");
    }
    usleep(20000);
    printf("workaround for no sync done \n");
}

static int userial_sync(int fd)
{
	unsigned char syncbuf[] = {0x55};
	unsigned char ackbuf[3] = {0};
	int ret = -1;
	int flags;
	int cnt = 0;

	flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		printf("[%s] fcntl NONBLOCK fail.\n", __FUNCTION__);
		exit(-1);
	}


	/*
	 * sync success or stop by alarm.
	 */
	do {
		printf("[%s] uart sync count: %d.\n", __FUNCTION__, ++cnt);
		write(fd, syncbuf, 1);
		usleep(20000);
		ret = read(fd, ackbuf, 2);
		printf("[%s] read buf: %02x %02x.\n", __FUNCTION__, ackbuf[0], ackbuf[1]);
		if (ret < 0) {
            brom_no_sync_workaround();
			if (errno == EAGAIN)
				continue;
		}
		if (ret == 2 && ((ackbuf[0] == 'O' && ackbuf[1] == 'K')
					|| (ackbuf[0] == 'K' && ackbuf[1] == 'O'))) {
			printf("[%s] Receive %s, uart sync done.\n", __FUNCTION__, ackbuf);
			break;
		}
        else
        {
            brom_no_sync_workaround();
        }
	} while (1);

	flags &= ~(O_NONBLOCK);
	if (fcntl(fd, F_SETFL, flags) == -1) {
		printf("[%s] fcntl BLOCK fail.\n", __FUNCTION__);
		exit(-1);
	}

	return 0;
}

static void userial_vendor_set_hw_fctrl(int fd, uint8_t hw_fctrl)
{
	struct termios ti;

	if (fd == -1)
	{
		printf("fd is -1\n");
		return;
	}

	if (tcgetattr(fd, &ti) < 0) {
		perror("Can't get port settings");
		return;
	}

	if(hw_fctrl)
	{
		printf("Set HW FlowControl On\n");
		if(ti.c_cflag & CRTSCTS)
		{
			printf("userial_vendor_set_hw_fctrl already hw flowcontrol on\n");
			return;
		}
		else
		{
			ti.c_cflag |= CRTSCTS;
			tcsetattr(fd, TCSANOW, &ti);
			printf("userial_vendor_set_hw_fctrl set hw flowcontrol on\n");
		}
	}
	else
	{
		printf("Set HW FlowControl Off\n");
		if(ti.c_cflag & CRTSCTS)
		{
			ti.c_cflag &= ~CRTSCTS;
			tcsetattr(fd, TCSANOW, &ti);
			return;
		}
		else
		{
			printf("userial_vendor_set_hw_fctrl set hw flowcontrol off\n");
			return;
		}
	}
	tcflush(fd, TCIOFLUSH);
}

static void userial_clearbuf(int fd, uint8_t flag)
{
	if (flag == UART_BUF_RXCLEAR)
		tcflush(fd, TCIFLUSH);
	else if (flag == UART_BUF_TXCLEAR)
		tcflush(fd, TCOFLUSH);
	else
		tcflush(fd, TCIOFLUSH);
}

static uint16_t userial_read(int fd, uint8_t *p_buffer, uint16_t len)
{
	int ret = -1;
	uint16_t total_len = 0;

	do {
		ret = read(fd, p_buffer + total_len, len);
		if(ret == -1)
		{
			printf("%s error writing to serial port: %s.\n", __func__, strerror(errno));
			return total_len;
		}
		total_len += ret;
		len -= ret;
	} while(len);

	return total_len;
}

static uint16_t userial_write(int fd, const uint8_t *p_data, uint16_t len)
{
    uint16_t total = 0;
    int ret;

    while (len) {
        ret = write(fd, p_data + total, len);
        switch (ret) {
            case -1:
                printf("%s error writing to serial port: %s.\n", __func__, strerror(errno));
                return total;
            case 0:
                return total;
            default:
                total += ret;
                len -= ret;
                break;
        }
    }
    return total;
}

static int userial_vendor_set_baud(int fd, int def_speed)
{
	struct termios ti;

	if (tcgetattr(fd, &ti) < 0) {
		perror("Can't get port settings");
		return -1;
	}

	if (set_speed(fd, &ti, def_speed) < 0) {
		perror("Can't set initial baud rate");
		return -1;
	}

	return 0;
}

static void read_event(int fd, unsigned char *buffer)
{
	int i = 0;
	int len = 3;
	int count;

	while ((count = read(fd, &buffer[i], len)) < len)
	{
		i += count;
		len -= count;
	}

	i += count;
	len = buffer[2];

	while ((count = read(fd, &buffer[i], len)) < len)
	{
		i += count;
		len -= count;
	}

	if (g_dump_debug)
	{
		count += i;

		fprintf(stderr, "received %d\n", count);
		debug_dump(buffer, count);
	}
}

static void xradio_hci_send_cmd(int fd, unsigned char *buf, int len)
{
	if (g_dump_debug)
	{
		fprintf(stderr, "writing\n");
		debug_dump(buf, len);
	}

	write(fd, buf, len);
}

/*
 * buf[0] - buf[6] contains the command header
 * buf[7] - buf[12] contains the bdaddr, LSB - MSB
 *
 * The reserved LAP addresses are 0x9E8B00-0x9E8B3F
 *
 * return 1 if valid, 0 if not
 */
static int check_bdaddr_valid(unsigned char *buf)
{
	int i = 0;
	unsigned char null_mac_addr[6] = {0, 0, 0, 0, 0, 0};
	unsigned char read_mac_addr[6] = {0};
	for(i = 0; i < 6; i++ ) {
		read_mac_addr[i] = buf[i+7];
	}
	if (!memcmp(read_mac_addr, null_mac_addr, 6)) {
		printf("bdaddr is null, not valid\n");
		return 0;
	}
	if (buf[9] == 0x9e && buf[8] == 0x8b
			&& (buf[7] & ~(0x3f)) == 0) {
		printf("bdaddr is not valid\n");
		return 0;
	}

	return 1;
}

/*
 * buf[0] - buf[6] contains the command header
 * buf[7] - buf[12] contains the bdaddr, LSB - MSB
 */
static int xradio_generate_bdaddr(unsigned char *buf)
{
	int fd;
	FILE* conf_fd = NULL;
	unsigned mac_hex;
	int i = 12; /* from MSB to LSB*/

	conf_fd = fopen(XR_BT_CONF_PATH_NAME, "r");
	if(conf_fd) {
		fscanf(conf_fd, "%x", &mac_hex);
		while (!feof(conf_fd)) {
			buf[i--] = mac_hex;
			fscanf(conf_fd, "%x", &mac_hex);
		}

		fclose(conf_fd);

		if (check_bdaddr_valid(buf))
			return 0;
	}

	printf("generating random bdaddr...\n");

	/* for Xradio, NAP is {0x22, 0x22}*/
	buf[12] = 0x22;
	buf[11] = 0x22;

	srand(time(0));

	do {
		/* generating UAP - buf[10] */
		buf[10] = rand() % (0xff + 1);

		/* generating LAP[2] - buf[9] */
		buf[9] = rand() % (0xff + 1);

		/* generating LAP[1] - buf[8] */
		buf[8] = rand() % (0xff + 1);

		/* generating LAP[0] - buf[7] */
		buf[7] = rand() % (0xff + 1);
	} while (!check_bdaddr_valid(buf));

	conf_fd = fopen(XR_BT_CONF_PATH_NAME, "w");
	if (conf_fd) {
		for (i = 12; i >=7; i--)
			fprintf(conf_fd, "%02x ", buf[i]);
		fclose(conf_fd);
	}

	return 0;
}

static void proc_reset(int fd)
{
	/* signal(SIGALRM, expired); */

	xradio_hci_send_cmd(fd, hci_reset, sizeof(hci_reset));
	/* alarm(4); */
	read_event(fd, g_trans_buf);
	/* alarm(0); */
}

static void proc_baudrate(int fd, int speed)
{
	xradio_hci_send_cmd(fd, hci_update_baud_rate, sizeof(hci_update_baud_rate));

	read_event(fd, g_trans_buf);

	userial_vendor_set_baud(fd, speed);
	ms_delay(100);

	if (g_dump_debug)
	{
		fprintf(stderr, "Done setting baudrate\n");
	}
}

static void proc_bdaddr(int fd)
{
	if (xradio_generate_bdaddr(hci_write_bd_addr) < 0) {
		fprintf(stderr, "generate random bdaddr failed, using the default address...\n");
		return;
	}

	xradio_hci_send_cmd(fd, hci_write_bd_addr, sizeof(hci_write_bd_addr));

	read_event(fd, g_trans_buf);
}

static void proc_enable_hci(int fd)
{
	int i = N_HCI;
	int proto = HCI_UART_H4;
	if (ioctl(fd, TIOCSETD, &i) < 0) {
		fprintf(stderr, "Can't set line discipline\n");
		return;
	}

	if (ioctl(fd, HCIUARTSETPROTO, proto) < 0) {
		fprintf(stderr, "Can't set hci protocol\n");
		return;
	}
	fprintf(stderr, "Done setting line discpline\n");
	return;
}

static int init_rfkill(void)
{
	char path[64];
	char buf[16];
	int fd, sz, id;

	for (id = 0; ; id++)
	{
		snprintf(path, sizeof(path), "/sys/class/rfkill/rfkill%d/type", id);
		fd = open(path, O_RDONLY);
		if (fd < 0)
		{
			printf("init_rfkill : open(%s) failed: %s (%d).\n", \
					path, strerror(errno), errno);
			return -1;
		}

		sz = read(fd, &buf, sizeof(buf));
		close(fd);

		if (sz >= 9 && memcmp(buf, "bluetooth", 9) == 0)
		{
			rfkill_id = id;
			break;
		}
	}

	asprintf(&rfkill_state_path, "/sys/class/rfkill/rfkill%d/state", rfkill_id);
	return 0;
}

static int upio_set_bluetooth_power(int on)
{
	int  sz;
	int  fd = -1;
	int  ret = -1;
	char buffer = '0';

	switch(on) {
	case UPIO_BT_POWER_OFF:
		buffer = '0';
		break;
	case UPIO_BT_POWER_ON:
		buffer = '1';
		break;
	}

	fd = open(rfkill_state_path, O_WRONLY);

	if (fd < 0)
	{
		printf("set_bluetooth_power : open(%s) for write failed: %s (%d).\n",
		rfkill_state_path, strerror(errno), errno);
		return ret;
	}

	sz = write(fd, &buffer, 1);

	if (sz < 0)
	{
		printf("set_bluetooth_power : write(%s) failed: %s (%d).\n",
		rfkill_state_path, strerror(errno),errno);
	}
	else
		ret = 0;

	if (fd >= 0)
		close(fd);

	return ret;
}

static int upio_set_btwake(int action)
{
	int fd = -1;
	int ret = -1;
	char buffer = '0';

	if (action == 0) {
		buffer = '0';
	} else if (action == LPM_BT_WAKE_POLARITY) {
		buffer = '1';
	} else if (action == LPM_BT_WAKE_LOCK) {
		buffer = '2';
	} else if (action == LPM_BT_WAKE_UNLOCK) {
		buffer = '3';
	} else {
		printf("Invalid btwake action\n");
		return -1;
	}

	fd = open(VENDOR_BTWAKE_PROC_NODE, O_WRONLY);
	if (fd < 0) {
		printf("upio_set : open(%s) for write failed: %s (%d)",
				VENDOR_BTWAKE_PROC_NODE, strerror(errno), errno);
		return fd;
	}

	if (write(fd, &buffer, 1) < 0) {
		printf("upio_set : write(%s) failed: %s (%d)",
				VENDOR_BTWAKE_PROC_NODE, strerror(errno),errno);
		return -1;
	}

	if (fd >= 0)
		close(fd);

	return 0;
}

static void upio_init(void)
{
	memset(upio_state, UPIO_UNKNOWN, UPIO_MAX_COUNT);
	memset(&lpm_proc_cb, 0, sizeof(vnd_lpm_proc_cb_t));
}

static void upio_cleanup(void)
{
	if (lpm_proc_cb.timer_created == 1)
		//timer_delete(lpm_proc_cb.timer_id);
		;
	lpm_proc_cb.timer_created = 0;
}

static void upio_set(uint8_t pio, uint8_t action, uint8_t polarity)
{
	UNUSED(polarity);
	int rc;
#if (BT_WAKE_VIA_PROC == 1)
	int fd = -1;
	char buffer;
#endif

	switch (pio)
	{
		case UPIO_LPM_MODE:
			printf("set LPM mode:%s", lpm_mode[action]);
			if (upio_state[UPIO_LPM_MODE] == action)
			{
				printf("LPM is %s already", lpm_mode[action]);
				return;
			}
			upio_state[UPIO_LPM_MODE] = action;

#if (BT_WAKE_VIA_PROC == 1)
			fd = open(VENDOR_LPM_PROC_NODE, O_WRONLY);
			if (fd < 0)
			{
				printf("upio_set : open(%s) for write failed: %s (%d)",
						VENDOR_LPM_PROC_NODE, strerror(errno), errno);
				return;
			}

			if (action == UPIO_ASSERT)
			{
				buffer = '1';
			}
			else
			{
				buffer = '0';
				if (lpm_proc_cb.timer_created == 1)
				{
					//timer_delete(lpm_proc_cb.timer_id);
					lpm_proc_cb.timer_created = 0;
				}
			}

			if (write(fd, &buffer, 1) < 0)
			{
				printf("upio_set : write(%s) failed: %s (%d)",
						VENDOR_LPM_PROC_NODE, strerror(errno),errno);
			}
			else
			{
				if (action == UPIO_ASSERT)
				{
					// create btwrite assertion holding timer
					if (lpm_proc_cb.timer_created == 0)
					{
#if 0
						int status;
						struct sigevent se;
						se.sigev_notify = SIGEV_THREAD;
						se.sigev_value.sival_ptr = &lpm_proc_cb.timer_id;
						se.sigev_notify_function = proc_btwrite_timeout;
						se.sigev_notify_attributes = NULL;
						status = timer_create(CLOCK_MONOTONIC, &se,&lpm_proc_cb.timer_id);
						if (status == 0)
							lpm_proc_cb.timer_created = 1;
#endif
					}
				}
			}

			if (fd >= 0)
				close(fd);
#endif
			break;
		case UPIO_BT_WAKE:
			printf("upio_set: UPIO_BT_WAKE");

			if (upio_state[UPIO_BT_WAKE] == action)
			{
				printf("BT_WAKE is %s already", lpm_state[action]);

#if (BT_WAKE_VIA_PROC == 1)
				if (lpm_proc_cb.btwrite_active == 1)
					/*
					 * The proc btwrite node could have not been updated for
					 * certain time already due to heavy downstream path flow.
					 * In this case, we want to explicity touch proc btwrite
					 * node to keep the bt_wake assertion in the LPM kernel
					 * driver. The current kernel bluesleep LPM code starts
					 * a 10sec internal in-activity timeout timer before it
					 * attempts to deassert BT_WAKE line.
					 */
#endif
				return;
			}

			upio_state[UPIO_BT_WAKE] = action;

#if (BT_WAKE_VIA_PROC == 1)

			//	Kick proc btwrite node only at UPIO_ASSERT
			if (action == UPIO_DEASSERT)
			{
				buffer = '0';
				return;
			}
			else
			{
				buffer = '1';
			}

			fd = open(VENDOR_BTWAKE_PROC_NODE, O_WRONLY);

			if (fd < 0)
			{
				printf("upio_set : open(%s) for write failed: %s (%d)",
						VENDOR_BTWAKE_PROC_NODE, strerror(errno), errno);
				return;
			}

			if (write(fd, &buffer, 1) < 0)
			{
				printf("upio_set : write(%s) failed: %s (%d)",
						VENDOR_BTWAKE_PROC_NODE, strerror(errno),errno);
			}
			else
			{
				lpm_proc_cb.btwrite_active = 1;

				if (lpm_proc_cb.timer_created == 1)
				{
#if 0
					struct itimerspec ts;

					ts.it_value.tv_sec = PROC_BTWAKE_TIMER_TIMEOUT_MS/1000;
					ts.it_value.tv_nsec = 1000*(PROC_BTWAKE_TIMER_TIMEOUT_MS%1000);
					ts.it_interval.tv_sec = 0;
					ts.it_interval.tv_nsec = 0;

					timer_settime(lpm_proc_cb.timer_id, 0, &ts, 0);
#endif
				}
			}

			printf("proc btwake assertion");

			if (fd >= 0)
				close(fd);
#endif

			break;
		case UPIO_HOST_WAKE:
			printf("upio_set: UPIO_HOST_WAKE");
			break;
	}
}

static int com_sync_baudrate(int fd, int def_speed, unsigned int lcr);
static int com_stream_write(int fd, unsigned int addr, unsigned int len, unsigned char* data);
static int com_write_byte(int fd, unsigned int addr, unsigned int len, unsigned char* data);
static int com_set_pc(int fd, unsigned int pc);

static int com_sync_baudrate(int fd, int speed, unsigned int lcr)
{
	unsigned int payload_len = 5;	/* cmdid(1) + lcr(1) + baud(3) */
	unsigned char cmdbuff[MB_CMD_HEADER_SIZE + 5] = {0};
	cmd_sys_setuart_t *cmd = (cmd_sys_setuart_t*)cmdbuff;
	cmd_ack_t *ack = (cmd_ack_t *)cmdbuff;

	/* fill header */
	FILL_HEADER_MAGIC(&cmd->h);
	cmd->h.flags = CMD_HFLAG_CHECK;
	cmd->h.version = 0;
	cmd->h.checksum = 0;
	cmd->h.payload_len = payload_len;
	/* fill command id */
	cmd->cmdid = CMD_ID_SETUART;
	cmd->lcr = lcr;
	cmd->h.checksum = ~check_sum16(cmdbuff, MB_CMD_HEADER_SIZE + payload_len);

	/* convert host byte order to network byte order */
	cmd->h.payload_len = SWAP32(cmd->h.payload_len);
	cmd->h.checksum    = SWAP16(cmd->h.checksum);
	cmd->lcr           = SWAP32(cmd->lcr);

	userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
	/* send command */
	userial_write(fd, cmdbuff, MB_CMD_HEADER_SIZE + payload_len);
	memset(cmdbuff, 0, MB_CMD_HEADER_SIZE + 5);
	userial_read(fd, cmdbuff, MB_CMD_HEADER_SIZE);
	/* check header */
	if (!HEADER_MAGIC_VALID(&ack->h)) {
		printf("invalid response\n");
		userial_clearbuf(fd, UART_BUF_RXCLEAR);
		return -1;
	} else if (ack->h.flags & CMD_HFLAG_ERROR) {
		userial_read(fd, cmdbuff + MB_CMD_HEADER_SIZE, 1);
		printf("resp error flag, type %d\n", ack->err);
		return -ack->err;
	} else {
		if (ack->h.flags & CMD_HFLAG_ACK) {
			/* convert network byte order to host byte order */
			ack->h.payload_len = SWAP32(ack->h.payload_len);
			ack->h.checksum    = SWAP16(ack->h.checksum);
			if (ack->h.payload_len != 0) {
				printf("data payload len %d != 0\n", ack->h.payload_len);
				userial_clearbuf(fd, UART_BUF_RXCLEAR);
				return -1;
			}
			if ((ack->h.flags & CMD_HFLAG_CHECK) &&
					(check_sum16(cmdbuff, MB_CMD_HEADER_SIZE)) != 0xffff) {
				printf("Set uart mode response checksum error\n");
				return -1;
			}

			printf("Set uart mode done\n");
			userial_vendor_set_baud(fd, speed);	// low 24 bits
			return userial_sync(fd);
			return 0;
		}
	}

	return 0;
}

static int com_stream_write(int fd, unsigned int addr, unsigned int len, unsigned char *data)
{
	unsigned int payload_len = 11;  /* cmdid(1) + addr(4) + dlen(4) + dcs(2) */
	unsigned char cmdbuff[MB_CMD_HEADER_SIZE + 11] = {0};
	cmd_seq_wr_t *cmd = (cmd_seq_wr_t *)cmdbuff;
	cmd_ack_t *ack = (cmd_ack_t *)cmdbuff;

	/* fill header */
	FILL_HEADER_MAGIC(&cmd->h);
	cmd->h.flags = CMD_HFLAG_CHECK;
	cmd->h.version = 0;
	cmd->h.checksum = 0;
	cmd->h.payload_len = payload_len;
	/* fill command id */
	cmd->cmdid = CMD_ID_SEQWR;
	/* fill command address, data length,
	data checksum and command checksum */
	cmd->addr = addr;
	cmd->dlen = len;
	cmd->dcs = ~check_sum16(data, len);
	cmd->h.checksum = ~check_sum16(cmdbuff, MB_CMD_HEADER_SIZE + payload_len);

	/* convert host byte order to network byte order */
	cmd->h.payload_len = SWAP32(cmd->h.payload_len);
	cmd->addr          = SWAP32(cmd->addr);
	cmd->dlen          = SWAP32(cmd->dlen);
	cmd->dcs           = SWAP16(cmd->dcs);
	cmd->h.checksum    = SWAP16(cmd->h.checksum);

	/* clear rx buffer */
	userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
	/* send cmd */
	userial_write(fd, cmdbuff, MB_CMD_HEADER_SIZE + payload_len);
	userial_read(fd, cmdbuff, MB_CMD_HEADER_SIZE);
	/* check data */
	if (!HEADER_MAGIC_VALID(&ack->h)) {
		printf("invalid response\n");
		userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
		return -1;
	} else if (ack->h.flags & CMD_HFLAG_ERROR) {
		userial_read(fd, cmdbuff + MB_CMD_HEADER_SIZE, 1);
		printf("resp error flag, type %d\n", ack->err);
	} else {
		if (ack->h.flags & CMD_HFLAG_ACK) {
			/* convert network byte order to host byte order */
			ack->h.payload_len = SWAP32(ack->h.payload_len);
			ack->h.checksum    = SWAP16(ack->h.checksum);
			if (ack->h.payload_len != 0) {
				printf("data payload len %d != 0\n", ack->h.payload_len);
				userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
				return -1;
			}
			/* check response */
			if ((ack->h.flags & CMD_HFLAG_CHECK) &&
					(check_sum16(cmdbuff, MB_CMD_HEADER_SIZE) != 0xffff)) {
				printf("write data response 0 checksum error\n");
				return -1;
			}
			/* send data */
			userial_write(fd, data, len);
			userial_read(fd, cmdbuff, MB_CMD_HEADER_SIZE);
			/* check data */
			if (ack->h.flags & CMD_HFLAG_ERROR) {
				userial_read(fd, cmdbuff + MB_CMD_HEADER_SIZE, 1);
				printf("resp error flag, type %d\n", ack->err);
				return -ack->err;
			} else {
				if (ack->h.flags & CMD_HFLAG_ACK) {
					/* convert network byte order to host byte order */
					ack->h.payload_len = SWAP32(ack->h.payload_len);
					ack->h.checksum    = SWAP16(ack->h.checksum);
					if (ack->h.payload_len != 0) {
						printf("data payload len %d != 0\n", ack->h.payload_len);
						userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
						return -1;
					}
					/* check response */
					if ((ack->h.flags & CMD_HFLAG_CHECK) &&
							(check_sum16(cmdbuff, MB_CMD_HEADER_SIZE) != 0xffff)) {
						printf("write data response 1 checksum error\n");
						return -1;
					}
				}
			}
		}
	}
	return 0;
}

static int com_write_byte(int fd, unsigned int addr, unsigned int len, unsigned char *data)
{
	unsigned int payload_len = 5 + len;
	unsigned char cmdbuff[MB_CMD_HEADER_SIZE + 13] = {0}; /* max 8 bytes data */
	cmd_rw_t *cmd = (cmd_rw_t *)cmdbuff;
	cmd_ack_t *ack = (cmd_ack_t *)cmdbuff;

	/* fill header */
	FILL_HEADER_MAGIC(&cmd->h);
	cmd->h.flags = CMD_HFLAG_CHECK;
	cmd->h.version = 0;
	cmd->h.checksum = 0;
	cmd->h.payload_len = payload_len; // cmdid(1) + addr(4) + data(len)
	/* fill command id */
	cmd->cmdid = len == 1 ? CMD_ID_WRITE1 :
	 len == 2 ? CMD_ID_WRITE2 :
	 len == 4 ? CMD_ID_WRITE4 :
	 len == 8 ? CMD_ID_WRITE8 : CMD_ID_WRITE4;
	/* fill command address, data and checksum */
	cmd->addr = addr;
	memcpy(cmdbuff + sizeof(cmd_rw_t), data, len);
	cmd->h.checksum = ~check_sum16(cmdbuff, MB_CMD_HEADER_SIZE + payload_len);

	/* convert host byte order to network byte order */
	cmd->h.payload_len = SWAP32(cmd->h.payload_len);
	cmd->addr          = SWAP32(cmd->addr);
	cmd->h.checksum    = SWAP16(cmd->h.checksum);

	/* clear rx buffer */
	userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
	/* send cmd */
	userial_write(fd, cmdbuff, MB_CMD_HEADER_SIZE + payload_len);
	userial_read(fd, cmdbuff, MB_CMD_HEADER_SIZE);

	/* check data */
	if (!HEADER_MAGIC_VALID(&ack->h)) {
		printf("invalid response\n");
		userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
		return -1;
	} else if (ack->h.flags & CMD_HFLAG_ERROR) {
		userial_read(fd, cmdbuff + MB_CMD_HEADER_SIZE, 1);
		printf("resp error flag, type %d\n", ack->err);
		return -ack->err;
	} else {
		if (ack->h.flags & CMD_HFLAG_ACK) {
			/* convert network byte order to host byte order */
			ack->h.payload_len = SWAP32(ack->h.payload_len);
			ack->h.checksum    = SWAP16(ack->h.checksum);
			if (ack->h.payload_len != 0) {
				printf("data payload len %d != 0\n", ack->h.payload_len);
				userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
				return -1;
			}
			/* check response */
			if ((ack->h.flags & CMD_HFLAG_CHECK) &&
						(check_sum16(cmdbuff, MB_CMD_HEADER_SIZE) != 0xffff)) {
					printf("write data response checksum error\n");
					return -1;
			}
		}
	}
	return 0;
}

static int com_set_pc(int fd, unsigned int pc)
{
	unsigned int payload_len = 5; /* cmdid(1) + PC addr(4) */
	unsigned char cmdbuff[MB_CMD_HEADER_SIZE + 5] = {0};
	cmd_sys_t *cmd = (cmd_sys_t*)cmdbuff;
	cmd_ack_t *ack = (cmd_ack_t *)cmdbuff;

	/* fill header */
	FILL_HEADER_MAGIC(&cmd->h);
	cmd->h.flags = CMD_HFLAG_CHECK;
	cmd->h.version = 0;
	cmd->h.checksum = 0;
	cmd->h.payload_len = payload_len;
	/* fill command id */
	cmd->cmdid = CMD_ID_SETPC;
	cmd->val = pc;
	cmd->h.checksum = ~check_sum16(cmdbuff, MB_CMD_HEADER_SIZE + payload_len);
	/* convert host byte order to network byte order */
	cmd->h.payload_len = SWAP32(cmd->h.payload_len);
	cmd->h.checksum    = SWAP16(cmd->h.checksum);
	cmd->val           = SWAP32(cmd->val);
	printf("set pc %x, val %x\n", pc, cmd->val);

	/* clear rx buffer */
	userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
	/* send command */
	userial_write(fd, cmdbuff, MB_CMD_HEADER_SIZE + payload_len);
	userial_read(fd, cmdbuff, MB_CMD_HEADER_SIZE);
	/* check header */
	if (!HEADER_MAGIC_VALID(&ack->h)) {
		printf("invalid response\n");
		userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
		return -1;
	} else if (ack->h.flags & CMD_HFLAG_ERROR) {
		userial_read(fd, cmdbuff + MB_CMD_HEADER_SIZE, 1);
		printf("resp error flag, type %d\n", ack->err);
		return -ack->err;
	} else {
		if (ack->h.flags & CMD_HFLAG_ACK) {
			/* convert network byte order to host byte order */
			ack->h.payload_len = SWAP32(ack->h.payload_len);
			ack->h.checksum    = SWAP16(ack->h.checksum);
			if (ack->h.payload_len != 0) {
				printf("data payload len %d != 0\n", ack->h.payload_len);
				userial_clearbuf(fd, UART_BUF_RXCLEAR|UART_BUF_TXCLEAR);
				return -1;
			}
			if ((ack->h.flags & CMD_HFLAG_CHECK) &&
						(check_sum16(cmdbuff, MB_CMD_HEADER_SIZE)) != 0xffff) {
				printf("Set PC response checksum error\n");
				return -1;
			}
			printf("Now the system will jump to %08x\n", pc);
		}
	}
	return 0;
}

static int load_btfirmware(int fd)
{
	FILE* fwfile_fd = NULL;
	unsigned int filesize, len;
	unsigned char *data = NULL;
	unsigned int addr = g_load_addr;
	unsigned char flag_value[4] = {0xFF, 0xFF, 0xFF, 0xFF};

	printf("[%s] start loading firmware...\n", __FUNCTION__);

	fwfile_fd = fopen(BT_FW_PATH_NAME, "rb");
	if(!fwfile_fd)
		return -1;

	printf("[%s] open firmware file success.\n", __FUNCTION__);

	fseek(fwfile_fd, 0, SEEK_END);
	filesize = ftell(fwfile_fd);
	fseek(fwfile_fd, 0, SEEK_SET);
	len = filesize;
	len = len > SZ_1K ? SZ_1K : len;
	data = (unsigned char*)malloc(len);
	if (data == NULL) {
      printf("failed to alloc %d byte memory\n", len);
      fclose(fwfile_fd);
      return -1;
	}

	do {
		len = filesize;
		len = (len > SZ_1K) ? SZ_1K : len;
		fread(data, 1, len, fwfile_fd);
		com_stream_write(fd, addr, len, data);
		addr += len;
		filesize -= len;
		//printf("remain len:\t0x%04X.\n", filesize);
	} while(filesize);
	free(data);
	fclose(fwfile_fd);
	printf("load firmware done.\n");

	/* jump */
	printf("jump:\n");
	com_set_pc(fd, g_jump_addr);

	if(g_chip_name == 2)
	{
		printf("\nsecond time sync starting....\n");
		if(userial_sync(fd) < 0)
			return -1;
		com_set_pc(fd, g_jump_addr);
	}
	return addr;
}

int xradio_init(int fd, int def_speed, int speed, struct termios *ti,
		const char *bdaddr)
{
	printf("xradio_init\n");

	if (init_rfkill())
		return -1;

	if (upio_set_bluetooth_power(UPIO_BT_POWER_OFF))
		return -1;
	usleep(500000);

	if (upio_set_bluetooth_power(UPIO_BT_POWER_ON))
		return -1;
	usleep(20000);

	upio_init();
	upio_set_btwake(LPM_BT_WAKE_POLARITY);
	upio_set(UPIO_LPM_MODE, UPIO_DEASSERT, 0);
	usleep(50000);

	if (userial_sync(fd))
		return -1;

	if (com_sync_baudrate(fd, speed, speed | (3 << 24)))
		return -1;

	if (load_btfirmware(fd) < 0)
		return -1;

	usleep(50000);

	userial_vendor_set_baud(fd, def_speed);
	usleep(100000);
	userial_vendor_set_hw_fctrl(fd, 1);
	usleep(100000);

	if (g_startup_reset_flag) {
		printf("[%s] send reset cmd...\n", __FUNCTION__);
		proc_reset(fd);
	}

	if (g_update_hcirate_flag) {
		printf("[%s] update hci baudrate...\n", __FUNCTION__);
		proc_baudrate(fd, speed);
	}

	if (g_bdaddr_flag) {
		printf("[%s] set bdaddr...\n", __FUNCTION__);
		proc_bdaddr(fd);
		proc_reset(fd);
	}

	if (g_hciup_flag) {
		printf("[%s] bring up hci...\n", __FUNCTION__);
		proc_enable_hci(fd);
	}

#ifdef CONFIG_XR829_BT_LPM
    upio_set(UPIO_LPM_MODE, UPIO_ASSERT, 0);
#endif
	return 0;
}
