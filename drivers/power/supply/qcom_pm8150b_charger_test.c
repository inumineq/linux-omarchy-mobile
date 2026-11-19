// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the PM8150B charger input current limit and charge limit.
 * Included by qcom_pm8150b_charger.c, so the tested helpers can stay static.
 */

#include <kunit/test.h>

struct smb5_input_case {
	enum power_supply_usb_type bc12;
	enum smb5_rp rp;
	int current_ua;
	enum power_supply_usb_type type;
};

static const struct smb5_input_case smb5_input_cases[] = {
	{ POWER_SUPPLY_USB_TYPE_SDP, SMB5_RP_DEFAULT, 500000, POWER_SUPPLY_USB_TYPE_SDP },
	{ POWER_SUPPLY_USB_TYPE_SDP, SMB5_RP_1P5, 1500000, POWER_SUPPLY_USB_TYPE_C },
	{ POWER_SUPPLY_USB_TYPE_SDP, SMB5_RP_3P0, 3000000, POWER_SUPPLY_USB_TYPE_C },
	{ POWER_SUPPLY_USB_TYPE_CDP, SMB5_RP_DEFAULT, 1500000, POWER_SUPPLY_USB_TYPE_CDP },
	{ POWER_SUPPLY_USB_TYPE_CDP, SMB5_RP_1P5, 1500000, POWER_SUPPLY_USB_TYPE_CDP },
	{ POWER_SUPPLY_USB_TYPE_CDP, SMB5_RP_3P0, 3000000, POWER_SUPPLY_USB_TYPE_C },
	{ POWER_SUPPLY_USB_TYPE_DCP, SMB5_RP_DEFAULT, 1500000, POWER_SUPPLY_USB_TYPE_DCP },
	{ POWER_SUPPLY_USB_TYPE_DCP, SMB5_RP_1P5, 1500000, POWER_SUPPLY_USB_TYPE_DCP },
	{ POWER_SUPPLY_USB_TYPE_DCP, SMB5_RP_3P0, 3000000, POWER_SUPPLY_USB_TYPE_C },
	{ POWER_SUPPLY_USB_TYPE_UNKNOWN, SMB5_RP_DEFAULT, 500000, POWER_SUPPLY_USB_TYPE_UNKNOWN },
	{ POWER_SUPPLY_USB_TYPE_UNKNOWN, SMB5_RP_1P5, 1500000, POWER_SUPPLY_USB_TYPE_C },
	{ POWER_SUPPLY_USB_TYPE_UNKNOWN, SMB5_RP_3P0, 3000000, POWER_SUPPLY_USB_TYPE_C },
};

static void smb5_input_case_desc(const struct smb5_input_case *c, char *desc)
{
	static const char * const bc12[] = {
		[POWER_SUPPLY_USB_TYPE_UNKNOWN] = "unknown",
		[POWER_SUPPLY_USB_TYPE_SDP] = "SDP",
		[POWER_SUPPLY_USB_TYPE_DCP] = "DCP",
		[POWER_SUPPLY_USB_TYPE_CDP] = "CDP",
	};
	static const char * const rp[] = { "default", "1.5A", "3.0A" };

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s Rp %s", bc12[c->bc12], rp[c->rp]);
}

KUNIT_ARRAY_PARAM(smb5_input, smb5_input_cases, smb5_input_case_desc);

static void smb5_input_limit_test(struct kunit *test)
{
	const struct smb5_input_case *c = test->param_value;
	struct smb5_input_limit limit = smb5_input_limit(c->bc12, c->rp);

	KUNIT_EXPECT_EQ(test, limit.current_ua, c->current_ua);
	KUNIT_EXPECT_EQ(test, limit.type, c->type);
}

