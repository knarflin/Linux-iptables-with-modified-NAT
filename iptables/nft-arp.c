/*
 * (C) 2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2013 by Giuseppe Longo <giuseppelng@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <net/if_arp.h>

#include <xtables.h>
#include <libiptc/libxtc.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>

#include <linux/netfilter_arp/arp_tables.h>
#include <linux/netfilter/nf_tables.h>

#include "nft-shared.h"
#include "nft.h"

/* a few names */
char *opcodes[] =
{
	"Request",
	"Reply",
	"Request_Reverse",
	"Reply_Reverse",
	"DRARP_Request",
	"DRARP_Reply",
	"DRARP_Error",
	"InARP_Request",
	"ARP_NAK",
};

static char *
addr_to_dotted(const struct in_addr *addrp)
{
	static char buf[20];
	const unsigned char *bytep;

	bytep = (const unsigned char *) &(addrp->s_addr);
	sprintf(buf, "%d.%d.%d.%d", bytep[0], bytep[1], bytep[2], bytep[3]);
	return buf;
}

static char *
addr_to_host(const struct in_addr *addr)
{
	struct hostent *host;

	if ((host = gethostbyaddr((char *) addr,
					sizeof(struct in_addr), AF_INET)) != NULL)
		return (char *) host->h_name;

	return (char *) NULL;
}

static char *
addr_to_network(const struct in_addr *addr)
{
	struct netent *net;

	if ((net = getnetbyaddr((long) ntohl(addr->s_addr), AF_INET)) != NULL)
		return (char *) net->n_name;

	return (char *) NULL;
}

static char *
addr_to_anyname(const struct in_addr *addr)
{
	char *name;

	if ((name = addr_to_host(addr)) != NULL ||
		(name = addr_to_network(addr)) != NULL)
		return name;

	return addr_to_dotted(addr);
}

static char *
mask_to_dotted(const struct in_addr *mask)
{
	int i;
	static char buf[20];
	u_int32_t maskaddr, bits;

	maskaddr = ntohl(mask->s_addr);

	if (maskaddr == 0xFFFFFFFFL)
		/* we don't want to see "/32" */
		return "";

	i = 32;
	bits = 0xFFFFFFFEL;
	while (--i >= 0 && maskaddr != bits)
		bits <<= 1;
	if (i >= 0)
		sprintf(buf, "/%d", i);
	else
		/* mask was not a decent combination of 1's and 0's */
		sprintf(buf, "/%s", addr_to_dotted(mask));

	return buf;
}

static void print_mac(const unsigned char *mac, int l)
{
	int j;

	for (j = 0; j < l; j++)
		printf("%02x%s", mac[j],
			(j==l-1) ? "" : ":");
}

static void print_mac_and_mask(const unsigned char *mac, const unsigned char *mask, int l)
{
	int i;

	print_mac(mac, l);
	for (i = 0; i < l ; i++)
		if (mask[i] != 255)
			break;
	if (i == l)
		return;
	printf("/");
	print_mac(mask, l);
}

static uint8_t arpt_to_ipt_flags(uint16_t invflags)
{
	uint8_t result = 0;

	if (invflags & ARPT_INV_VIA_IN)
		result |= IPT_INV_VIA_IN;

	if (invflags & ARPT_INV_VIA_OUT)
		result |= IPT_INV_VIA_OUT;

	if (invflags & ARPT_INV_SRCIP)
		result |= IPT_INV_SRCIP;

	if (invflags & ARPT_INV_TGTIP)
		result |= IPT_INV_DSTIP;

	if (invflags & ARPT_INV_ARPPRO)
		result |= IPT_INV_PROTO;

	if (invflags & ARPT_INV_MASK)
		result |= IPT_INV_MASK;

	return result;
}

