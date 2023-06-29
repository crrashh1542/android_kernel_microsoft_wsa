// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2020 - 2023 Intel Corporation
 */

#include "mvm.h"
#include "fw/api/commands.h"
#include "fw/api/phy-ctxt.h"

/* DDR needs frequency in units of 16.666MHz, so provide FW with the
 * frequency values in the adjusted format.
 */
static const
struct iwl_rfi_lut_entry iwl_rfi_ddr_table[IWL_RFI_DDR_LUT_SIZE] = {
	/* frequency 2600MHz */
	{cpu_to_le16(156), {34, 36, 38, 40, 42, 50},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5,}},

	/* frequency 2667MHz */
	{cpu_to_le16(160), {50, 58, 60, 62, 64, 68},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5,}},

	/* frequency 2800MHz */
	{cpu_to_le16(168), {114, 116, 118, 120, 122},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 2933MHz */
	{cpu_to_le16(176), {163, 167, 169, 171, 173, 175},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5,}},

	/* frequency 3000MHz */
	{cpu_to_le16(180), {3, 5, 7, 9, 11, 15, 31},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 3067MHz */
	{cpu_to_le16(184), {15, 23, 27, 29, 31, 33, 35, 37, 39, 47, 63},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6,}},

	/* frequency 3200MHz */
	{cpu_to_le16(192), {63, 79, 83, 85, 87, 89, 91, 95},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 3300MHz */
	{cpu_to_le16(198), {95, 111, 119, 123, 125, 129, 127, 131, 135, 143,
				159},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6,}},

	/* frequency 3400MHz */
	{cpu_to_le16(204), {159, 163, 165, 167, 169, 171, 175, 191},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 3733MHz */
	{cpu_to_le16(224), {114, 116, 118, 120, 122},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 4000MHz */
	{cpu_to_le16(240), {3, 5, 7, 9, 11, 15, 31},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 4200MHz */
	{cpu_to_le16(252), {63, 65, 67, 69, 71, 79, 95},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 4267MHz */
	{cpu_to_le16(256), {63, 79, 83, 85, 87, 89, 91, 95},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 4400MHz */
	{cpu_to_le16(264), {95, 111, 119, 123, 125, 127, 129, 131, 135, 143,
				159},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6,}},

	/* frequency 4600MHz */
	{cpu_to_le16(276), {159, 175, 183, 185, 187, 189, 191},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 4800MHz */
	{cpu_to_le16(288), {1, 3, 5, 7, 9, 11, 13, 15},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 5200MHz */
	{cpu_to_le16(312), {34, 36, 38, 40, 42, 50},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
		PHY_BAND_5,}},

	/* frequency 5333MHz */
	{cpu_to_le16(320), {50, 58, 60, 62, 64, 68},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
		PHY_BAND_5,}},

	/* frequency 5600MHz */
	{cpu_to_le16(336), {114, 116, 118, 120, 122},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 5868MHz */
	{cpu_to_le16(352), {163, 167, 169, 171, 173, 175},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
		PHY_BAND_5,}},

	/* frequency 6000MHz */
	{cpu_to_le16(360), {3, 5, 7, 9, 11, 15, 31},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 6133MHz */
	{cpu_to_le16(368), {15, 23, 27, 29, 31, 33, 35, 37, 39, 47, 63},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6,}},

	/* frequency 6400MHz */
	{cpu_to_le16(384), {63, 79, 83, 85, 87, 89, 91, 95,},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 6600MHz */
	{cpu_to_le16(396), {95, 111, 119, 123, 125, 127, 129, 131, 135, 143,
				159},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6,}},

	/* frequency 6800MHz */
	{cpu_to_le16(408), {159, 163, 165, 167, 169, 171, 175, 191},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 6933MHz */
	{cpu_to_le16(416), {159, 175, 183, 187, 189, 191, 193, 195, 197, 199,
				207},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6,}},
};

bool iwl_rfi_ddr_supported(struct iwl_mvm *mvm)
{
	u8 dsm_rfi_ddr = iwl_mvm_eval_dsm_rfi_ddr(mvm);
	u32 mac_type = CSR_HW_REV_TYPE(mvm->trans->hw_rev);
	bool ddr_capa = fw_has_capa(&mvm->fw->ucode_capa,
				    IWL_UCODE_TLV_CAPA_RFI_DDR_SUPPORT);

	IWL_DEBUG_FW(mvm,
		     "FW has RFI DDR capability:%s DDR enabled in BIOS:%s\n",
		     ddr_capa ? "yes" : "no",
		     dsm_rfi_ddr == DSM_VALUE_RFI_DDR_ENABLE ? "yes" : "no");
	IWL_DEBUG_FW(mvm,
		     "HW is integrated:%s mac type:%d fw_rfi_state:%d\n",
		     mvm->trans->trans_cfg->integrated ? "yes" : "no",
		     mac_type, mvm->fw_rfi_state);

	return ddr_capa && dsm_rfi_ddr == DSM_VALUE_RFI_DDR_ENABLE &&
		mac_type >= IWL_CFG_MAC_TYPE_MA &&
		mvm->trans->trans_cfg->integrated &&
		mvm->fw_rfi_state == IWL_RFI_PMC_SUPPORTED;
}

bool iwl_rfi_dlvr_supported(struct iwl_mvm *mvm)
{
	u8 dsm_rfi_dlvr = iwl_mvm_eval_dsm_rfi_dlvr(mvm);
	u32 mac_type = CSR_HW_REV_TYPE(mvm->trans->hw_rev);
	bool dlvr_capa = fw_has_capa(&mvm->fw->ucode_capa,
				     IWL_UCODE_TLV_CAPA_RFI_DLVR_SUPPORT);

	IWL_DEBUG_FW(mvm,
		     "FW has RFI DLVR capability:%s DLVR enabled in BIOS:%s\n",
		     dlvr_capa ? "yes" : "no",
		     dsm_rfi_dlvr == DSM_VALUE_RFI_DLVR_ENABLE ? "yes" : "no");
	IWL_DEBUG_FW(mvm,
		     "HW is integrated:%s mac type:%d fw_rfi_state:%d\n",
		     mvm->trans->trans_cfg->integrated ? "yes" : "no",
		     mac_type, mvm->fw_rfi_state);

	return dlvr_capa && dsm_rfi_dlvr == DSM_VALUE_RFI_DLVR_ENABLE &&
		mac_type >= IWL_CFG_MAC_TYPE_MA &&
		mvm->trans->trans_cfg->integrated &&
		mvm->fw_rfi_state == IWL_RFI_PMC_SUPPORTED;
}

int iwl_rfi_send_config_cmd(struct iwl_mvm *mvm,
			    struct iwl_rfi_lut_entry *rfi_ddr_table)
{
	int ret;
	struct iwl_rfi_config_cmd *cmd = NULL;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(SYSTEM_GROUP, RFI_CONFIG_CMD),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.len[0] = sizeof(*cmd),
	};
	bool rfi_ddr_support = iwl_rfi_ddr_supported(mvm);
	bool rfi_dlvr_support = iwl_rfi_dlvr_supported(mvm);
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					   WIDE_ID(SYSTEM_GROUP,
						   RFI_CONFIG_CMD), 0);

	if (cmd_ver != 3)
		return -EOPNOTSUPP;

	if (!rfi_ddr_support && !rfi_dlvr_support)
		return -EOPNOTSUPP;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	hcmd.data[0] = cmd;

	lockdep_assert_held(&mvm->mutex);

	if (rfi_ddr_support) {
		cmd->rfi_memory_support = cpu_to_le32(RFI_DDR_SUPPORTED_MSK);
		/* in case no table is passed, use the default one */
		if (!rfi_ddr_table) {
			memcpy(cmd->table, iwl_rfi_ddr_table,
			       sizeof(cmd->table));
		} else {
			memcpy(cmd->table, rfi_ddr_table, sizeof(cmd->table));
			/* notify FW the table is not the default one */
			cmd->oem = 1;
		}
	}

	if (rfi_dlvr_support)
		cmd->rfi_memory_support |= cpu_to_le32(RFI_DLVR_SUPPORTED_MSK);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);

	if (ret)
		IWL_ERR(mvm, "Failed to send RFI config cmd %d\n", ret);

	kfree(cmd);
	return ret;
}

