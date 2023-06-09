// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * based on libiio - ADRV9009 IIO streaming example
 *
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 * Copyright (C) 2019 Analog Devices Inc.
 * Copyright (C) 2023 Open Research Institute
 **/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <iio.h>
#include <unistd.h>

/* OPV-RTP configuration */
#define SYMBOLS_PER_40MS	1084
#define SAMPLES_PER_SYMBOL	10
#define SAMPLES_PER_40MS	(SYMBOLS_PER_40MS * SAMPLES_PER_SYMBOL)
#define SAMPLES_PER_SECOND	271000

/* Signal generator */
extern void next_tx_sample(int16_t * const i_sample, int16_t * const q_sample);

/* helper macros */
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

#define IIO_ENSURE(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
	long long lo_hz; // Local oscillator frequency in Hz
};

/* static scratch mem for strings */
static char tmpstr[64];

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
//static struct iio_channel *rx0_i = NULL;
//static struct iio_channel *rx0_q = NULL;

static struct iio_channel *tx0_i = NULL;
static struct iio_channel *tx0_q = NULL;
//static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *txbuf = NULL;

static bool stop;

/* cleanup and exit */
static void shutdown()
{	
	printf("* Destroying buffers\n");
//	if (rxbuf) { iio_buffer_destroy(rxbuf); }
	if (txbuf) { iio_buffer_destroy(txbuf); }

	printf("* Disabling streaming channels\n");
//	if (rx0_i) { iio_channel_disable(rx0_i); }
//	if (rx0_q) { iio_channel_disable(rx0_q); }
	if (tx0_i) { iio_channel_disable(tx0_i); }
	if (tx0_q) { iio_channel_disable(tx0_q); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish... Got signal %d\n", sig);
	stop = true;
}

/* check return value of iio_attr_write function (for whole device) */
static void errchk_dev(int v) {
	if (v < 0) { fprintf(stderr, "Error %d writing to IIO device\n", v); shutdown(); }
}

/* check return value of iio_channel_attr_write function */
static void errchk_chn(int v, const char* what) {
	 if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\nvalue may not be supported.\n", v, what); shutdown(); }
}

/* write attribute: long long int */
static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val)
{
	errchk_chn(iio_channel_attr_write_longlong(chn, what, val), what);
}

/* write attribute: long long int */
static long long rd_ch_lli(struct iio_channel *chn, const char* what)
{
	long long val;

	errchk_chn(iio_channel_attr_read_longlong(chn, what, &val), what);

	printf("\t %s: %lld\n", what, val);
	return val;
}

#if 0
/* write attribute: string */
static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str)
{
	errchk_chn(iio_channel_attr_write(chn, what, str), what);
}
#endif

/* helper function generating channel names */
static char* get_ch_name_mod(const char* type, int id, char modify)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d_%c", type, id, modify);
	return tmpstr;
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns adrv9009 phy device */
static struct iio_device* get_adrv9009_phy(void)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "adrv9009-phy");
	IIO_ENSURE(dev && "No adrv9009-phy found");
	return dev;
}

/* finds ADRV9009 streaming IIO devices */
static bool get_adrv9009_stream_dev(enum iodev d, struct iio_device **dev)
{
	switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "axi-adrv9009-tx-hpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "axi-adrv9009-rx-hpc");  return *dev != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds ADRV9009 streaming IIO channels */
static bool get_adrv9009_stream_ch(enum iodev d, struct iio_device *dev, int chid, char modify, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, modify ? get_ch_name_mod("voltage", chid, modify) : get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, modify ? get_ch_name_mod("voltage", chid, modify) : get_ch_name("voltage", chid), d == TX);
	return *chn != NULL;
}