static int nft_arp_add(struct nft_rule *r, void *data)
{
	struct arpt_entry *fw = data;
	uint8_t flags = arpt_to_ipt_flags(fw->arp.invflags);
	struct xt_entry_target *t;
	char *targname;
	int ret;

	if (fw->arp.iniface[0] != '\0')
		add_iniface(r, fw->arp.iniface, flags);

	if (fw->arp.outiface[0] != '\0')
		add_outiface(r, fw->arp.outiface, flags);

	if (fw->arp.arhrd != 0) {
		add_payload(r, offsetof(struct arphdr, ar_hrd), 2);
		add_cmp_u16(r, fw->arp.arhrd, NFT_CMP_EQ);
	}

	if (fw->arp.arpro != 0) {
	        add_payload(r, offsetof(struct arphdr, ar_pro), 2);
		add_cmp_u16(r, fw->arp.arpro, NFT_CMP_EQ);
	}

	if (fw->arp.arhln != 0)
		add_proto(r, offsetof(struct arphdr, ar_hln), 1,
			  fw->arp.arhln, flags);

	add_proto(r, offsetof(struct arphdr, ar_pln), 1, 4, 0);

	if (fw->arp.arpop != 0) {
		add_payload(r, offsetof(struct arphdr, ar_op), 2);
		add_cmp_u16(r, fw->arp.arpop, NFT_CMP_EQ);
	}

	if (fw->arp.src_devaddr.addr[0] != '\0') {
		add_payload(r, sizeof(struct arphdr), fw->arp.arhln);
		add_cmp_ptr(r, NFT_CMP_EQ, fw->arp.src_devaddr.addr, fw->arp.arhln);
	}

	if (fw->arp.src.s_addr != 0)
		add_addr(r, sizeof(struct arphdr) + fw->arp.arhln,
			 &fw->arp.src.s_addr, 4, flags);

	if (fw->arp.tgt_devaddr.addr[0] != '\0') {
		add_payload(r, sizeof(struct arphdr) + fw->arp.arhln + 4, fw->arp.arhln);
		add_cmp_ptr(r, NFT_CMP_EQ, fw->arp.tgt_devaddr.addr, fw->arp.arhln);
	}

	if (fw->arp.tgt.s_addr != 0)
		add_addr(r, sizeof(struct arphdr) + fw->arp.arhln + sizeof(struct in_addr),
			 &fw->arp.tgt.s_addr, 4, flags);

	/* Counters need to me added before the target, otherwise they are
	 * increased for each rule because of the way nf_tables works.
	 */
	if (add_counters(r, fw->counters.pcnt, fw->counters.bcnt) < 0)
		return -1;

	t = nft_arp_get_target(fw);
	targname = t->u.user.name;

	/* Standard target? */
	if (strcmp(targname, XTC_LABEL_ACCEPT) == 0)
		ret = add_verdict(r, NF_ACCEPT);
	else if (strcmp(targname, XTC_LABEL_DROP) == 0)
		ret = add_verdict(r, NF_DROP);
	else if (strcmp(targname, XTC_LABEL_RETURN) == 0)
		ret = add_verdict(r, NFT_RETURN);
	else if (xtables_find_target(targname, XTF_TRY_LOAD) != NULL)
		ret = add_target(r, t);
	else
		ret = add_jumpto(r, targname, NFT_JUMP);

	return ret;
}

static uint16_t ipt_to_arpt_flags(uint8_t invflags)
{
	uint16_t result = 0;

	if (invflags & IPT_INV_VIA_IN)
		result |= ARPT_INV_VIA_IN;

	if (invflags & IPT_INV_VIA_OUT)
		result |= ARPT_INV_VIA_OUT;

	if (invflags & IPT_INV_SRCIP)
		result |= ARPT_INV_SRCIP;

	if (invflags & IPT_INV_DSTIP)
		result |= ARPT_INV_TGTIP;

	if (invflags & IPT_INV_PROTO)
		result |= ARPT_INV_ARPPRO;

	if (invflags & IPT_INV_MASK)
		result |= ARPT_INV_MASK;

	return result;
}

