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
	int port = 123, quiet = 0, unsync = 0, bogus = 0, mode = 4;
	int sd, c;

	while ((c = getopt(argc, argv, "p:k:m:bqu")) != -1) {
		switch (c) {
		case 'p': port   = atoi(optarg); break;
		case 'k': kod    = optarg;       break;
		case 'b': bogus  = 1;            break;
		case 'm': mode   = atoi(optarg); break;
		case 'q': quiet  = 1;            break;
		case 'u': unsync = 1;            break;
		default:
			fprintf(stderr, "usage: fake-ntpd [-p PORT] [-k CODE]"
				" [-m MODE] [-b] [-q] [-u]\n");
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

		if (quiet)
			continue;

		secs = (uint32_t)time(NULL) + JAN_1970;

		/* li:0 vn:4 mode:4 (server), li:3 says unsynchronised, which
		 * is a server answering while its own clock is no good --
		 * a GPS receiver that has not got a fix yet.  -m sends some
		 * other mode, which no server would, to stand in for one that
		 * is broken rather than refusing us. */
		buf[0] = (unsync ? 3 : 0) << 6 | 4 << 3 | (mode & 7);
		buf[1] = kod ? 0 : 1;		/* stratum, 0 signals KoD */
		buf[2] = 4;			/* poll */
		buf[3] = (unsigned char)-9;	/* precision */
		memset(&buf[4], 0, 8);		/* root delay, dispersion */
		memcpy(&buf[12], kod ? kod : "LOCL", 4);

		/* origin := client transmit, or zero to answer a question
		 * nobody asked, the way a spoofed or replayed packet does */
		if (bogus)
			memset(&buf[24], 0, 8);
		else
			memcpy(&buf[24], &buf[40], 8);
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
