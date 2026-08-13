/* Unit tests for the sntpd server argument parser. */
#include <stdio.h>
#include <string.h>

#include "peer.h"

/* logit.c refers to this; sntpd.c is not linked into this test binary. */
const char *prognm = "test-parse";

static int fails;

static void check(const char *what, int cond)
{
	printf("%s %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond)
		fails++;
}

static struct ntp_server *add(const char *arg)
{
	if (peer_add(arg))
		return NULL;

	return peer_get(peer_count() - 1);
}

int main(void)
{
	struct ntp_server *s;

	s = add("time.example.com");
	check("plain host", s && !strcmp(s->host, "time.example.com"));
	check("plain host default port", s && s->port == NTP_PORT);

	s = add("time.example.com:1123");
	check("host with port", s && !strcmp(s->host, "time.example.com"));
	check("host port value", s && s->port == 1123);

	s = add("2001:db8::1");
	check("bare v6 literal", s && !strcmp(s->host, "2001:db8::1"));
	check("bare v6 default port", s && s->port == NTP_PORT);

	s = add("[2001:db8::1]:1123");
	check("bracketed v6", s && !strcmp(s->host, "2001:db8::1"));
	check("bracketed v6 port", s && s->port == 1123);

	s = add("[2001:db8::1]");
	check("bracketed v6 no port", s && !strcmp(s->host, "2001:db8::1"));

	s = add("host,iburst");
	check("iburst flag", s && s->iburst && !s->prefer);

	s = add("host,prefer");
	check("prefer flag", s && s->prefer && !s->iburst);

	s = add("host:1123,iburst,prefer");
	check("port and both flags", s && s->port == 1123 && s->iburst && s->prefer);

	/*
	 * Eight successful adds above is exactly MAX_SERVERS, so start
	 * over: otherwise the overflow guard rejects these and the test
	 * passes for the wrong reason.
	 */
	peer_reset();
	check("unknown option rejected", peer_add("host,bogus") == -1);
	check("unclosed bracket rejected", peer_add("[2001:db8::1") == -1);
	check("empty host rejected", peer_add("") == -1);
	check("rejected entries are not stored", peer_count() == 0);

	while (peer_count() < MAX_SERVERS)
		peer_add("filler");
	check("overflow rejected", peer_add("one.too.many") == -1);
	check("count capped", peer_count() == MAX_SERVERS);

	return fails ? 1 : 0;
}