static void nft_arp_parse_meta(struct nft_rule_expr *e, uint8_t key,
			       void *data)
{
	struct arpt_entry *fw = data;
	uint8_t flags = 0;

	parse_meta(e, key, fw->arp.iniface, fw->arp.iniface_mask,
		   fw->arp.outiface, fw->arp.outiface_mask,
		   &flags);

	fw->arp.invflags |= ipt_to_arpt_flags(flags);
}

static void nft_arp_parse_target(struct xtables_target *target, void *data)
{
	struct arpt_entry *fw = data;
	struct xt_entry_target **t;

	fw->target_offset = offsetof(struct arpt_entry, elems);
	fw->next_offset = fw->target_offset + target->t->u.target_size;

	t = (void *) &fw->elems;
	*t = target->t;
}

static void nft_arp_parse_immediate(const char *jumpto, bool nft_goto,
				    void *data)
{
	struct xtables_target *target;
	size_t size;

	target = xtables_find_target(XT_STANDARD_TARGET,
				     XTF_LOAD_MUST_SUCCEED);

	size = XT_ALIGN(sizeof(struct xt_entry_target)) + target->size;

	target->t = xtables_calloc(1, size);
	target->t->u.target_size = size;
	strcpy(target->t->u.user.name, jumpto);
	target->t->u.user.revision = target->revision;

	nft_arp_parse_target(target, data);
}

static void nft_arp_parse_payload(struct nft_rule_expr_iter *iter,
				  uint32_t offset, void *data)
{
	struct arpt_entry *fw = data;
	struct in_addr addr;
	unsigned short int ar_hrd, ar_pro, ar_op, ar_hln;
	bool inv;

	switch (offset) {
	case offsetof(struct arphdr, ar_hrd):
		get_cmp_data(iter, &ar_hrd, sizeof(ar_hrd), &inv);
		fw->arp.arhrd = ar_hrd;
		fw->arp.arhrd_mask = 0xffff;
		if (inv)
			fw->arp.invflags |= ARPT_INV_ARPHRD;
		break;
	case offsetof(struct arphdr, ar_pro):
		get_cmp_data(iter, &ar_pro, sizeof(ar_pro), &inv);
		fw->arp.arpro = ar_pro;
		fw->arp.arpro_mask = 0xffff;
		if (inv)
			fw->arp.invflags |= ARPT_INV_ARPPRO;
		break;
	case offsetof(struct arphdr, ar_op):
		get_cmp_data(iter, &ar_op, sizeof(ar_op), &inv);
		fw->arp.arpop = ar_op;
		fw->arp.arpop_mask = 0xffff;
		if (inv)
			fw->arp.invflags |= ARPT_INV_ARPOP;
		break;
	case offsetof(struct arphdr, ar_hln):
		get_cmp_data(iter, &ar_hln, sizeof(ar_op), &inv);
		fw->arp.arhln = ar_hln;
		fw->arp.arhln_mask = 0xff;
		if (inv)
			fw->arp.invflags |= ARPT_INV_ARPOP;
		break;
	default:
		if (fw->arp.arhln < 0)
			break;

		if (offset == sizeof(struct arphdr) + fw->arp.arhln) {
			get_cmp_data(iter, &addr, sizeof(addr), &inv);
			fw->arp.src.s_addr = addr.s_addr;
			fw->arp.smsk.s_addr = 0xffffffff;
			if (inv)
				fw->arp.invflags |= ARPT_INV_SRCIP;
		} else if (offset == sizeof(struct arphdr) + fw->arp.arhln + sizeof(struct in_addr)) {
			get_cmp_data(iter, &addr, sizeof(addr), &inv);
			fw->arp.tgt.s_addr = addr.s_addr;
			fw->arp.tmsk.s_addr = 0xffffffff;
			if (inv)
				fw->arp.invflags |= ARPT_INV_TGTIP;
		}
		break;
	}
}

