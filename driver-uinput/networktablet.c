
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdint.h>
#include "protocol.h"

#define die(str, args...) { \
	perror(str); \
	exit(EXIT_FAILURE); \
}

//#define DEBUG




int udp_socket;


void init_device(int fd)
{
	// enable synchronization
	if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0)
		die("error: ioctl UI_SET_EVBIT EV_SYN");

	// enable 1 button
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		die("error: ioctl UI_SET_EVBIT EV_KEY");
	if (ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0)
		die("error: ioctl UI_SET_KEYBIT");
	if (ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN) < 0)
		die("error: ioctl UI_SET_KEYBIT");
	if (ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS) < 0)
		die("error: ioctl UI_SET_KEYBIT");
	if (ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS2) < 0)
		die("error: ioctl UI_SET_KEYBIT");

	// enable 2 main axes + pressure (absolute positioning)
	if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0)
		die("error: ioctl UI_SET_EVBIT EV_ABS");
	if (ioctl(fd, UI_SET_ABSBIT, ABS_X) < 0)
		die("error: ioctl UI_SETEVBIT ABS_X");
	if (ioctl(fd, UI_SET_ABSBIT, ABS_Y) < 0)
		die("error: ioctl UI_SETEVBIT ABS_Y");
	if (ioctl(fd, UI_SET_ABSBIT, ABS_PRESSURE) < 0)
		die("error: ioctl UI_SETEVBIT ABS_PRESSURE");

        {
          struct uinput_abs_setup abs_setup;
          struct uinput_setup setup;

          memset(&abs_setup, 0, sizeof(abs_setup));
          abs_setup.code = ABS_X;
          abs_setup.absinfo.value = 0;
          abs_setup.absinfo.minimum = 0;
          abs_setup.absinfo.maximum = UINT16_MAX;
          abs_setup.absinfo.fuzz = 0;
          abs_setup.absinfo.flat = 0;
          abs_setup.absinfo.resolution = 400;
          if (ioctl(fd, UI_ABS_SETUP, &abs_setup) < 0)
            die("error: UI_ABS_SETUP ABS_X");

          memset(&abs_setup, 0, sizeof(abs_setup));
          abs_setup.code = ABS_Y;
          abs_setup.absinfo.value = 0;
          abs_setup.absinfo.minimum = 0;
          abs_setup.absinfo.maximum = UINT16_MAX;
          abs_setup.absinfo.fuzz = 0;
          abs_setup.absinfo.flat = 0;
          abs_setup.absinfo.resolution = 400;
          if (ioctl(fd, UI_ABS_SETUP, &abs_setup) < 0)
            die("error: UI_ABS_SETUP ABS_Y");

          memset(&abs_setup, 0, sizeof(abs_setup));
          abs_setup.code = ABS_PRESSURE;
          abs_setup.absinfo.value = 0;
          abs_setup.absinfo.minimum = 0;
          abs_setup.absinfo.maximum = INT16_MAX;
          abs_setup.absinfo.fuzz = 0;
          abs_setup.absinfo.flat = 0;
          abs_setup.absinfo.resolution = 0;
          if (ioctl(fd, UI_ABS_SETUP, &abs_setup) < 0)
            die("error: UI_ABS_SETUP ABS_PRESSURE");

          memset(&setup, 0, sizeof(setup));
          snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Network Tablet");
          setup.id.bustype = BUS_VIRTUAL;
          setup.id.vendor  = 0x1;
          setup.id.product = 0x1;
          setup.id.version = 2;
          setup.ff_effects_max = 0;
          if (ioctl(fd, UI_DEV_SETUP, &setup) < 0)
            die("error: UI_DEV_SETUP");

          if (ioctl(fd, UI_DEV_CREATE) < 0)
            die("error: ioctl");
        }
}

int prepare_socket()
{
	int s;
	struct sockaddr_in addr;

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		die("error: prepare_socket()");

	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(GFXTABLET_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		die("error: prepare_socket()");

	return s;
}

void send_event(int device, int type, int code, int value)
{
	struct input_event ev;
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(device, &ev, sizeof(ev)) < 0)
		die("error: write()");
}

void quit(int signal) {
	close(udp_socket);
}


int main(void)
{
	int device;
	struct event_packet ev_pkt;

	if ((device = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) < 0)
		die("error: open");

	init_device(device);
	udp_socket = prepare_socket();

	printf("GfxTablet driver (protocol version %u) is ready and listening on 0.0.0.0:%u (UDP)\n"
		"Hint: Make sure that this port is not blocked by your firewall.\n", PROTOCOL_VERSION, GFXTABLET_PORT);

	signal(SIGINT, quit);
	signal(SIGTERM, quit);

	while (recv(udp_socket, &ev_pkt, sizeof(ev_pkt), 0) >= 9) {		// every packet has at least 9 bytes
		#ifdef DEBUG
		printf("."); fflush(0);
		#endif

		if (memcmp(ev_pkt.signature, "GfxTable", 8) != 0) {
			fprintf(stderr, "\nGot unknown packet on port %i, ignoring\n", GFXTABLET_PORT);
			continue;
		}
		ev_pkt.version = ntohs(ev_pkt.version);
		if (ev_pkt.version != PROTOCOL_VERSION) {
			fprintf(stderr, "\nGfxTablet app speaks protocol version %i but driver speaks version %i, please update\n",
				ev_pkt.version, PROTOCOL_VERSION);
			break;
		}

		ev_pkt.x = ntohs(ev_pkt.x);
		ev_pkt.y = ntohs(ev_pkt.y);
		ev_pkt.pressure = ntohs(ev_pkt.pressure);
		#ifdef DEBUG
		printf("x: %hu, y: %hu, pressure: %hu\n", ev_pkt.x, ev_pkt.y, ev_pkt.pressure);
		#endif

		send_event(device, EV_ABS, ABS_X, ev_pkt.x);
		send_event(device, EV_ABS, ABS_Y, ev_pkt.y);
		send_event(device, EV_ABS, ABS_PRESSURE, ev_pkt.pressure);

		switch (ev_pkt.type) {
			case EVENT_TYPE_MOTION:
				send_event(device, EV_SYN, SYN_REPORT, 1);
				break;
			case EVENT_TYPE_BUTTON:
				// stylus hovering
				if (ev_pkt.button == -1)
					send_event(device, EV_KEY, BTN_TOOL_PEN, ev_pkt.down);
				// stylus touching
				if (ev_pkt.button == 0)
					send_event(device, EV_KEY, BTN_TOUCH, ev_pkt.down);
				// button 1
				if (ev_pkt.button == 1)
					send_event(device, EV_KEY, BTN_STYLUS, ev_pkt.down);
				// button 2
				if (ev_pkt.button == 2)
					send_event(device, EV_KEY, BTN_STYLUS2, ev_pkt.down);
				
		#ifdef DEBUG
				printf("sent button: %hhi, %hhu\n", ev_pkt.button, ev_pkt.down);
		#endif
				send_event(device, EV_SYN, SYN_REPORT, 1);
				break;

		}
	}
	close(udp_socket);

	printf("Removing network tablet from device list\n");
	ioctl(device, UI_DEV_DESTROY);
	close(device);

	printf("GfxTablet driver shut down gracefully\n");
	return 0;
}
