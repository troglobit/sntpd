ToDo
====

- Ask on newsgroup about making sanity checks mandatory, default, or optional  
   - Rob Landley says disable checks with CONFIG to save space  
     good to know why something fails
   - Denis Vlasenko recommends not checking UDP source, since that can  
     break on multihomed server, confirmed for ntpd
   - Denis also says "Give a message. You may make it configurable:  
     `ntpclient: dropped malformed packet[: <optionally what's wrong>]`
- Ask for pre-release comments from Walter Harms and others
- Exit without warning if -f is the only option
- Leap second, crib from openntpd?  worthless!  Look at wikipedia "unix time" article
- Test handle laptop suspend?
- Drift file - read on startup, save on SIGxxxx
- `usage()`'s synopsis lists `-V` (uppercase) but the actual option is
  `-v` (lowercase).  One character, pre-existing, worth a follow-up fix
- Not planned: `pool` association type and a config file.  Several
  servers are handled as a failover list, see sntpd(8); the RFC 5905
  selection algorithms are out of scope for SNTP.

Known gaps in the server list
------------------------------

- Randomize the transmit timestamp, RFC 4330 section 3.  The
  origin-timestamp gate that stops a forged Kiss-o'-Death from retiring
  a server rests on `ntpc_gettime()`: wall-clock time at microsecond
  resolution, roughly 20 guessable bits plus the source port.  What it
  protects is permanent and unrevivable, so that is not enough margin.
- Two entries both marked `prefer`: the first one silently wins.  A
  startup error would be more honest than a silent pick.
- `-t` still accepts a forged stratum-0 reply as a time sample when the
  packet's mode does not also say it is a server reply, since the
  Kiss-o'-Death check and the mode check that would otherwise catch it
  are the two things `-t` was told to skip.
- The dropped-packet log names the mode check `MODE!=3`, but the check
  is `mode != 4`.  Wrong string, and now reachable in daemon logs too.
- `server_init()` ignores `setup_receive()`'s failure and hands back
  the socket anyway, leaving an unbound descriptor sitting in the
  select set.