static void smb5_rp_from_snk_status_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(0x00), SMB5_RP_DEFAULT);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_RP_STD_BIT), SMB5_RP_DEFAULT);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_RP_1P5_BIT), SMB5_RP_1P5);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_RP_3P0_BIT), SMB5_RP_3P0);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_RP_SHORT_BIT), SMB5_RP_DEFAULT);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_RP_1P5_BIT | SNK_RP_3P0_BIT),
			SMB5_RP_DEFAULT);
	KUNIT_EXPECT_EQ(test, smb5_rp_from_snk_status(SNK_DAM_3000MA_BIT | SNK_RP_3P0_BIT),
			SMB5_RP_DEFAULT);
}

/* Every value of the status register: only a lone 1.5 A or 3.0 A Rp bit counts. */
static void smb5_rp_sweep_test(struct kunit *test)
{
	unsigned int stat;

	for (stat = 0; stat <= 0xff; stat++) {
		enum smb5_rp want = SMB5_RP_DEFAULT;

		if ((stat & 0x7f) == 0x04)
			want = SMB5_RP_1P5;
		else if ((stat & 0x7f) == 0x02)
			want = SMB5_RP_3P0;
		KUNIT_EXPECT_EQ_MSG(test, smb5_rp_from_snk_status(stat), want,
				    "TYPE_C_SNK_STATUS 0x%02x", stat);
	}
}

/*
 * For every BC1.2 result and Rp level, valid or not: the limit is never
 * below the BC1.2 baseline nor above 3 A, and only a valid 1.5 A or 3.0 A
 * Rp may raise it.
 */
static void smb5_input_limit_bounds_test(struct kunit *test)
{
	static const enum power_supply_usb_type bc12s[] = {
		POWER_SUPPLY_USB_TYPE_UNKNOWN, POWER_SUPPLY_USB_TYPE_SDP,
		POWER_SUPPLY_USB_TYPE_DCP, POWER_SUPPLY_USB_TYPE_CDP,
		POWER_SUPPLY_USB_TYPE_ACA, POWER_SUPPLY_USB_TYPE_C,
		POWER_SUPPLY_USB_TYPE_PD, POWER_SUPPLY_USB_TYPE_PD_PPS,
		(enum power_supply_usb_type)99, (enum power_supply_usb_type)-1,
	};
	static const enum smb5_rp rps[] = {
		SMB5_RP_DEFAULT, SMB5_RP_1P5, SMB5_RP_3P0,
		(enum smb5_rp)3, (enum smb5_rp)-1,
	};
	int i, j;

	for (i = 0; i < ARRAY_SIZE(bc12s); i++) {
		for (j = 0; j < ARRAY_SIZE(rps); j++) {
			enum power_supply_usb_type bc12 = bc12s[i];
			struct smb5_input_limit limit = smb5_input_limit(bc12, rps[j]);
			int base = (bc12 == POWER_SUPPLY_USB_TYPE_CDP ||
				    bc12 == POWER_SUPPLY_USB_TYPE_DCP) ? 1500000 : 500000;
			int want = base;

			if (rps[j] == SMB5_RP_1P5)
				want = max(base, 1500000);
			else if (rps[j] == SMB5_RP_3P0)
				want = 3000000;

			KUNIT_EXPECT_EQ_MSG(test, limit.current_ua, want,
					    "bc12 %d rp %d", bc12, rps[j]);
			KUNIT_EXPECT_GE(test, limit.current_ua, base);
			KUNIT_EXPECT_LE(test, limit.current_ua, 3000000);
			KUNIT_EXPECT_EQ_MSG(test, limit.type,
					    want > base ? POWER_SUPPLY_USB_TYPE_C : bc12,
					    "bc12 %d rp %d", bc12, rps[j]);
		}
	}
}

