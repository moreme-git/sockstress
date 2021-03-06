#include "../config.h"

#include "packets.h"
#include "chksum.h"
#include "makepkt.h"

# define TH_FIN         0x01
# define TH_SYN         0x02
# define TH_RST         0x04
# define TH_PUSH        0x08
# define TH_PSH         TH_PUSH
# define TH_ACK         0x10
# define TH_URG         0x20
# define TH_ECE         0x40
# define TH_CWR         0x80

#define PKBUF_SIZE 0xffff
static uint8_t pkt_buf[PKBUF_SIZE];
static size_t pkt_len=0;
static int do_ipchk=0;
static struct myiphdr *_ih;

static int started_at_ethernet=0;

#ifndef TH_PSH
# define TH_PSH TH_PUSH
#endif

void makepkt_clear(void) {

	_ih=NULL;

	memset(pkt_buf, 0x0, sizeof(pkt_buf));
	pkt_len=0; do_ipchk=0;
	started_at_ethernet=0;

	return;
}

int makepkt_getbuf(size_t *len, const uint8_t **buf) {

	if (len == NULL) PANIC("null size pointer in makepkt_get");
	if (buf == NULL) PANIC("buffer pointer null");

	if (_ih != NULL) {
		if (started_at_ethernet) {
			_ih->tot_len=htons(pkt_len - sizeof(struct my6etherheader));
		}
		else {
			_ih->tot_len=htons(pkt_len);
		}
	}

	if (do_ipchk) {
		if (started_at_ethernet) {
			ip_checksum(pkt_buf + sizeof(struct my6etherheader), pkt_len - sizeof(struct my6etherheader));
		}
		else {
			ip_checksum(pkt_buf, pkt_len);
		}
	}

	*len=pkt_len;
	*buf=pkt_buf;

	return 1;
}

int makepkt_build_udp(uint16_t lport, uint16_t rport, uint16_t chksum, const uint8_t *pl, size_t pl_s) {
	struct myudphdr uh;

	/* this still can overflow */
	if (pl_s > (PKBUF_SIZE - sizeof(uh))) {
		return -1;
	}

	if ((pl_s + sizeof(uh)) > (PKBUF_SIZE - (pl_s + sizeof(uh)))) {
		return -1;
	}

	uh.source=htons(lport);
	uh.dest=htons(rport);
	uh.len=ntohs((uint16_t)(pl_s + sizeof(uh)));
	uh.check=chksum;

	if ((pkt_len + (uint16_t)(pl_s + sizeof(uh))) > sizeof(pkt_buf)) {
		return -1;
	}

	memcpy(&pkt_buf[pkt_len], &uh, sizeof(uh));
	pkt_len += sizeof(uh);

	if (pl_s) {
		if (pl == NULL) PANIC("payload buffer is NULL with non-zero length");
		memcpy(&pkt_buf[pkt_len], pl, pl_s);
		pkt_len += (uint16_t)pl_s;
	}

	return 1;
}

int makepkt_build_tcp(uint16_t lport, uint16_t rport, uint16_t chksum, uint32_t seq, uint32_t ackseq, uint16_t tflgs,
	uint16_t window_size, uint16_t urgp, const uint8_t *tcpopts, size_t tcpopts_s, const uint8_t *pl, size_t pl_s) {
	struct mytcphdr th;
	size_t save_len=0;

	/* XXX overflows */

	if (tcpopts_s % 4) {
		PANIC("bad tcp option");
	}
	if (tcpopts_s > 60) {
		PANIC("bad tcp optlen");
	}

	if (pl_s > (PKBUF_SIZE - sizeof(th) - tcpopts_s)) {
		return -1;
	}
	if ((pl_s + sizeof(th) + tcpopts_s) > (PKBUF_SIZE - (pl_s + sizeof(th) + tcpopts_s))) {
		return -1;
	}

	save_len=pkt_len;

	th.source=htons(lport);
	th.dest=htons(rport);
	th.seq=ntohl(seq);
	th.ack_seq=ntohl(ackseq);

	th.res1=0;
	th.doff=(sizeof(th) + tcpopts_s) / 4;
	th.fin=th.syn=th.rst=th.psh=th.ack=th.urg=th.ece=th.cwr=0;
	if (tflgs & TH_FIN) th.fin=1;
	if (tflgs & TH_SYN) th.syn=1;
	if (tflgs & TH_RST) th.rst=1;
	if (tflgs & TH_PSH) th.psh=1;
	if (tflgs & TH_ACK) th.ack=1;
	if (tflgs & TH_URG) th.urg=1;
	if (tflgs & TH_ECE) th.ece=1;
	if (tflgs & TH_CWR) th.cwr=1;

	th.window=htons(window_size);
	th.urg_ptr=htons(urgp);
	th.check=0;

	memcpy(&pkt_buf[pkt_len], &th, sizeof(th));

	assert((pkt_len + sizeof(th)) > pkt_len);
	pkt_len += sizeof(th);

	if (tcpopts_s) {
		if (tcpopts == NULL) PANIC("tcpoption buffer is NULL with non-zero length");
		memcpy(&pkt_buf[pkt_len], tcpopts, tcpopts_s);
		pkt_len += (uint16_t)tcpopts_s;
	}

	if (pl_s) {
		if (pl == NULL) PANIC("payload buffer is NULL with non-zero length");
		memcpy(&pkt_buf[pkt_len], pl, pl_s);
		pkt_len += (uint16_t)pl_s;
	}

	return 1;
}