void nft_rule_to_arpt_entry(struct nft_rule *r, struct arpt_entry *fw)
{
	struct nft_rule_expr_iter *iter;
	struct nft_rule_expr *expr;
	int family = nft_rule_attr_get_u32(r, NFT_RULE_ATTR_FAMILY);

	iter = nft_rule_expr_iter_create(r);
	if (iter == NULL)
		return;

	expr = nft_rule_expr_iter_next(iter);
	while (expr != NULL) {
		const char *name =
			nft_rule_expr_get_str(expr, NFT_RULE_EXPR_ATTR_NAME);

		if (strcmp(name, "counter") == 0)
			nft_parse_counter(expr, iter, &fw->counters);
		else if (strcmp(name, "payload") == 0)
			nft_parse_payload(expr, iter, family, fw);
		else if (strcmp(name, "meta") == 0)
			nft_parse_meta(expr, iter, family, fw);
		else if (strcmp(name, "immediate") == 0)
			nft_parse_immediate(expr, iter, family, fw);
		else if (strcmp(name, "target") == 0)
			nft_parse_target(expr, iter, family, fw);

		expr = nft_rule_expr_iter_next(iter);
	}

	nft_rule_expr_iter_destroy(iter);
}

static struct xtables_target
*get_target(struct arpt_entry *fw, unsigned int format)
{
	const char *targname;
	struct xtables_target *target = NULL;
	const struct xt_entry_target *t;

	if (!fw->target_offset)
		return NULL;

	t = nft_arp_get_target(fw);
	targname = t->u.user.name;
	target = xtables_find_target(targname, XTF_TRY_LOAD);
	if (!(format & FMT_NOTARGET))
		printf("-j %s ", targname);

	return target;
}