/* TYPE_C_MISC_STATUS: bit 0 attached, bit 1 orientation, bit 3 debounced, bit 6 source */
static void smb5_cc_from_status_test(struct kunit *test)
{
	static const struct {
		unsigned int misc, snk;
		bool attached, flipped;
		enum smb5_rp rp;
	} cases[] = {
		{ 0x00, 0x02, false, false, SMB5_RP_DEFAULT },
		{ 0x09, 0x02, true, false, SMB5_RP_3P0 },
		{ 0x0b, 0x04, true, true, SMB5_RP_1P5 },
		{ 0x0b, 0x08, true, true, SMB5_RP_DEFAULT },
		{ 0x01, 0x02, false, false, SMB5_RP_DEFAULT },
		{ 0x08, 0x02, false, false, SMB5_RP_DEFAULT },
		{ 0x49, 0x02, false, false, SMB5_RP_DEFAULT },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct smb5_cc cc = smb5_cc_from_status(cases[i].misc, cases[i].snk);

		KUNIT_EXPECT_EQ_MSG(test, cc.attached, cases[i].attached,
				    "misc 0x%02x", cases[i].misc);
		KUNIT_EXPECT_EQ_MSG(test, cc.flipped, cases[i].flipped,
				    "misc 0x%02x", cases[i].misc);
		KUNIT_EXPECT_EQ_MSG(test, cc.rp, cases[i].rp,
				    "misc 0x%02x", cases[i].misc);
	}
}

static const struct smb5_cc smb5_cc_none = { false, false, SMB5_RP_DEFAULT };
static const struct smb5_cc smb5_cc_3p0 = { true, false, SMB5_RP_3P0 };
static const struct smb5_cc smb5_cc_3p0_flipped = { true, true, SMB5_RP_3P0 };
static const struct smb5_cc smb5_cc_1p5 = { true, false, SMB5_RP_1P5 };

static void smb5_expect_hold(struct kunit *test, struct smb5_cc_step step)
{
	KUNIT_EXPECT_TRUE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_DEFAULT);
}

/* Attach, classify, then stay quiet; the start time straddles a jiffies wrap. */
static struct smb5_cc_track smb5_classified(struct kunit *test,
					    const struct smb5_cc *cc,
					    unsigned long t0)
{
	struct smb5_cc_track t = {};
	struct smb5_cc_step step;

	step = smb5_cc_next(&t, 0, true, cc, t0);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, step.recheck, SMB5_CC_SETTLE);

	step = smb5_cc_next(&t, 0, true, cc, t0 + SMB5_CC_SETTLE - 1);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, step.recheck, 1UL);

	step = smb5_cc_next(&t, 0, true, cc, t0 + SMB5_CC_SETTLE);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_SAMPLED);

	step = smb5_cc_next(&t, 0, true, cc, t0 + 2 * SMB5_CC_SETTLE - 1);
	smb5_expect_hold(test, step);

	step = smb5_cc_next(&t, 0, true, cc, t0 + 2 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_FALSE(test, step.replugged);
	KUNIT_EXPECT_EQ(test, step.rp, cc->rp);
	KUNIT_EXPECT_EQ(test, step.recheck, 0UL);
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_CLASSIFIED);
	return t;
}

static void smb5_cc_attach_test(struct kunit *test)
{
	unsigned long t0 = ULONG_MAX - SMB5_CC_SETTLE / 2;
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, t0);
	struct smb5_cc_step step;

	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, t0 + 100 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_3P0);
	KUNIT_EXPECT_EQ_MSG(test, step.recheck, 0UL, "no poll once classified");

	t = smb5_classified(test, &smb5_cc_none, t0);
	KUNIT_EXPECT_EQ(test, t.cc.rp, SMB5_RP_DEFAULT);
}

static void smb5_cc_detach_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	step = smb5_cc_next(&t, 0, false, &smb5_cc_none, 5000);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ_MSG(test, step.recheck, 0UL, "no poll while detached");
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_DETACHED);

	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 5001);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);
}

/*
 * A plug edge handled between two passes resets the track, even though the
 * next pass sees USBIN online and the same Type-C sample as before.
 */
static void smb5_cc_reattach_between_passes_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	t.state = SMB5_CC_DETACHED;
	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 3000);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_SETTLING);
}

