/* Unit tests for the sntpd failover state machine. */
#include <stdio.h>

#include "peer.h"

const char *prognm = "test-peer";

static int fails;

static void check(const char *what, int cond)
{
	printf("%s %s\n", cond ? "ok  " : "FAIL", what);
	if (!cond)
		fails++;
}

static void miss(struct ntp_server *srv, int n)
{
	while (n--)
		peer_timeout(srv);
}

int main(void)
{
	struct ntp_server *a, *b;

	peer_add("a.example");
	peer_add("b.example");
	a = peer_get(0);
	b = peer_get(1);

	check("first entry is active", peer_active() == a);
	check("a fresh server is not yet condemned", !peer_unreachable(a));
	check("fresh server polls at the cycle time", peer_spacing(a, 600) == 600);

	peer_timeout(a);
	check("one miss does not condemn it", !peer_unreachable(a));
	check("one miss drops to the floor", peer_spacing(a, 600) == MIN_INTERVAL);

	peer_rx(a);
	check("a reply restores the cycle time", peer_spacing(a, 600) == 600);
	check("reach records the reply", a->reach == 0x01);

	peer_rx(a);
	check("reach shifts left", a->reach == 0x03);

	miss(a, BCOUNT);
	check("BCOUNT misses is unreachable", peer_unreachable(a));
	check("reach is clear", a->reach == 0);

	check("rotation moves to b", peer_rotate() == 1);
	check("b is active", peer_active() == b);
	check("a fresh fallback polls at the cycle time", peer_spacing(b, 600) == 600);

	/*
	 * peer_rotate() leaves lost alone, and peer_activate() is what
	 * clears it for the server rotated to; the daemon calls both,
	 * this test only the first.  So a below is not a state sntpd
	 * ever sits in -- it is how the backoff arithmetic is held
	 * still long enough to assert on.
	 */
	miss(b, BCOUNT);
	check("rotation wraps to a", peer_rotate() == 0);
	check("wrapping doubled the backoff", peer_spacing(a, 600) == 2 * MIN_INTERVAL);

	peer_rotate();				/* 0 -> 1, no wrap */
	peer_rotate();				/* 1 -> 0, wraps   */
	check("second wrap doubles again", peer_spacing(a, 600) == 4 * MIN_INTERVAL);
	check("backoff is capped at the poll interval", peer_spacing(a, 20) == 20);

	peer_rx(a);
	check("a reply resets the backoff", peer_spacing(a, 600) == 600);
	peer_timeout(a);
	check("and the floor is back to MIN_INTERVAL", peer_spacing(a, 600) == MIN_INTERVAL);

	/* A retired server is stepped over, not merely skipped once. */
	peer_reset();
	peer_add("one.example");
	peer_add("two.example");
	peer_add("three.example");
	peer_get(1)->dead = 1;
	check("rotation steps over a dead entry", peer_rotate() == 2);

	peer_get(2)->dead = 1;
	peer_get(0)->dead = 1;
	check("every server dead reports -1", peer_rotate() == -1);

	{
		struct ntp_server *k;

		peer_reset();
		peer_add("kod.example");
		peer_add("good.example");
		k = peer_get(0);

		/*
		 * RATE is only ever the backoff, so it needs a miss on the
		 * books to be visible at all: with nothing lost the spacing
		 * is the poll interval no matter what the backoff says.
		 */
		peer_timeout(k);
		check("a miss is at the floor", peer_spacing(k, 600) == MIN_INTERVAL);

		peer_kod(k, "RATE");
		check("RATE keeps the server", !k->dead);
		check("RATE doubles the backoff", peer_spacing(k, 600) == 2 * MIN_INTERVAL);

		/*
		 * With the floor above the backoff -- where a fresh run
		 * starts, and where -m can leave it after the fact --
		 * doubling what is there is not backing off at all.
		 */
		min_interval = 4 * MIN_INTERVAL;
		peer_kod(k, "RATE");
		check("RATE doubles from the floor, not from under it",
		      peer_spacing(k, 600) == 8 * MIN_INTERVAL);
		min_interval = MIN_INTERVAL;

		peer_kod(k, "WXYZ");
		check("an unknown code keeps the server", !k->dead);
		check("an unknown code does not back off",
		      peer_spacing(k, 600) == 8 * MIN_INTERVAL);

		peer_kod(k, "DENY");
		check("DENY retires the server", k->dead);

		peer_kod(peer_get(1), "RSTR");
		check("RSTR retires the server", peer_get(1)->dead);
		check("no server left", peer_rotate() == -1);
	}

	{
		struct ntp_server *p;

		/* A retired server is not somewhere to go back to either. */
		peer_reset();
		peer_add("fallback.example");
		peer_add("chosen.example,prefer");
		p = peer_get(1);

		check("preferred entry is the candidate", peer_candidate() == p);

		/* Two banked replies is the winning count, so the retirement
		 * is the only thing left that can stop the switch. */
		peer_candidate_rx();
		peer_candidate_rx();
		peer_kod(p, "DENY");
		check("a retired preferred entry is no candidate",
		      peer_candidate() == NULL);
		check("and cannot win a switch back", !peer_switchback());
		check("so the fallback stays active", peer_active() == peer_get(0));
	}

	{
		struct ntp_server *c, *d;
		int n, spaced = 0;

		peer_reset();
		peer_add("plain.example");
		peer_add("fast.example,iburst");
		d = peer_get(0);
		c = peer_get(1);

		check("iburst parsed", c->iburst);
		check("iburst off by default", !d->iburst);

		/* Nothing lost yet, so nothing to burst about. */
		check("no burst while answering", peer_spacing(c, 600) == 600);

		peer_timeout(c);
		for (n = 0; n < BCOUNT; n++) {
			if (peer_spacing(c, 600) == BTIME)
				spaced++;
			peer_probed(c);
		}
		check("burst is exactly BCOUNT packets", spaced == BCOUNT);
		check("spacing falls back to the floor",
		      peer_spacing(c, 600) == MIN_INTERVAL);

		miss(c, 3);
		check("further misses do not re-arm", c->burst == 0);

		peer_timeout(d);
		check("a server without iburst never bursts",
		      peer_spacing(d, 600) == MIN_INTERVAL);

		peer_rx(c);			/* lost = 0                  */
		peer_timeout(c);		/* lost = 1, arms the burst  */
		check("burst armed on the first miss", c->burst == BCOUNT);
		peer_rx(c);
		check("a reply disarms a running burst", c->burst == 0);
	}

	{
		struct ntp_server *p, *q;

		peer_reset();
		peer_add("multi.example");
		peer_add("single.example");
		p = peer_get(0);
		q = peer_get(1);

		/* Nothing resolved yet: one candidate per server. */
		check("unresolved server has no addresses", p->naddr == 0);
		check("rotation leaves an unresolved server", peer_rotate() == 1);

		peer_reset();
		peer_add("multi.example");
		peer_add("single.example");
		p = peer_get(0);
		q = peer_get(1);
		peer_naddr(p, 3);
		peer_naddr(q, 1);

		check("starts on the first address", p->addr_idx == 0);
		check("second address is the same server", peer_rotate() == 0);
		check("cursor advanced", p->addr_idx == 1);
		check("third address is still the same server", peer_rotate() == 0);
		check("cursor advanced again", p->addr_idx == 2);

		/*
		 * Three candidates so far and not one lap, so the backoff
		 * has not moved.  It doubles per pass over the servers,
		 * or a four-address name would quadruple it every lap.
		 */
		peer_timeout(p);
		check("stepping through addresses leaves the backoff alone",
		      peer_spacing(p, 600) == MIN_INTERVAL);

		check("exhausted addresses move to the next server", peer_rotate() == 1);
		check("leaving a server rewinds its cursor", p->addr_idx == 0);

		check("single-address server moves on at once", peer_rotate() == 0);
		check("a lap over the servers does double it",
		      peer_spacing(p, 600) == 2 * MIN_INTERVAL);

		/* A dead server is skipped whole, every address of it. */
		peer_naddr(p, 3);
		p->dead = 1;
		check("a dead server is skipped entirely", peer_rotate() == 1);
		check("dead server's cursor is not consulted", p->addr_idx == 0);

		/*
		 * A resolve returning fewer addresses than the last one
		 * has to leave the cursor where the pick would land, and
		 * the pick wraps.
		 */
		p->addr_idx = 2;
		peer_naddr(p, 2);
		check("a shrinking address count wraps the cursor", p->addr_idx == 0);
		p->addr_idx = 3;
		peer_naddr(p, 2);
		check("and wraps it the way the resolver's pick does", p->addr_idx == 1);
	}

	{
		struct ntp_server *p;

		/* Without the keyword the machinery must stay dormant. */
		peer_reset();
		peer_add("a.example");
		peer_add("b.example");
		check("a flat list has no candidate", peer_candidate() == NULL);
		peer_rotate();
		check("and still none after rotating", peer_candidate() == NULL);
		check("nothing to switch back to", !peer_switchback());

		/* Fresh list: second entry marked prefer. */
		peer_reset();
		peer_add("fallback.example");
		peer_add("chosen.example,prefer");

		p = peer_get(1);

		check("candidate is the preferred entry", peer_candidate() == p);
		peer_rotate();				/* now on chosen  */
		check("prefer is active", peer_active() == p);
		check("no candidate while already on it", peer_candidate() == NULL);
		peer_rotate();				/* forced onto fallback */
		check("candidate again once we leave it", peer_candidate() == p);

		peer_candidate_rx();
		check("one reply is not enough", !peer_switchback());
		peer_candidate_rx();
		check("two replies win", peer_switchback());
		check("switched back", peer_active() == p);

		peer_rotate();
		peer_candidate_rx();
		peer_candidate_timeout();
		peer_candidate_rx();
		check("a miss resets the run", !peer_switchback());
	}

	{
		struct ntp_server *p;

		/*
		 * A banked reply must not survive the ring moving.  Three
		 * entries, because the point is a rotation that lands on the
		 * preferred one: from then on there is no candidate, so no
		 * probe, so peer_candidate_timeout() is unreachable and
		 * rotation is the only thing left that can clear the count.
		 */
		peer_reset();
		peer_add("one.example");
		peer_add("two.example,prefer");
		peer_add("three.example");
		p = peer_get(1);

		peer_candidate_rx();		/* one reply banked        */
		peer_rotate();			/* ring lands on prefer    */
		check("no candidate once the ring lands on it", peer_candidate() == NULL);
		peer_rotate();			/* and leaves it again     */
		check("candidate once more", peer_candidate() == p);

		peer_candidate_rx();
		check("a rotation dropped the banked reply", !peer_switchback());
		peer_candidate_rx();
		check("and two fresh replies still win", peer_switchback());
	}

	return fails ? 1 : 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
