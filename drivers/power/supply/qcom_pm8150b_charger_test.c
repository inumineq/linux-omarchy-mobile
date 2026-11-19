// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the PM8150B charger input current limit. Included by
 * qcom_pm8150b_charger.c, so the tested helpers can stay static.
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

static struct kunit_case smb5_charger_test_cases[] = {
	KUNIT_CASE_PARAM(smb5_input_limit_test, smb5_input_gen_params),
	KUNIT_CASE(smb5_rp_from_snk_status_test),
	{}
};

static struct kunit_suite smb5_charger_test_suite = {
	.name = "qcom_pm8150b_charger",
	.test_cases = smb5_charger_test_cases,
};

kunit_test_suite(smb5_charger_test_suite);
