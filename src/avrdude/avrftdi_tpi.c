#include "ac_cfg.h"

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "usbasp.h"

#include "avrftdi_tpi.h"
#include "avrftdi_private.h"

#ifndef DO_NOT_BUILD_AVRFTDI

static void avrftdi_tpi_disable(PROGRAMMER *);
static int avrftdi_tpi_program_enable(PROGRAMMER * pgm, AVRPART * p);

#ifdef notyet
static void
avrftdi_debug_frame(uint16_t frame)
{
	static char bit_name[] = "IDLES01234567PSS";
	//static char bit_name[] = "SSP76543210SELDI";
	char line0[34], line1[34], line2[34];
	int bit, pos;

	for(bit = 0; bit < 16; bit++)
	{
		pos = 16 - bit - 1;
		if(frame & (1 << pos))
		{
			line0[2*pos]  = '_';
			line0[2*pos+1] = ' ';
			
			line2[2*pos]  = ' ';
			line2[2*pos+1] = ' ';
		}
		else
		{
			line0[2*pos]  = ' ';
			line0[2*pos+1] = ' ';
			
			line2[2*pos]  = '-';
			line2[2*pos+1] = ' ';
		}
			
		line1[2*pos]  = bit_name[pos];
		line1[2*pos+1] = ' ';
			
	}

	line0[32] = 0;
	line1[32] = 0;
	line2[32] = 0;

	log_debug("%s\n", line0);
	log_debug("%s\n", line1);
	//log_debug("%s\n", line2);
}
#endif /* notyet */

int
avrftdi_tpi_initialize(PROGRAMMER * pgm, AVRPART * p)
{
	int ret;

	avrftdi_t* pdata = to_pdata(pgm);
	unsigned char buf[] = { MPSSE_DO_WRITE | MPSSE_WRITE_NEG | MPSSE_LSB, 0x01, 0x00, 0xff, 0xff };

	log_info("Using TPI interface\n");

	pgm->program_enable = avrftdi_tpi_program_enable;
	pgm->cmd_tpi = avrftdi_cmd_tpi;
	pgm->chip_erase = avr_tpi_chip_erase;
	pgm->disable = avrftdi_tpi_disable;

	pgm->paged_load = NULL;
	pgm->paged_write = NULL;

	log_info("Setting /Reset pin low\n");
	pgm->setpin(pgm, PIN_AVR_RESET, OFF);
	pgm->setpin(pgm, PIN_AVR_SCK, OFF);
	pgm->setpin(pgm, PIN_AVR_MOSI, ON);
	usleep(20 * 1000);

	pgm->setpin(pgm, PIN_AVR_RESET, ON);
	/* worst case 128ms */
	usleep(2 * 128 * 1000);

	/*setting rst back to 0 */
	pgm->setpin(pgm, PIN_AVR_RESET, OFF);
	/*wait at least 20ms bevor issuing spi commands to avr */
	usleep(20 * 1000);
	
	log_info("Sending 16 init clock cycles ...\n");
	ret = ftdi_write_data(pdata->ftdic, buf, sizeof(buf));

	return ret;
}

#define TPI_PARITY_MASK 0x2000

static uint16_t
tpi_byte2frame(uint8_t byte)
{
	uint16_t frame = 0xc00f;
	int parity = __builtin_popcount(byte) & 1;

	frame |= ((byte << 5) & 0x1fe0);

	if(parity)
		frame |= TPI_PARITY_MASK;
	
	return frame;
}

static int
tpi_frame2byte(uint16_t frame, uint8_t * byte)
{
	/* drop idle and start bit(s) */
	*byte = (frame >> 5) & 0xff;

	int parity = __builtin_popcount(*byte) & 1;
	int parity_rcvd = (frame & TPI_PARITY_MASK) ? 1 : 0;

	return parity != parity_rcvd;
}

