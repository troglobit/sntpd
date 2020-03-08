#include "sntpd.h"

int server_init(uint16_t port)
{
	int sd;

	sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sd == -1)
		return -1;

	setup_receive(sd, AF_INET, port);

	return sd;
}

static int validate_request(char *buf, size_t len)
{
	struct ntp *ntp = (struct ntp *)buf;
	int vn, mode;

	if (len < sizeof(struct ntp)) {
		DBG("NTP request too small (%zd)", len);
		return 1;
	}

	/*
	 *  0 1 2 3 4 5 6 7
	 * +-+-+-+-+-+-+-+-+
	 * |LI | VN  |MODE |
	 * +-+-+-+-+-+-+-+-+
	 */
//	li   = (ntp->flags >> 6) & 0x3;
	vn   = (ntp->flags >> 3) & 0x7;
	mode = (ntp->flags >> 0) & 0x7;
//	DBG("flags 0x%01x - li:%d - vn:%d - mode:%d", ntp->flags, li, vn, mode);

	if (mode != 3 && mode != 0) {	/* client */
		DBG("NTP request not a client (%d), flags: 0x%01x", mode, ntp->flags);
		return 1;
	}

	if (vn != 3 && vn != 4) {
		DBG("NTP request has wrong version (%d), flags: 0x%01x", vn, ntp->flags);
		return 1;
	}

	return 0;
}

static size_t compose_reply(struct ntp *ntp, size_t len)
{
	char id[] = "LOCL";

	/* li:0, version:3, mode:4 (server) */
	ntp->flags = 0 << 6 | 3 << 3 | 4 << 0 ;

	ntp->stratum = 1;
	ntp->interval = 4;	/* 16 s */
	ntp->precision = G_precision_exp;

	/* root delay and rooty dispersion */
	ntp->delay      = u2sec(root_delay);
	ntp->dispersion = u2sec(root_dispersion);

	memcpy(ntp->identifier, id, sizeof(ntp->identifier));

	return sizeof(*ntp);
}

int server_recv(int sd)
{
	struct sockaddr_storage ss;
	struct ntptime recv_ts, xmit_ts;
	struct ntp *ntp;
	socklen_t ss_len;
	ssize_t num;
	char buf[128];

	ss_len = sizeof(struct sockaddr_storage);
	num = recvfrom(sd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&ss, &ss_len);
	if (num == -1)
		return -1;

	get_packet_timestamp(sd, &recv_ts);
	if (validate_request(buf, num)) {
		DBG("Validation failed");
		return -1;
	}

	/* Compose NTP message reply */
	ntp = (struct ntp *)buf;
	num = compose_reply(ntp, sizeof(*ntp));

	memcpy(&ntp->origin_ts, &ntp->xmit_ts, sizeof(ntp->origin_ts));

	ntp->recv_ts.coarse   = htonl(recv_ts.coarse);
	ntp->recv_ts.fine     = htonl(recv_ts.fine);

	ntp->refclk_ts.coarse = htonl(peer.last_update_ts.coarse);
	ntp->refclk_ts.fine   = htonl(peer.last_update_ts.fine);

	ntpc_gettime(&xmit_ts);
	ntp->xmit_ts.coarse = htonl(xmit_ts.coarse);
	ntp->xmit_ts.fine   = htonl(xmit_ts.fine);

	return sendto(sd, buf, num, 0, (struct sockaddr *)&ss, ss_len);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
