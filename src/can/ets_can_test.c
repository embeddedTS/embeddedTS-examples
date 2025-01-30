/* SPDX-License-Identifier: BSD-2-Clause */

/* A simple example that will attempt to communicate with an Ozen mOByDic1610
 * OBD ECU simulator, or, do a simple emulation of one aspect of it for a
 * loopback test.
 *
 * When doing a loopback between the two ports locally in a single command,
 * this is a one-shot loop. Similar if --query is specified. If running in
 * --ecu mode, then this will loop forever unless there is an error.
 *
 * On each loop, depending on the operation mode, it will send a query to
 * the mOByDic 1610 to read the RPM gauge. If emulating this ECU, it will
 * respond by returning a random RPM value, 0-255. Then it will wait for
 * a response from the ECU.
 *
 * For simplicity, messages sent on the bus are just using a raw write()
 * since the packet format is quite simple. However, on the receive side
 * recvmsg() is used. This allows for more complex parsing, including
 * timestamping, flags, sizes, etc. For the messages used here, read()
 * would likely be fine, but we wanted to provide an example that can
 * be expanded for additional functionality.
 */

#include <assert.h>
#include <getopt.h>
#include <linux/can.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef RELEASE
#define RELEASE "Unknown"
#endif

static void usage(char **argv)
{
	fprintf(stderr,
		"Version %s - Built: " __DATE__ "\n\n"
		"embeddedTS CAN example application\n"
		"Usage:\n"
		"  %s [(--ecu | --query) --iface <iface>]\n"
		"  %s --help\n"
		"\n"
		"  -i, --iface <iface>        Specify single interface to use\n"
		"  -e, --ecu                  Emulate ECU RPM on <iface>\n"
		"  -q, --query                Query ECU RPM on <iface>\n"
		"  -h, --help                 This message\n"
		"\n"
		"  With no options specified, attempts to open both can0 and can1\n"
		"  interfaces and do a simple one-shot loopback test between the\n"
		"  two.\n"
		"\n"
		"  Only one of --ecu or --query can be specified, and if either are\n"
		"  specified, then --iface must be as well. The --ecu instance\n"
		"  will continue to run and await queries on the interface and\n"
		"  respond to them.\n"
		"\n",
		RELEASE, argv[0], argv[0]
	);
}

static signed int test_and_bind(int sock, struct ifreq *ifr,
				struct sockaddr_can *addr, char *iface)
{
	/* Write the interface name to the struct, this is used by ioctl to find
	 * the interface index if it exists.
	 */
	memset(ifr->ifr_name, '\0', IFNAMSIZ);
	strncpy(ifr->ifr_name, iface, IFNAMSIZ-1);

	if (ioctl(sock, SIOCGIFINDEX, ifr) < 0) {
		fprintf(stderr, "Unable to open iface %s: ", iface);
		perror("");
		return -1;
	}
	addr->can_family = AF_CAN;
	addr->can_ifindex = ifr->ifr_ifindex;

	/* Now, bind the interface to the socket for use */
	if (bind(sock, (struct sockaddr *)addr, sizeof(struct sockaddr_can)) < 0) {
		fprintf(stderr, "Unable to bind on iface %s: ", iface);
		perror("");
		return -1;
	}

	return 0;
}

static signed int poll_sock_fd(int fd_epoll, struct epoll_event *event,
				int expected_fd, int err_on_timeout)
{
	int num_events;

	num_events = epoll_wait(fd_epoll, event, 1, 1000);
	if (num_events < 0) {
		fprintf(stderr, "epoll_wait error on %d: ", expected_fd);
		perror("");
		return -1;
	}

	if (num_events == 0) {
		if (err_on_timeout) {
			fprintf(stderr, "Timeout waiting for receive on %d!\n",
				expected_fd);
			return -1;
		} else {
			return 0;
		}
	}

	/* Check that the event was on the expected FD */
	if (event[0].data.fd != expected_fd) {
		fprintf(stderr, "Received event on unexpected socket! "
			"Expected: %d, Got: %d\n",
			expected_fd, event[0].data.fd);
		return -1;
	}

	return num_events;
}