/* A sample of the previous attach never completes the classification of the new one. */
static void smb5_cc_stale_sample_test(struct kunit *test)
{
	struct smb5_cc_track t = {};
	struct smb5_cc_step step;

	smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 1000);
	smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 1000 + SMB5_CC_SETTLE);
	KUNIT_ASSERT_EQ(test, t.state, SMB5_CC_SAMPLED);

	t.state = SMB5_CC_DETACHED;
	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 1000 + 2 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, step.recheck, SMB5_CC_SETTLE);
}

/* Without any plug edge: a flipped orientation is a new attach, a new Rp is not. */
static void smb5_cc_hardware_change_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0_flipped, 4000);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);

	t = smb5_classified(test, &smb5_cc_3p0, 1000);
	step = smb5_cc_next(&t, 0, true, &smb5_cc_1p5, 4000);
	KUNIT_EXPECT_FALSE(test, step.replugged);
	smb5_expect_hold(test, step);
	smb5_cc_next(&t, 0, true, &smb5_cc_1p5, 4000 + SMB5_CC_SETTLE);
	step = smb5_cc_next(&t, 0, true, &smb5_cc_1p5, 4000 + 2 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_1P5);
}

static void smb5_cc_unstable_test(struct kunit *test)
{
	const struct smb5_cc *samples[] = {
		&smb5_cc_3p0, &smb5_cc_1p5, &smb5_cc_3p0, &smb5_cc_1p5,
	};
	struct smb5_cc_track t = {};
	struct smb5_cc_step step;
	int i;

	smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 1000);
	for (i = 0; i < ARRAY_SIZE(samples) - 1; i++) {
		step = smb5_cc_next(&t, 0, true, samples[i],
				    1000 + (i + 1) * SMB5_CC_SETTLE);
		smb5_expect_hold(test, step);
		KUNIT_EXPECT_EQ(test, step.recheck, SMB5_CC_SETTLE);
	}
	step = smb5_cc_next(&t, 0, true, samples[i], 1000 + (i + 1) * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_DEFAULT);
	KUNIT_EXPECT_EQ_MSG(test, step.recheck, 0UL, "bounded number of samples");
}

static void smb5_cc_resume_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	smb5_cc_restart(&t, 9000);
	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, 9000);
	KUNIT_EXPECT_FALSE(test, step.replugged);
	smb5_expect_hold(test, step);
	KUNIT_EXPECT_EQ(test, step.recheck, SMB5_CC_SETTLE);
}

/*
 * A plug edge was counted in hard IRQ context before the pass, and its IRQ
 * thread has not run: the same orientation and Rp neither keep the old
 * classification nor complete a pending one.
 */
static void smb5_cc_edge_before_pass_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	step = smb5_cc_next(&t, 2, true, &smb5_cc_3p0, 4000);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);

	step = smb5_cc_next(&t, 2, true, &smb5_cc_3p0, 4000 + SMB5_CC_SETTLE);
	KUNIT_ASSERT_EQ(test, t.state, SMB5_CC_SAMPLED);
	step = smb5_cc_next(&t, 3, true, &smb5_cc_3p0, 4000 + 2 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);

	smb5_cc_next(&t, 3, true, &smb5_cc_3p0, 4000 + 3 * SMB5_CC_SETTLE);
	step = smb5_cc_next(&t, 3, true, &smb5_cc_3p0, 4000 + 4 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_3P0);
}

/* A plug edge counted while a pass wrote a limit above 500 mA voids that limit. */
static void smb5_cc_edge_during_pass_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;

	KUNIT_EXPECT_TRUE(test, smb5_cc_still_valid(&t, 0));
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_CLASSIFIED);

	KUNIT_EXPECT_FALSE(test, smb5_cc_still_valid(&t, 1));
	KUNIT_EXPECT_EQ(test, t.state, SMB5_CC_DETACHED);
	step = smb5_cc_next(&t, 1, true, &smb5_cc_3p0, 4000);
	KUNIT_EXPECT_TRUE(test, step.replugged);
	smb5_expect_hold(test, step);
}