static void print_fw_details(struct arpt_entry *fw, unsigned int format)
{
	char buf[BUFSIZ];
	char iface[IFNAMSIZ+2];
	int print_iface = 0;
	int i;

	iface[0] = '\0';

	if (fw->arp.iniface[0] != '\0') {
		strcat(iface, fw->arp.iniface);
		print_iface = 1;
	}
	else if (format & FMT_VIA) {
		print_iface = 1;
		if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
	}
	if (print_iface)
		printf("%s-i %s ", fw->arp.invflags & ARPT_INV_VIA_IN ?
				   "! " : "", iface);

	print_iface = 0;
	iface[0] = '\0';

	if (fw->arp.outiface[0] != '\0') {
		strcat(iface, fw->arp.outiface);
		print_iface = 1;
	}
	else if (format & FMT_VIA) {
		print_iface = 1;
		if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
	}
	if (print_iface)
		printf("%s-o %s ", fw->arp.invflags & ARPT_INV_VIA_OUT ?
				   "! " : "", iface);

	if (fw->arp.smsk.s_addr != 0L) {
		printf("%s", fw->arp.invflags & ARPT_INV_SRCIP
			? "! " : "");
		if (format & FMT_NUMERIC)
			sprintf(buf, "%s", addr_to_dotted(&(fw->arp.src)));
		else
			sprintf(buf, "%s", addr_to_anyname(&(fw->arp.src)));
		strncat(buf, mask_to_dotted(&(fw->arp.smsk)),
			sizeof(buf) - strlen(buf) - 1);
		printf("-s %s ", buf);
	}

	for (i = 0; i < ARPT_DEV_ADDR_LEN_MAX; i++)
		if (fw->arp.src_devaddr.mask[i] != 0)
			break;
	if (i == ARPT_DEV_ADDR_LEN_MAX)
		goto after_devsrc;
	printf("%s", fw->arp.invflags & ARPT_INV_SRCDEVADDR
		? "! " : "");
	printf("--src-mac ");
	print_mac_and_mask((unsigned char *)fw->arp.src_devaddr.addr,
		(unsigned char *)fw->arp.src_devaddr.mask, ETH_ALEN);
	printf(" ");
after_devsrc:

	if (fw->arp.tmsk.s_addr != 0L) {
		printf("%s", fw->arp.invflags & ARPT_INV_TGTIP
			? "! " : "");
		if (format & FMT_NUMERIC)
			sprintf(buf, "%s", addr_to_dotted(&(fw->arp.tgt)));
		else
			sprintf(buf, "%s", addr_to_anyname(&(fw->arp.tgt)));
		strncat(buf, mask_to_dotted(&(fw->arp.tmsk)),
			sizeof(buf) - strlen(buf) - 1);
		printf("-d %s ", buf);
	}

	for (i = 0; i <ARPT_DEV_ADDR_LEN_MAX; i++)
		if (fw->arp.tgt_devaddr.mask[i] != 0)
			break;
	if (i == ARPT_DEV_ADDR_LEN_MAX)
		goto after_devdst;
	printf("%s", fw->arp.invflags & ARPT_INV_TGTDEVADDR
		? "! " : "");
	printf("--dst-mac ");
	print_mac_and_mask((unsigned char *)fw->arp.tgt_devaddr.addr,
		(unsigned char *)fw->arp.tgt_devaddr.mask, ETH_ALEN);
	printf(" ");

after_devdst:

	if (fw->arp.arhln_mask != 0) {
		printf("%s", fw->arp.invflags & ARPT_INV_ARPHLN
			? "! " : "");
		printf("--h-length %d", fw->arp.arhln);
		if (fw->arp.arhln_mask != 255)
			printf("/%d", fw->arp.arhln_mask);
		printf(" ");
	}

	if (fw->arp.arpop_mask != 0) {
		int tmp = ntohs(fw->arp.arpop);

		printf("%s", fw->arp.invflags & ARPT_INV_ARPOP
			? "! " : "");
		if (tmp <= NUMOPCODES && !(format & FMT_NUMERIC))
			printf("--opcode %s", opcodes[tmp-1]);
		else

		if (fw->arp.arpop_mask != 65535)
			printf("/%d", ntohs(fw->arp.arpop_mask));
		printf(" ");
	}

	if (fw->arp.arhrd_mask != 0) {
		uint16_t tmp = ntohs(fw->arp.arhrd);

		printf("%s", fw->arp.invflags & ARPT_INV_ARPHRD
			? "! " : "");
		if (tmp == 1 && !(format & FMT_NUMERIC))
			printf("--h-type %s", "Ethernet");
		else
			printf("--h-type %u", tmp);
		if (fw->arp.arhrd_mask != 65535)
			printf("/%d", ntohs(fw->arp.arhrd_mask));
		printf(" ");
	}

	if (fw->arp.arpro_mask != 0) {
		int tmp = ntohs(fw->arp.arpro);

		printf("%s", fw->arp.invflags & ARPT_INV_ARPPRO
			? "! " : "");
		if (tmp == 0x0800 && !(format & FMT_NUMERIC))
			printf("--proto-type %s", "IPv4");
		else
			printf("--proto-type 0x%x", tmp);
		if (fw->arp.arpro_mask != 65535)
			printf("/%x", ntohs(fw->arp.arpro_mask));
		printf(" ");
	}
}

static void
nft_arp_print_firewall(struct nft_rule *r, unsigned int num,
		       unsigned int format)
{
	struct arpt_entry fw = {};
	struct xtables_target *target = NULL;
	const struct xt_entry_target *t = NULL;

	nft_rule_to_arpt_entry(r, &fw);

	if (format & FMT_LINENUMBERS)
		printf("%u ", num);

	target = get_target(&fw, format);
	print_fw_details(&fw, format);

	if (target) {
		if (target->print)
			/* Print the target information. */
			target->print(&fw.arp, t, format & FMT_NUMERIC);
	}

	if (!(format & FMT_NOCOUNTS)) {
		printf(", pcnt=");
		xtables_print_num(fw.counters.pcnt, format);
		printf("-- bcnt=");
		xtables_print_num(fw.counters.bcnt, format);
	}

	if (!(format & FMT_NONEWLINE))
		fputc('\n', stdout);
}