int main(int argc, char **argv)
{
	/* ECU emulation related */
	int ecu_recv_sock = 0;
	struct sockaddr_can ecu_recv_addr;
	struct epoll_event ecu_recv_event = {
		.events = EPOLLIN,
	};

	/* Query mode related */
	int query_recv_sock = 0;
	struct sockaddr_can query_recv_addr;
	struct epoll_event query_recv_event = {
		.events = EPOLLIN,
	};

	/* msghdr related */
	struct can_frame frame;
	struct ifreq ifr;
	struct iovec iov;
	struct msghdr msg;
	char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];

	/* epoll related */
	int fd_epoll = 0;
	int nbytes;
	struct epoll_event events_pending[1];
	int num_events = 0;

	/* Program flow related */
	int c;
	int opt_ecu = 0;
	int opt_query = 0;
	int opt_loopback = 0;
	char opt_iface[IFNAMSIZ] = {0};
	int ret = 1;

	static struct option long_options[] = {
		{ "iface",	required_argument, 	NULL, 'i' },
		{ "ecu",	no_argument,		NULL, 'e' },
		{ "query",	no_argument,		NULL, 'q' },
		{ "help",	no_argument,		NULL, 'h' },
		{NULL},
	};

	while((c = getopt_long(argc, argv, "i:eqh", long_options, NULL)) != -1) {
		switch(c) {
		case 'i':
			strncpy(opt_iface, optarg, sizeof(opt_iface)-1);
			break;
		case 'e':
			opt_ecu = 1;
			break;
		case 'q':
			opt_query = 1;
			break;
		case 'h':
		default:
			usage(argv);
			return 1;
			break;
		}
	}

	/* Test for correct argument combinations */
	if (opt_ecu && opt_query) {
		fprintf(stderr, "Error! May only specify one of --ecu or --query!\n");
		return 1;
	}

	if ((opt_ecu || opt_query) && opt_iface[0] == '\0') {
		fprintf(stderr, "Error! --iface must be specified with --ecu or "
			"--query!\n");
		return 1;
	}

	if (!(opt_ecu || opt_query))
		opt_loopback = 1;

	/* Set up sockets */
	if((query_recv_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Error opening send socket");
		return 1;
	}

	if((ecu_recv_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("Error opening recv socket");
		close(query_recv_sock);
		return 1;
	}
 
	if (opt_query) {
		if (test_and_bind(query_recv_sock, &ifr, &query_recv_addr, opt_iface) < 0)
			return 1;
	} else if (opt_ecu) {
		if (test_and_bind(ecu_recv_sock, &ifr, &ecu_recv_addr, opt_iface) < 0)
			return 1;
	/* Local loopback on can0 and can1 */
	} else {
		if (test_and_bind(query_recv_sock, &ifr, &query_recv_addr, "can0") < 0)
			return 1;

		if (test_and_bind(ecu_recv_sock, &ifr, &ecu_recv_addr, "can1") < 0)
			return 1;
	}

	/* Set up epoll FD handling */
	fd_epoll = epoll_create1(0);
	if (fd_epoll < 0) {
		perror("Error creating epoll");
		return 1;
	}

	ecu_recv_event.data.fd = ecu_recv_sock;
	if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, ecu_recv_sock, &ecu_recv_event) < 0) {
		perror("Error adding ECU recv socket to epoll");
		return 1;
	}

	query_recv_event.data.fd = query_recv_sock;
	if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, query_recv_sock, &query_recv_event) < 0) {
		perror("Error adding query recv socket to epoll");
		return 1;
	}

	/* Set up message struct for receiving messages for parsing */
	iov.iov_base = &frame;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrlmsg;
	iov.iov_len = sizeof(frame);
	msg.msg_namelen = sizeof(struct sockaddr_can);
	msg.msg_controllen = sizeof(ctrlmsg);
	msg.msg_flags = 0;

	/* Seed random RPM return value */
	srandom(time(NULL));

	while (1) {
		ret = 1;
		/* Send initial packet request if querying or in loopback mode */
		if (opt_query || opt_loopback) {
		 	/* For the ozen mOByDic 1610 this requests the RPM guage */
			frame.can_id  = 0x7df;
			frame.can_dlc = 3;
			frame.data[0] = 3;
			frame.data[1] = 1;
			frame.data[2] = 0x0c;
 
			if (write(query_recv_sock, &frame, sizeof(struct can_frame)) < 0) {
				perror("Error sending query");
				break;
			}
		}

		/* Wait to receive packet if ECU mode or loopback */
		if (opt_ecu || opt_loopback) {
			msg.msg_name = &ecu_recv_addr;

			/* Error on timeout if opt_loopback, otherwise, if just
			 * opt_ecu, a timeout will cause this to return 0.
			 */
			num_events = poll_sock_fd(fd_epoll, events_pending,
						  ecu_recv_sock, opt_loopback);
			if (num_events < 0)
				break;

			if (num_events == 0)
				continue;

			nbytes = recvmsg(ecu_recv_sock, &msg, 0);
			if (nbytes < 0) {
				perror("Error receving on ECU emulation");
				break;
			}

			if (nbytes < (int)sizeof(struct can_frame)) {
				fprintf(stderr, "Incomplete CAN frame on ECU emulation\n");
			}

			if (frame.data[0] == 0x03) {
				/* Set up response */
				frame.can_id = 0x7e8;
				frame.can_dlc = 5;
				memset(frame.data, '\0', sizeof(frame.data));
				frame.data[0] = 0x04;
				frame.data[1] = 0x41;
				frame.data[2] = 0x0c;
				frame.data[3] = random() & 0xFF; // RPM value
				frame.data[4] = 0x40;
				if (write(ecu_recv_sock, &frame,
				  sizeof(struct can_frame)) < 0) {
					perror("Error sending ECU response");
					break;
				}
			}
		}

		/* Finally, receive response from ECU if querying or loopback */
		if (opt_query || opt_loopback) {
			msg.msg_name = &query_recv_addr;

			num_events = poll_sock_fd(fd_epoll, events_pending,
						  query_recv_sock, 1);
			if (num_events < 0)
				break;

			nbytes = recvmsg(query_recv_sock, &msg, 0);
			if (nbytes < 0) {
				perror("Error receving on query");
				break;
			}
	
			if (nbytes < (int)sizeof(struct can_frame)) {
				fprintf(stderr, "Incomplete CAN frame on query\n");
			}

			if(frame.data[0] == 0x4)
				printf("RPM at %d of 255\n", frame.data[3]);
		}

		/* Finally, if we're querying or loopback, break out of the loop */
		ret = 0;
		if (!opt_ecu)
			break;
	}

	close(ecu_recv_sock);
	close(query_recv_sock);
 
	return ret;
}
