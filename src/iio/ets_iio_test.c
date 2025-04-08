/* SPDX License */

/* Description of the tool */

#include <assert.h>
#include <errno.h>
#include <error.h>
#include <iio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* In general, samples being processed would be of a known width and signedness.
 * However, since this is a rather generic function, we need to do some type
 * casting in order to ensure we interpret the data as expected.
 *
 * In order to do this without too much verbosity of code, we use a union.
 *
 * Real applications should understand exact details about the IIO device they
 * are reading and will know exactly what channels would be read.
 */
union types {
	int8_t i8;
	uint8_t u8;
	int16_t i16;
	uint16_t u16;
	int32_t i32;
	uint32_t u32;
	int64_t i64;
	uint64_t u64;
};
static ssize_t process_sample_cb(const struct iio_channel *chan, void *src,
				size_t bytes, void *d)
{
	int is_signed = iio_channel_get_data_format(chan)->is_signed;
	int do_scale = iio_channel_get_data_format(chan)->with_scale;
	double scale = iio_channel_get_data_format(chan)->scale;
	double scaled_val;
	int64_t raw;
	union types value;
	(void)d;

	/* This will only work on samples of up to 64-bit */
	assert(bytes <= sizeof(uint64_t));

	/* This will copy the buffer contents to our local variable while also
	 * converting from the hardware format to the system's native format.
	 * i.e. endianness and any bit-shifting of the samples.
	 */
	iio_channel_convert(chan, &value, src);

	switch (bytes) {
	case 1:
		if (is_signed) {
			raw = value.i8;
			if (do_scale)
				scaled_val = scale * value.i8;
			else
				scaled_val = value.i8;
		} else {
			raw = value.u8;
			if (do_scale)
				scaled_val = scale * value.u8;
			else
				scaled_val = value.u8;
		}
		break;
	case 2:
		if (is_signed) {
			raw = value.i16;
			if (do_scale)
				scaled_val = scale * value.i16;
			else
				scaled_val = value.i16;
		} else {
			raw = value.u16;
			if (do_scale)
				scaled_val = scale * value.u16;
			else
				scaled_val = value.u16;
		}
		break;
	case 4:
		if (is_signed) {
			raw = value.i32;
			if (do_scale)
				scaled_val = scale * value.i32;
			else
				scaled_val = value.i32;
		} else {
			raw = value.u32;
			if (do_scale)
				scaled_val = scale * value.u32;
			else
				scaled_val = value.u32;
		}
		break;
	case 8:
		if (is_signed) {
			raw = value.i64;
			if (do_scale)
				scaled_val = scale * value.i64;
			else
				scaled_val = value.i64;
		} else {
			raw = value.u64;
			if (do_scale)
				scaled_val = scale * value.u64;
			else
				scaled_val = value.u64;
		}
		break;
	default:
		fprintf(stderr, "Sample is unsupported width, %zd bytes\n", bytes);
		return -1;
	}

	printf("%s: raw=%" PRId64 ",\tscaled=%lf\n",
		iio_channel_get_id(chan), raw, scaled_val);

	return bytes;
}

#ifndef RELEASE
#define RELEASE "Unknown"
#endif

static void usage(char **argv)
{
	fprintf(stderr,
		"Version %s - Built: " __DATE__ "\n\n"
		"embeddedTS IIO example application\n"
		"Usage:\n"
		"  %s <device> <channel>...\n"
		"  %s --help\n"
		"\n"
		"  -h, --help                 This message\n"
		"\n"
		"  Take a sample from each <channel> specified. Samples are printed\n"
		"  both with their raw value and scaled value.\n"
		"\n"
		"  With no <device> specified, prints all devices available to the\n"
		"  system.\n"
		"\n"
		"  With no <channel>s specified, prints all available channels for the\n"
		"  <device>.\n"
		"\n"
		"  This tool currently only supports buffered channels.\n"
		"\n",
		RELEASE, argv[0], argv[0]
	);
}

