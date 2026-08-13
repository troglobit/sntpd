/* Controllable NTP responder for the sntpd test suite. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define JAN_1970 0x83aa7e80

int main(int argc, char *argv[])
{
	struct sockaddr_storage ss;
	struct sockaddr_in sin;
	unsigned char buf[128];
	const char *kod = NULL;
	int port = 123, answer = -1, quiet = 0;
	int sd, c;

	while ((c = getopt(argc, argv, "p:s:k:q")) != -1) {
		switch (c) {
		case 'p': port   = atoi(optarg); break;
		case 's': answer = atoi(optarg); break;
		case 'k': kod    = optarg;       break;
		case 'q': quiet  = 1;            break;
		default:
			fprintf(stderr, "usage: fake-ntpd [-p PORT] [-s N] [-k CODE] [-q]\n");
			return 1;
		}
	}

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd == -1)
		return 1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family      = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port        = htons(port);
	if (bind(sd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		perror("bind");
		return 1;
	}

	/* Tell the harness we are ready to serve. */
	puts("ready");
	fflush(stdout);

	while (1) {
		socklen_t len = sizeof(ss);
		uint32_t secs;
		ssize_t num;

		num = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&ss, &len);
		if (num < 48)
			continue;

		if (quiet || answer == 0)
			continue;
		if (answer > 0)
			answer--;

		secs = (uint32_t)time(NULL) + JAN_1970;

		/* li:0 vn:4 mode:4 (server) */
		buf[0] = 0 << 6 | 4 << 3 | 4;
		buf[1] = kod ? 0 : 1;		/* stratum, 0 signals KoD */
		buf[2] = 4;			/* poll */
		buf[3] = (unsigned char)-9;	/* precision */
		memset(&buf[4], 0, 8);		/* root delay, dispersion */
		memcpy(&buf[12], kod ? kod : "LOCL", 4);

		memcpy(&buf[24], &buf[40], 8);	/* origin  := client transmit */
		memset(&buf[16], 0, 8);		/* reference */
		secs = htonl(secs);
		memcpy(&buf[32], &secs, 4);	/* receive  */
		memcpy(&buf[40], &secs, 4);	/* transmit */

		sendto(sd, buf, 48, 0, (struct sockaddr *)&ss, len);
	}
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