/* finds ADRV9009 phy IIO configuration channel with id chid */
static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_adrv9009_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_adrv9009_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds ADRV9009 local oscillator IIO configuration channels */
static bool get_lo_chan(struct iio_channel **chn)
{
	 // LO chan is always output, i.e. true
	*chn = iio_device_find_channel(get_adrv9009_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
}

#if 0		// for Pluto only
/* finds AD9361 decimation/interpolation configuration channels */
// !!! not tested for receive decimation
static bool get_dec8_int8_chan(enum iodev d, struct iio_channel **chn)
{
	struct iio_device *dev;

	IIO_ENSURE(get_ad9361_stream_dev(d, &dev) && "No dec/int dev found");

	switch (d) {
		case RX:
			*chn = iio_device_find_channel(dev, get_ch_name("voltage", 0), false);
			return *chn != NULL;

		case TX:
			*chn = iio_device_find_channel(dev, get_ch_name("voltage", 0), true);
			return *chn != NULL;

		default:
			IIO_ENSURE(0);
			return false;
	}
}

/* enables or disables Pluto's FPGA 8x interpolator for tx */
static bool set_pluto_8x_interpolator(bool enable)
{
	// The tx interpolator is set by channel-specific attributes
	// 		of the channel named voltage0
	// 		of the device named cf-ad9361-dds-core-lpc.
	// Two attributes are involved: sampling_frequency and sampling_frequency_available.
	// The attribute sampling_frequency_available returns a list of two available values,
	// which should differ by a factor of 8 (within a few counts).
	// The higher value is the sample clock rate when interpolation is not used;
	// the lower value is the effective sample clock rate when interpolation is used.
	// Set the attribute sampling_frequency to the value corresponding to the range of
	// sample rates you want to use.

	struct iio_channel *tx_int8_chn;
	long long sf1, sf2;

	printf("* %s 8x transmit interpolation\n", enable ? "Enabling" : "Disabling");

	// obtain the transmit interpolator's config channel,
	// namely device cf-ad9361-dds-core-lpc, channel voltage0
	if (!get_dec8_int8_chan(TX, &tx_int8_chn)) { return false; }
	
	// read the attribute sampling_frequency_available,
	// which should be a list of two, like '30720000 3840000 '
	if(iio_channel_attr_read(	tx_int8_chn,
								"sampling_frequency_available",
								tmpstr, sizeof(tmpstr)
							) <= 0) {
		fprintf(stderr, "No sampling_frequency_available attribute\n");
		return false;
		}

	// parse the returned pair of numbers
	if (2 != sscanf(tmpstr, "%lld %lld ", &sf1, &sf2)) {
		fprintf(stderr, "sampling_frequency_available format unexpected\n");
		return false;
	}

	// validate that they're in approximate 8x ratio
	if (abs(8 * sf2 - sf1) > 20) {
		fprintf(stderr, "sampling_frequency_available values not in ~8x ratio\n");
		return false;
	}

	// Now we can enable or disable interpolation
	if (enable) {
		// write the lower of the two values into attribute sampling_frequency
		// to set the interpolator to active.
		printf("* Writing %lld to set 8x interpolation\n", sf2);
		wr_ch_lli(tx_int8_chn, "sampling_frequency", sf2);
	} else {
		// write the higher of the two values into attribute sampling_frequency
		// to set the interpolator to inactive.
		printf("* Writing %lld to disable 8x interpolation\n", sf1);
		wr_ch_lli(tx_int8_chn, "sampling_frequency", sf1);
	}

	return true;
}

/* sets TX sample rate, using Pluto's FPGA 8x interpolator if needed */
static bool set_tx_sample_rate(struct iio_channel *phy_chn, long long sample_rate)
{
	long long ad9361_sample_rate;
	bool use_pluto_8x_interpolator;

	if (sample_rate < 260417) {
		fprintf(stderr, "Pluto can't sample slower than 260417 Hz\n");
		shutdown();
	} else if (sample_rate > 61440000) {
		fprintf(stderr, "Pluto can't sample faster than 61.44 MHz\n");
		shutdown();
	} else if (sample_rate < 2083333) {
		ad9361_sample_rate = sample_rate *8;
		use_pluto_8x_interpolator = 1;
	} else {
		ad9361_sample_rate = sample_rate;
		use_pluto_8x_interpolator = 0;
	}

	if (!set_pluto_8x_interpolator(use_pluto_8x_interpolator)) {
		fprintf(stderr, "Failed to set Pluto 8x interpolator\n");
		shutdown();
	}

	// Now we can set the sample rate at the AD9361, which is either
	// the actual sample rate or the sample rate after interpolation.
	printf("* Setting AD9361 sample rate to %lld Hz\n", ad9361_sample_rate);
	wr_ch_lli(phy_chn, "sampling_frequency", ad9361_sample_rate);

	return true;
}
#endif

/* applies streaming configuration through IIO */
bool cfg_adrv9009_streaming_ch(struct stream_cfg *cfg, int chid)
{
	struct iio_channel *chn = NULL;

	// Configure phy and lo channels
	printf("* Acquiring ADRV9009 phy channel %d\n", chid);
	if (!get_phy_chan(true, chid, &chn)) {	return false; }

	// rd_ch_lli(chn, "rf_bandwidth");
	// rd_ch_lli(chn, "sampling frequency");

	// Configure LO channel
	printf("* Acquiring ADRV9009 TRX lo channel\n");
	if (!get_lo_chan(&chn)) { return false; }
	wr_ch_lli(chn, "frequency", cfg->lo_hz);
	return true;
}

#if 0	// just for Pluto?
/* adjusts single-ended crystal oscillator compensation for Pluto SDR */
static bool cfg_ad9361_xo_correction(int delta)
{
	errchk_dev(iio_device_attr_write_longlong(get_ad9361_phy(), "xo_correction", 40000000 + delta));
}

/* turns off automatic transmit calibration for Pluto SDR */
static bool cfg_ad9361_manual_tx_quad(void)
{
	errchk_dev(iio_device_attr_write(get_ad9361_phy(), "calib_mode", "manual_tx_quad"));
}
#endif

/* simple configuration and streaming */
/* usage:
 * Default context, assuming local IIO devices, i.e., this script is run on ADALM-Pluto for example
 $./a.out
 * URI context, find out the uri by typing `iio_info -s` at the command line of the host PC
 $./a.out usb:x.x.x
 * 
 */
int main (int argc, char **argv)
{
	// Streaming devices
	struct iio_device *tx;
//	struct iio_device *rx;

	// RX and TX sample counters
	size_t nrx = 0;
	size_t ntx = 0;
	
	// Buffer pointers
	char *p_dat, *p_end;
	ptrdiff_t p_inc;

	// Stream configuration
	struct stream_cfg trxcfg;

	// Listen to ctrl+c and IIO_ENSURE
	signal(SIGINT, handle_sig);

	// TRX stream config
	trxcfg.lo_hz = MHZ(905.05);	// 905.05 MHz RF frequency

	printf("* Acquiring IIO context\n");
	if (argc == 1) {
		IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
	}
	else if (argc == 2) {
		IIO_ENSURE((ctx = iio_create_context_from_uri(argv[1])) && "No context");
	}
	IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

	//unsigned int attrs_count = iio_context_get_attrs_count(ctx);
	//IIO_ENSURE(attrs_count > 0 && "No context attributes");
	// printf("Found IIO context:\n");
	// for (unsigned int index=0; index < attrs_count; index++) {
	// 	const char *attr_name;
	// 	const char *attr_val;
	// 	if (iio_context_get_attr(ctx, index, &attr_name, &attr_val) == 0) {
	// 		printf("%s: %s\n", attr_name, attr_val);
	// 	}
	// }

	printf("* Acquiring ADRV9009 streaming devices\n");
	IIO_ENSURE(get_adrv9009_stream_dev(TX, &tx) && "No tx dev found");
//	IIO_ENSURE(get_adrv9009_stream_dev(RX, &rx) && "No rx dev found");

#if 0		// only for Pluto
	printf("* Configuring Pluto SDR for transmitting\n");
	cfg_ad9361_manual_tx_quad();	// disable automatic TX calibration
	cfg_ad9361_xo_correction(-465);	// -465 out of 40e6 for remote lab's Pluto S/N b83991001015001f00c7a0653f04
#endif

	printf("* Configuring ADRV9009 for streaming\n");
	IIO_ENSURE(cfg_adrv9009_streaming_ch(&trxcfg, 0) && "TRX device not found");

	printf("* Initializing ADRV9009 IIO streaming channels\n");
//	IIO_ENSURE(get_adrv9009_stream_ch(RX, rx, 0, 'i', &rx0_i) && "RX chan i not found");
//	IIO_ENSURE(get_adrv9009_stream_ch(RX, rx, 0, 'q', &rx0_q) && "RX chan q not found");
	IIO_ENSURE(get_adrv9009_stream_ch(TX, tx, 0, 0, &tx0_i) && "TX chan i not found");
	IIO_ENSURE(get_adrv9009_stream_ch(TX, tx, 1, 0, &tx0_q) && "TX chan q not found");

	printf("* Enabling IIO streaming channels\n");
//	iio_channel_enable(rx0_i);
//	iio_channel_enable(rx0_q);
	iio_channel_enable(tx0_i);
	iio_channel_enable(tx0_q);
	
	printf("* Creating non-cyclic IIO buffers of %d samples (1 40ms frame)\n", SAMPLES_PER_40MS);
//	rxbuf = iio_device_create_buffer(rx, 1024*1024, false);
//	if (!rxbuf) {
//		perror("Could not create RX buffer");
//		shutdown();
//	}
	txbuf = iio_device_create_buffer(tx, SAMPLES_PER_40MS, false);
	if (!txbuf) {
		perror("Could not create TX buffer");
		shutdown();
		return 0;
	}
	
	// Write first TX buf with all zeroes, for cleaner startup 
	p_inc = iio_buffer_step(txbuf);
	p_end = iio_buffer_end(txbuf);
	for (p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {
			((int16_t*)p_dat)[0] = 0 << 4; // Real (I)
			((int16_t*)p_dat)[1] = 0 << 4; // Imag (Q)
		}

	printf("* Starting IO streaming (press CTRL+C to cancel)\n");
	while (!stop)
	{
		ssize_t nbytes_rx, nbytes_tx;

		// Schedule TX buffer
		nbytes_tx = iio_buffer_push(txbuf);
		if (nbytes_tx < 0) { printf("Error pushing buf %d\n", (int) nbytes_tx); shutdown(); }

		// Refill RX buffer
//		nbytes_rx = iio_buffer_refill(rxbuf);
//		if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); shutdown(); }

		// READ: Get pointers to RX buf and read IQ from RX buf port 0
//		p_inc = iio_buffer_step(rxbuf);
//		p_end = iio_buffer_end(rxbuf);
//		for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
			// Example: swap I and Q
//			const int16_t i = ((int16_t*)p_dat)[0]; // Real (I)
//			const int16_t q = ((int16_t*)p_dat)[1]; // Imag (Q)
//			((int16_t*)p_dat)[0] = q;
//			((int16_t*)p_dat)[1] = i;
//		}

		// WRITE: Get pointers to TX buf and write IQ to TX buf port 0
		p_inc = iio_buffer_step(txbuf);
		p_end = iio_buffer_end(txbuf);
		for (p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {
			// Example: fill with zeros
			// 14-bit sample needs to be MSB aligned so shift by 4
			// https://wiki.analog.com/resources/eval/user-guides/ad-fmcomms2-ebz/software/basic_iq_datafiles#binary_format
			//((int16_t*)p_dat)[0] = 0 << 2; // Real (I)
			//((int16_t*)p_dat)[1] = 0 << 2; // Imag (Q)
			next_tx_sample((int16_t*)p_dat, (int16_t*)(p_dat+2));
//			printf("%d %d\n", ((int16_t*)p_dat)[0], ((int16_t*)p_dat)[1]);
		}

		// Sample counter increment and status output
		// nrx += nbytes_rx / iio_device_get_sample_size(rx);
		ntx += nbytes_tx / iio_device_get_sample_size(tx);
		printf("\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx/1e6, ntx/1e6);
	}

	shutdown();

	return 0;
}