int main(int argc, char *argv[])
{
	struct iio_context *iio_ctx = NULL;
	struct iio_device *iio_dev = NULL;
	int num_channels, i;
	unsigned int x;
	struct iio_channel *channels[10];
	struct iio_buffer *buffer = NULL;
	ssize_t cnt;

	/* Print usage when -h/--h(help) is specified.
	 * If there are no args, instead of help output, attempt to list
	 * IIO devices available to the system.
	 */
	if (argc > 1 &&
	   (!(strncmp(argv[1], "-h", 2)) ||
	   !(strncmp(argv[1], "--h", 3)))) {
		usage(argv);
		return 1;
	}

	iio_ctx = iio_create_local_context();
	if (iio_ctx == NULL) {
		perror("Unable to create IIO context");
		return errno;
	}

	/* List devices available to the system if no <device> arg was provided */
	/* XXX: TODO: This is presenting devices that are not really devices and
	 * erroring in some cases. e.g.
root@tsimx6ul:~# ets_iio_test 
No devices specified.
Available devices:
'50000180.mikro_adc'
'tssupervisor-adc'
'2198000.adc'
'tssupervisor-temp'
'an_3p3v'
'an_5v'
'an_8v_48v'
'ism330dlc_gyro'
'ism330dlc_accel'
'lis2mdl'
'lis2mdl-trigger'
Segmentation fault
*/

	if (argc == 1) {
		fprintf(stderr, "No devices specified.\n");
		fprintf(stderr, "Available devices:\n");
		for (x = 0; x < iio_context_get_devices_count(iio_ctx) ; x++) {
			iio_dev = iio_context_get_device(iio_ctx, x);
			if (iio_dev == NULL) {
				fprintf(stderr, "Unable to get IIO device %d: ", x);
				perror("");
				return errno;
			}
			fprintf(stderr, "'%s'\n", iio_device_get_name(iio_dev));
		}
	}

	iio_dev = iio_context_find_device(iio_ctx, argv[1]);
	if (iio_dev == NULL) {
		fprintf(stderr, "Unable to find IIO device '%s': ", argv[1]);
		perror("");
		usage(argv);
		return errno;
	}

	/* If no channels specified, list all of them */
	/* XXX: TODO: This is still listing things like timestamp that we should
	 * be able to parse, but cannot fully yet. e.g. 
root@tsimx6ul:~# ets_iio_test ism330dlc_accel
No channels for device 'ism330dlc_accel' specified.
Available channels:
'accel_x'
'accel_y'
'accel_z'
'timestamp'
root@tsimx6ul:~# ets_iio_test ism330dlc_accel accel_x timestamp
num_channels 2
WARNING: High-speed mode not enabled
accel_x: raw=-231,      scaled=-0.138185
timestamp: raw=1744146475254490671,     scaled=1744146475254490624.000000
accel_x: raw=-208,      scaled=-0.124427
timestamp: raw=1744146475330790671,     scaled=1744146475330790656.000000
accel_x: raw=-237,      scaled=-0.141775
timestamp: raw=1744146475407090671,     scaled=1744146475407090688.000000
accel_x: raw=-219,      scaled=-0.131007
timestamp: raw=1744146475483365671,     scaled=1744146475483365632.000000
accel_x: raw=-222,      scaled=-0.132802
timestamp: raw=1744146475559665671,     scaled=1744146475559665664.000000
accel_x: raw=-222,      scaled=-0.132802
timestamp: raw=1744146475635965671,     scaled=1744146475635965696.000000
accel_x: raw=-230,      scaled=-0.137587
timestamp: raw=1744146475712265671,     scaled=1744146475712265728.000000
accel_x: raw=-237,      scaled=-0.141775
timestamp: raw=1744146475788540671,     scaled=1744146475788540672.000000
*/
	num_channels = (argc-2);
	if (!num_channels) {
		fprintf(stderr, "No channels for device '%s' specified.\n", argv[1]);
		fprintf(stderr, "Available channels:\n");
		num_channels = iio_device_get_channels_count(iio_dev);
		for (i = 0; i < num_channels; i++) {
			channels[0] = iio_device_get_channel(iio_dev, i);
			if (channels[0] == NULL) {
				fprintf(stderr, "Unable to get IIO channel %d: ", i);
				perror("");
				return errno;
			}
			if (iio_channel_is_scan_element(channels[0]))
				fprintf(stderr, "'%s'\n", iio_channel_get_id(channels[0]));
		}
		return 1;
	}

	/* Find and set up the channels */
	for (i = 0; i < num_channels; i++) {
		channels[i] = iio_device_find_channel(iio_dev, argv[i+2], false);
		if (channels[i] == NULL) {
			fprintf(stderr,
				"Unable to find IIO channel '%s': ", argv[i+2]);
			perror("");
			return errno;
		}

		/* Enable channels
		 * Note that, this only actually is possible on buffered
		 * channels. While there is no issue with enabling a
		 * non-buffered channel, it is safer not to try.
		 */
		if (iio_channel_is_scan_element(channels[i])) {
			iio_channel_enable(channels[i]);
		} else {
			fprintf(stderr, "Channel '%s' is non-buffered. This tool " \
					"does not support unbuffered channels at this "\
					"time.\n", iio_channel_get_id(channels[i]));
			return 1;
		}
	}

	/* Create buffer and process samples */
	buffer = iio_device_create_buffer(iio_dev, num_channels * 4, false);
	if (buffer == NULL) {
		perror("Unable to create buffer");
		return errno;
	}

	/* XXX: TODO: In some instances, this fails. It seems to be related to the
	 * number of samples somehow. e.g. if (num_channels * >4) it seems to fail.
	 * Some combinations of channels on the commandline also result in this, e.g.
root@tsimx6ul:~# ets_iio_test ism330dlc_accel accel_x accel_y accel_z
num_channels 3
WARNING: High-speed mode not enabled
accel_x: raw=-218,      scaled=-0.130409
...
root@tsimx6ul:~# ets_iio_test ism330dlc_accel accel_x accel_y accel_z accel_x
num_channels 4
WARNING: High-speed mode not enabled
Unable to fill buffer: Invalid argument
root@tsimx6ul:~# ets_iio_test ism330dlc_accel accel_x accel_y accel_x
num_channels 3
WARNING: High-speed mode not enabled
accel_x: raw=-242,      scaled=-0.144766
...
root@tsimx6ul:~# ets_iio_test ism330dlc_accel timestamp
num_channels 1
WARNING: High-speed mode not enabled
Unable to create buffer: Invalid argument
root@tsimx6ul:~# ets_iio_test ism330dlc_accel timestamp accel_x
num_channels 2
WARNING: High-speed mode not enabled
accel_x: raw=-207,      scaled=-0.123828
timestamp: raw=1744146637987063191,     scaled=1744146637987063296.000000
*/
	cnt = iio_buffer_refill(buffer);
	if (cnt < 0) {
		perror("Unable to fill buffer");
		return errno;
	}
	if (cnt < (num_channels * 4)) {
		fprintf(stderr, "Short read from buffer!\n");
		return 1;
	}

	iio_buffer_foreach_sample(buffer, process_sample_cb, iio_dev);

	/* Clean up: destroy the buffer, disable channels, destroy the context */
	iio_buffer_destroy(buffer);
	for (i = 0; i < num_channels; i++) {
		if (iio_channel_is_scan_element(channels[i]))
			iio_channel_disable(channels[i]);
	}
	iio_context_destroy(iio_ctx);
}