static void nft_arp_save_firewall(const void *data,
				  unsigned int format)
{
	const struct arpt_entry *fw = data;
	struct xtables_target *target = NULL;
	const struct xt_entry_target *t = NULL;

	print_fw_details((struct arpt_entry *)fw, format);

	if (!(format & FMT_NOCOUNTS)) {
		printf("-c ");
		xtables_print_num(fw->counters.pcnt, format);
		xtables_print_num(fw->counters.bcnt, format);
	}

	target = get_target((struct arpt_entry *)fw, format);

	if (target) {
		if (target->print)
			/* Print the target information. */
			target->print(&fw->arp, t, format & FMT_NUMERIC);
	}
	printf("\n");
}

static bool nft_arp_is_same(const void *data_a,
			    const void *data_b)
{
	const struct arpt_entry *a = data_a;
	const struct arpt_entry *b = data_b;

	if (a->arp.src.s_addr != b->arp.src.s_addr
	    || a->arp.tgt.s_addr != b->arp.tgt.s_addr
	    || a->arp.smsk.s_addr != b->arp.tmsk.s_addr
	    || a->arp.arpro != b->arp.arpro
	    || a->arp.flags != b->arp.flags
	    || a->arp.invflags != b->arp.invflags) {
		DEBUGP("different src/dst/proto/flags/invflags\n");
		return false;
	}

	return is_same_interfaces(a->arp.iniface,
				  a->arp.outiface,
				  (unsigned char *)a->arp.iniface_mask,
				  (unsigned char *)a->arp.outiface_mask,
				  b->arp.iniface,
				  b->arp.outiface,
				  (unsigned char *)b->arp.iniface_mask,
				  (unsigned char *)b->arp.outiface_mask);
}

static bool nft_arp_rule_find(struct nft_family_ops *ops, struct nft_rule *r,
			      void *data)
{
	struct arpt_entry *fw = data;
	struct xt_entry_target *t_fw, *t_this;
	char *targname_fw, *targname_this;
	struct arpt_entry this = {};

	/* Delete by matching rule case */
	nft_rule_to_arpt_entry(r, &this);

	if (!ops->is_same(fw, &this))
		return false;

	t_fw = nft_arp_get_target(fw);
	t_this = nft_arp_get_target(&this);

	targname_fw = t_fw->u.user.name;
	targname_this = t_this->u.user.name;

	if (!strcmp(targname_fw, targname_this) &&
	    (!strcmp(targname_fw, "mangle") ||
	    !strcmp(targname_fw, "CLASSIFY"))) {
		if (memcmp(t_fw->data, t_this->data,
		    t_fw->u.user.target_size - sizeof(*t_fw)) != 0) {
			DEBUGP("Different target\n");
			return false;
		}
		return true;
	}

	if (strcmp(targname_fw, targname_this) != 0) {
		DEBUGP("Different verdict\n");
		return false;
	}

	return true;
}

struct nft_family_ops nft_family_ops_arp = {
	.add			= nft_arp_add,
	.is_same		= nft_arp_is_same,
	.print_payload		= NULL,
	.parse_meta		= nft_arp_parse_meta,
	.parse_payload		= nft_arp_parse_payload,
	.parse_immediate	= nft_arp_parse_immediate,
	.print_firewall		= nft_arp_print_firewall,
	.save_firewall		= nft_arp_save_firewall,
	.post_parse		= NULL,
	.rule_find		= nft_arp_rule_find,
	.parse_target		= nft_arp_parse_target,
};
