/*
 * Author: Paul.Russell@rustcorp.com.au and mneuling@radlogic.com.au
 *
 * Based on the ipchains code by Paul Russell and Michael Neuling
 *
 * (C) 2000-2002 by the netfilter coreteam <coreteam@netfilter.org>:
 * 		    Paul 'Rusty' Russell <rusty@rustcorp.com.au>
 * 		    Marc Boucher <marc+nf@mbsi.ca>
 * 		    James Morris <jmorris@intercode.com.au>
 * 		    Harald Welte <laforge@gnumonks.org>
 * 		    Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 *	arptables -- IP firewall administration for kernels with
 *	firewall table (aimed for the 2.3 kernels)
 *
 *	See the accompanying manual page arptables(8) for information
 *	about proper usage of this program.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <xtables.h>
#include "nft.h"
#include <linux/netfilter_arp/arp_tables.h>

#include "xtables-multi.h"

extern struct xtables_globals xtables_globals;
extern const char *program_version, *program_name;

static const struct xtables_afinfo afinfo_arp = {
        .kmod          = "arp_tables",
        .proc_exists   = "/proc/net/arp_tables_names",
        .libprefix     = "libarp_",
        .family        = NFPROTO_ARP,
        .ipproto       = IPPROTO_IP,
        .so_rev_match  = -1,
        .so_rev_target = -1,
};

int xtables_arp_main(int argc, char *argv[])
{
	int ret;
	char *table = "filter";
	struct nft_handle h = {
		.family = NFPROTO_ARP,
	};

	xtables_globals.program_name = "arptables";
	/* This code below could be replaced by xtables_init_all, which
	 * doesn't support NFPROTO_ARP yet.
	 */
	xtables_init();
	afinfo = &afinfo_arp;
	ret = xtables_set_params(&xtables_globals);
	if (ret < 0) {
		fprintf(stderr, "%s/%s Failed to initialize xtables\n",
				xtables_globals.program_name,
				xtables_globals.program_version);
		exit(1);
	}

#if defined(ALL_INCLUSIVE) || defined(NO_SHARED_LIBS)
	init_extensions();
#endif

	ret = do_commandarp(&h, argc, argv, &table);
	if (ret)
		ret = nft_commit(&h);

	exit(!ret);
}
