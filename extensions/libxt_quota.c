/*
 * Shared library add-on to iptables to add quota support
 *
 * Sam Johnston <samj@samj.net>
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <xtables.h>

#include <linux/netfilter/xt_quota.h>

static const struct option opts[] = {
        {"quota", 1, 0, '1'},
        {0}
};

/* print usage */
static void
help(void)
{
        printf("quota options:\n"
               " --quota quota			quota (bytes)\n" "\n");
}

/* print matchinfo */
static void
print(const void *ip, const struct xt_entry_match *match, int numeric)
{
        struct xt_quota_info *q = (struct xt_quota_info *) match->data;
        printf("quota: %llu bytes", (unsigned long long) q->quota);
}

/* save matchinfo */
static void
save(const void *ip, const struct xt_entry_match *match)
{
        struct xt_quota_info *q = (struct xt_quota_info *) match->data;
        printf("--quota %llu ", (unsigned long long) q->quota);
}

/* parse quota option */
static int
parse_quota(const char *s, u_int64_t * quota)
{
        *quota = strtoull(s, (char **) NULL, 10);

#ifdef DEBUG_XT_QUOTA
        printf("Quota: %llu\n", *quota);
#endif

        if (*quota == -1)
                exit_error(PARAMETER_PROBLEM, "quota invalid: '%s'\n", s);
        else
                return 1;
}

/* parse all options, returning true if we found any for us */
static int
parse(int c, char **argv, int invert, unsigned int *flags,
      const void *entry,
      struct xt_entry_match **match)
{
        struct xt_quota_info *info = (struct xt_quota_info *) (*match)->data;

        switch (c) {
        case '1':
                if (check_inverse(optarg, &invert, NULL, 0))
                        exit_error(PARAMETER_PROBLEM, "quota: unexpected '!'");
                if (!parse_quota(optarg, &info->quota))
                        exit_error(PARAMETER_PROBLEM,
                                   "bad quota: '%s'", optarg);
                break;

        default:
                return 0;
        }
        return 1;
}

/* no final check */
static void
final_check(unsigned int flags)
{
}

struct xtables_match quota = { 
	.family		= AF_INET,
	.name		= "quota",
	.version	= IPTABLES_VERSION,
	.size		= XT_ALIGN(sizeof (struct xt_quota_info)),
	.userspacesize	= offsetof(struct xt_quota_info, quota),
	.help		= &help,
	.parse		= &parse,
	.final_check	= &final_check,
	.print		= &print,
	.save		= &save,
	.extra_opts	= opts
};

struct xtables_match quota6 = { 
	.family		= AF_INET6,
	.name		= "quota",
	.version	= IPTABLES_VERSION,
	.size		= XT_ALIGN(sizeof (struct xt_quota_info)),
	.userspacesize	= offsetof(struct xt_quota_info, quota),
	.help		= &help,
	.parse		= &parse,
	.final_check	= &final_check,
	.print		= &print,
	.save		= &save,
	.extra_opts	= opts
};

void
_init(void)
{
	xtables_register_match(&quota);
	xtables_register_match(&quota6);
}