#ifdef notyet
static int
avrftdi_tpi_break(PROGRAMMER * pgm)
{
	unsigned char buffer[] = { MPSSE_DO_WRITE | MPSSE_WRITE_NEG | MPSSE_LSB, 1, 0, 0, 0 };
	E(ftdi_write_data(to_pdata(pgm)->ftdic, buffer, sizeof(buffer)) != sizeof(buffer), to_pdata(pgm)->ftdic);

	return 0;
}
#endif /* notyet */

static int
avrftdi_tpi_write_byte(PROGRAMMER * pgm, unsigned char byte)
{
	uint16_t frame;

	struct ftdi_context* ftdic = to_pdata(pgm)->ftdic;

	unsigned char buffer[] = { MPSSE_DO_WRITE | MPSSE_WRITE_NEG | MPSSE_LSB, 1, 0, 0, 0 };

	frame = tpi_byte2frame(byte);
	
	buffer[3] = frame & 0xff;
	buffer[4] = frame >> 8;
	
	log_trace("Byte %02x, frame: %04x, MPSSE: 0x%02x 0x%02x 0x%02x  0x%02x 0x%02x\n",
			byte, frame, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);

	//avrftdi_debug_frame(frame);
	
	E(ftdi_write_data(ftdic, buffer, sizeof(buffer)) != sizeof(buffer), ftdic);

	return 0;
}

#define TPI_FRAME_SIZE 12
#define TPI_IDLE_BITS   2

static int
avrftdi_tpi_read_byte(PROGRAMMER * pgm, unsigned char * byte)
{
	uint16_t frame;
	
	/* use 2 guard bits, 2 default idle bits + 12 frame bits = 16 bits total */
	const int bytes = 3;
	int err, i = 0;
	
	unsigned char buffer[4];

	buffer[0] = MPSSE_DO_READ | MPSSE_LSB;
	buffer[1] = (bytes-1) & 0xff;
	buffer[2] = ((bytes-1) >> 8) & 0xff;
	buffer[3] = SEND_IMMEDIATE;

	log_trace("MPSSE: 0x%02x 0x%02x 0x%02x 0x%02x (Read frame)\n",
			buffer[0], buffer[1], buffer[2], buffer[3]);

	ftdi_write_data(to_pdata(pgm)->ftdic, buffer, 4);

	memset(buffer, 0, sizeof(buffer));

	i = 0;
	do {
		int err = ftdi_read_data(to_pdata(pgm)->ftdic, &buffer[i], bytes - i);
		E(err < 0, to_pdata(pgm)->ftdic);
		i += err;
	} while(i < bytes);


	log_trace("MPSSE: 0x%02x 0x%02x 0x%02x 0x%02x (Read frame)\n",
			buffer[0], buffer[1], buffer[2], buffer[3]);


	frame = buffer[0] | (buffer[1] << 8);
	
	err = tpi_frame2byte(frame, byte);
	log_trace("Frame: 0x%04x, byte: 0x%02x\n", frame, *byte);
	
	//avrftdi_debug_frame(frame);

	return err;
}

static int
avrftdi_tpi_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
	return avr_tpi_program_enable(pgm, p, TPIPCR_GT_2b);
}

int
avrftdi_cmd_tpi(PROGRAMMER * pgm, const unsigned char *cmd, int cmd_len,
		unsigned char *res, int res_len)
{
	int i, err = 0;

	for(i = 0; i < cmd_len; i++)
	{
		err = avrftdi_tpi_write_byte(pgm, cmd[i]);
		if(err)
			return err;
	}

	for(i = 0; i < res_len; i++)
	{
		err = avrftdi_tpi_read_byte(pgm, &res[i]);
		if(err)
			return err;
	}

	return 0;
}

static void
avrftdi_tpi_disable(PROGRAMMER * pgm)
{
	unsigned char cmd[] = {TPI_OP_SSTCS(TPIPCR), 0};
	pgm->cmd_tpi(pgm, cmd, sizeof(cmd), NULL, 0);

	log_info("Leaving Programming mode.\n");
}

#endif /* DO_NOT_BUILD_AVRFTDI */