int makepkt_build_ipv4(uint8_t tos, uint16_t ipid, uint16_t frag, uint8_t ttl, uint8_t proto, uint16_t chksum, uint32_t src,
	uint32_t dst, const uint8_t *ipopts, size_t ipopt_size, const uint8_t *pl, size_t pl_s) {
	struct myiphdr ih;

	if (sizeof(ih) > (PKBUF_SIZE - pkt_len)) {
		PANIC("too much data");
	}

	ih.ihl=5;
	ih.version=4;

	do_ipchk=1;
	ih.tos=tos;
	ih.tot_len=htons(pkt_len + sizeof(ih));
	ih.id=ipid;
	ih.frag_off=htons(frag);
	ih.ttl=ttl;
	ih.protocol=proto;
	ih.check=0;

	ih.saddr=src;
	ih.daddr=dst;

	if (ipopts != NULL || ipopt_size != 0) {
		PANIC("Not implemented");
	}
	if (pl != NULL || pl_s != 0) {
		PANIC("Not Implemented");
	}

	if (_ih == NULL) {
		_ih=(struct myiphdr *)&pkt_buf[pkt_len];
	}

	memcpy(&pkt_buf[pkt_len], &ih, sizeof(ih));
	pkt_len += sizeof(ih);

	return 1;
}

int makepkt_build_arp(uint16_t hwfmt, uint16_t protfmt, uint8_t hwlen, uint8_t protlen, uint16_t opcode, const uint8_t *s_hwaddr, const uint8_t *s_protoaddr, const uint8_t *t_hwaddr, const uint8_t *t_protoaddr) {
	struct myarphdr ma;

	if (s_hwaddr == NULL) PANIC("s_hwaddr null");
	if (s_protoaddr == NULL) PANIC("s_protoaddr null");
	if (t_hwaddr == NULL) PANIC("t_hwaddr null");
	if (t_protoaddr == NULL) PANIC("t_protoaddr null");

	if (hwlen > 16 || protlen > 16) PANIC("ARE YOU SURE YOU REALLY MEAN THIS? <Click Ok To Continue>");

	if ((sizeof(ma) + (hwlen * 2) + (protlen * 2)) > (PKBUF_SIZE - pkt_len)) {
		PANIC("stfu");
	}

	ma.hw_type=htons(hwfmt);
	ma.protocol=htons(protfmt);
	ma.hwsize=hwlen;
	ma.protosize=protlen;
	ma.opcode=htons(opcode);

	memcpy(&pkt_buf[pkt_len], &ma, sizeof(ma));
	pkt_len += sizeof(ma);
	memcpy(&pkt_buf[pkt_len], s_hwaddr, hwlen); pkt_len += hwlen;
	memcpy(&pkt_buf[pkt_len], s_protoaddr, protlen); pkt_len += protlen;
	memcpy(&pkt_buf[pkt_len], t_hwaddr, hwlen); pkt_len += hwlen;
	memcpy(&pkt_buf[pkt_len], t_protoaddr, protlen); pkt_len += protlen;

	return 1;
}

int makepkt_build_ethernet(uint8_t addrlen, const uint8_t *dest, const uint8_t *src, uint16_t type) {
	union {
		uint8_t *ptr;
		uint16_t *hw;
	} b_u;

	if (dest == NULL || src == NULL) {
		PANIC("loser");
	}
	do_ipchk=0;

	if (addrlen > 16) PANIC("ARE YOU SURE YOU REALLY MEAN THIS? <Click Ok To Continue>");

	if ((sizeof(uint16_t) + (addrlen * 2)) > (PKBUF_SIZE - pkt_len)) {
		PANIC("stfu");
	}

	memcpy(&pkt_buf[pkt_len], dest, addrlen); pkt_len += addrlen;
	memcpy(&pkt_buf[pkt_len], src, addrlen); pkt_len += addrlen;

	b_u.ptr=&pkt_buf[pkt_len];
	*b_u.hw=htons(type);
	pkt_len += sizeof(*b_u.hw);

	started_at_ethernet=1;

	return 1;
}