/* After a failed write nothing above 500 mA, until a 500 mA write succeeds. */
static void smb5_cc_icl_unconfirmed_test(struct kunit *test)
{
	struct smb5_cc_track t = smb5_classified(test, &smb5_cc_3p0, 1000);
	struct smb5_cc_step step;
	unsigned long now = 4000;
	int i;

	smb5_cc_icl_written(&t, 500000, -EIO);
	for (i = 0; i < 6; i++, now += SMB5_CC_SETTLE) {
		step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, now);
		smb5_expect_hold(test, step);
		KUNIT_EXPECT_FALSE(test, step.replugged);
		KUNIT_EXPECT_EQ_MSG(test, step.recheck, SMB5_CC_SETTLE, "retry");
	}

	smb5_cc_icl_written(&t, 3000000, 0);
	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, now);
	smb5_expect_hold(test, step);

	smb5_cc_icl_written(&t, 500000, 0);
	smb5_cc_next(&t, 0, true, &smb5_cc_3p0, now + SMB5_CC_SETTLE);
	step = smb5_cc_next(&t, 0, true, &smb5_cc_3p0, now + 2 * SMB5_CC_SETTLE);
	KUNIT_EXPECT_FALSE(test, step.hold);
	KUNIT_EXPECT_EQ(test, step.rp, SMB5_RP_3P0);
}

struct smb5_limit_case {
	const char *desc;
	bool held;
	int end;
	int capacity;
	bool hold;
	bool held_after;
};

static const struct smb5_limit_case smb5_limit_cases[] = {
	{ "off at 100", false, 100, 100, false, false },
	{ "off clears held", true, 100, 90, false, false },
	{ "off ignores no capacity", false, 100, -ENODATA, false, false },
	{ "below end", false, 80, 79, false, false },
	{ "at end", false, 80, 80, true, true },
	{ "above end", false, 80, 95, true, true },
	{ "in band, rising", false, 80, 76, false, false },
	{ "in band, held", true, 80, 79, true, true },
	{ "band edge, held", true, 80, 76, true, true },
	{ "release, held", true, 80, 75, false, false },
	{ "below band", false, 80, 40, false, false },
	{ "no capacity", false, 80, -ENODATA, true, false },
	{ "no capacity, held", true, 80, -EIO, true, true },
	{ "capacity above 100", false, 80, 101, true, false },
	{ "lowest end", false, 50, 50, true, true },
	{ "empty, lowest end", false, 50, 0, false, false },
};