struct iwl_rfi_freq_table_resp_cmd *iwl_rfi_get_freq_table(struct iwl_mvm *mvm)
{
	struct iwl_rfi_freq_table_resp_cmd *resp;
	int resp_size = sizeof(*resp);
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(SYSTEM_GROUP, RFI_GET_FREQ_TABLE_CMD),
		.flags = CMD_WANT_SKB,
	};

	if (!iwl_rfi_ddr_supported(mvm))
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &cmd);
	mutex_unlock(&mvm->mutex);
	if (ret)
		return ERR_PTR(ret);

	if (WARN_ON_ONCE(iwl_rx_packet_payload_len(cmd.resp_pkt) != resp_size))
		return ERR_PTR(-EIO);

	resp = kmemdup(cmd.resp_pkt->data, resp_size, GFP_KERNEL);
	if (!resp)
		return ERR_PTR(-ENOMEM);

	iwl_free_resp(&cmd);
	return resp;
}

void iwl_rfi_support_notif_handler(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rfi_support_notif *notif = (void *)pkt->data;

	mvm->fw_rfi_state = le32_to_cpu(notif->reason);
	switch (mvm->fw_rfi_state) {
	case IWL_RFI_PMC_SUPPORTED:
		IWL_DEBUG_FW(mvm, "RFIm, PMC supported\n");
		break;
	case IWL_RFI_PMC_NOT_SUPPORTED:
		IWL_DEBUG_FW(mvm, "RFIm, PMC not supported\n");
		break;
	case IWL_RFI_RESET_FAILURE_SEND_TO_PEER:
		fallthrough;
	case IWL_RFI_RESET_FAILURE_PLAT_PSS:
		fallthrough;
	case IWL_RFI_RESET_FAILURE_TIMER:
		fallthrough;
	case IWL_RFI_MAX_RESETS_DONE:
		fallthrough;
	default:
		IWL_DEBUG_FW(mvm, "RFIm is deactivated, reason = %d\n",
			     mvm->fw_rfi_state);
	}
}