static void smb5_limit_case_desc(const struct smb5_limit_case *c, char *desc)
{
	strscpy(desc, c->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(smb5_limit, smb5_limit_cases, smb5_limit_case_desc);

static void smb5_limit_hold_test(struct kunit *test)
{
	const struct smb5_limit_case *c = test->param_value;
	bool held = c->held;

	KUNIT_EXPECT_EQ(test, smb5_limit_hold(&held, c->end, c->capacity), c->hold);
	KUNIT_EXPECT_EQ(test, held, c->held_after);
}

/* A run at an 80 % limit: one stop, one restart, a gauge glitch in between. */
static void smb5_limit_cycle_test(struct kunit *test)
{
	static const struct {
		int capacity;
		bool hold;
	} run[] = {
		{ 78, false }, { 79, false }, { 80, true }, { 79, true },
		{ -EIO, true }, { 77, true }, { 76, true }, { 75, false },
		{ 76, false }, { -EIO, true }, { 77, false }, { 80, true },
	};
	bool held = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(run); i++)
		KUNIT_EXPECT_EQ_MSG(test, smb5_limit_hold(&held, 80, run[i].capacity),
				    run[i].hold, "step %d, capacity %d", i, run[i].capacity);
}

/* What the property accepts; the omarchy-mobile helper writes 80 to 100. */
static void smb5_limit_valid_test(struct kunit *test)
{
	static const struct {
		int end;
		bool valid;
	} cases[] = {
		{ -1, false }, { 0, false }, { 30, false }, { 49, false },
		{ 50, true }, { 80, true }, { 85, true }, { 90, true },
		{ 95, true }, { 100, true }, { 101, false },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++)
		KUNIT_EXPECT_EQ_MSG(test, smb5_limit_valid(cases[i].end), cases[i].valid,
				    "end %d", cases[i].end);
}

/* The limit is one more reason not to charge; it never enables charging. */
static void smb5_charge_enable_test(struct kunit *test)
{
	struct smb5_chip *chip = kunit_kzalloc(test, sizeof(*chip), GFP_KERNEL);
	unsigned int bits;

	KUNIT_ASSERT_NOT_NULL(test, chip);
	for (bits = 0; bits < 16; bits++) {
		chip->user_charge_enable = bits & 1;
		chip->policy_charge_enable = bits & 2;
		chip->limit_hold = bits & 4;
		chip->suspended = bits & 8;
		KUNIT_EXPECT_EQ_MSG(test, smb5_charge_enable(chip), bits == 3,
				    "user %d policy %d limit %d suspended %d",
				    !!(bits & 1), !!(bits & 2), !!(bits & 4), !!(bits & 8));
		KUNIT_EXPECT_EQ_MSG(test, smb5_charge_wanted(chip), bits % 8 == 3,
				    "user %d policy %d limit %d",
				    !!(bits & 1), !!(bits & 2), !!(bits & 4));
	}
}

/*
 * A cable edge counts as handled only after a pass wrote its decision; a
 * write that keeps failing gives up after three passes so suspend is not
 * refused for good.
 */
static void smb5_policy_settle_test(struct kunit *test)
{
	static const struct {
		unsigned int gen;
		int ret;
		unsigned int policy_gen;
	} run[] = {
		{ 2, -EIO, 1 }, { 3, -EIO, 1 }, { 3, -EIO, 3 },
		{ 4, -EIO, 3 }, { 4, 0, 4 },
		{ 5, -EIO, 4 }, { 5, -EIO, 4 }, { 6, 0, 6 },
	};
	unsigned int policy_gen = 1, fails = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(run); i++) {
		smb5_policy_settle(&policy_gen, &fails, run[i].gen, run[i].ret);
		KUNIT_EXPECT_EQ_MSG(test, policy_gen, run[i].policy_gen,
				    "step %d, gen %u, write %d", i, run[i].gen, run[i].ret);
	}
	KUNIT_EXPECT_EQ(test, fails, 0);
}

static struct kunit_case smb5_charger_test_cases[] = {
	KUNIT_CASE_PARAM(smb5_input_limit_test, smb5_input_gen_params),
	KUNIT_CASE(smb5_rp_from_snk_status_test),
	KUNIT_CASE(smb5_rp_sweep_test),
	KUNIT_CASE(smb5_input_limit_bounds_test),
	KUNIT_CASE(smb5_cc_from_status_test),
	KUNIT_CASE(smb5_cc_attach_test),
	KUNIT_CASE(smb5_cc_detach_test),
	KUNIT_CASE(smb5_cc_reattach_between_passes_test),
	KUNIT_CASE(smb5_cc_stale_sample_test),
	KUNIT_CASE(smb5_cc_hardware_change_test),
	KUNIT_CASE(smb5_cc_unstable_test),
	KUNIT_CASE(smb5_cc_resume_test),
	KUNIT_CASE(smb5_cc_edge_before_pass_test),
	KUNIT_CASE(smb5_cc_edge_during_pass_test),
	KUNIT_CASE(smb5_cc_icl_unconfirmed_test),
	KUNIT_CASE_PARAM(smb5_limit_hold_test, smb5_limit_gen_params),
	KUNIT_CASE(smb5_limit_cycle_test),
	KUNIT_CASE(smb5_limit_valid_test),
	KUNIT_CASE(smb5_charge_enable_test),
	KUNIT_CASE(smb5_policy_settle_test),
	{}
};

static struct kunit_suite smb5_charger_test_suite = {
	.name = "qcom_pm8150b_charger",
	.test_cases = smb5_charger_test_cases,
};

kunit_test_suite(smb5_charger_test_suite);
