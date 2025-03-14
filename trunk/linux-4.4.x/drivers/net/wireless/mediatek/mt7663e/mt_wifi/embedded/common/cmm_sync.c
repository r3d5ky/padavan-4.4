/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

	Module Name:
	cmm_sync.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John Chang	2004-09-01      modified for rt2561/2661
*/
#include "rt_config.h"

/*BaSizeArray follows the 802.11n definition as MaxRxFactor.  2^(13+factor) bytes. When factor =0, it's about Ba buffer size =8.*/
UCHAR BaSizeArray[4] = {8, 16, 32, 64};

extern COUNTRY_REGION_CH_DESC Country_Region_ChDesc_2GHZ[];
extern UINT16 const Country_Region_GroupNum_2GHZ;
extern COUNTRY_REGION_CH_DESC Country_Region_ChDesc_5GHZ[];
extern UINT16 const Country_Region_GroupNum_5GHZ;

/*
	==========================================================================
	Description:
		Update StaCfg[0]->ChannelList[] according to 1) Country Region 2) RF IC type,
		and 3) PHY-mode user selected.
		The outcome is used by driver when doing site survey.

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
static UCHAR BuildChannelListFor2G(RTMP_ADAPTER *pAd, CHANNEL_CTRL *pChCtrl, UCHAR PhyMode)
{
	UCHAR ChIdx, ChIdx_TxPwr, num = 0;
	PCH_DESC pChDesc = NULL;
	BOOLEAN bRegionFound = FALSE;
	PUCHAR pChannelList;
	PUCHAR pChannelListFlag;
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("[BuildChannelListFor2G]\n"));

	for (ChIdx = 0; ChIdx < Country_Region_GroupNum_2GHZ; ChIdx++) {
		if ((pAd->CommonCfg.CountryRegion & 0x7f) ==
			Country_Region_ChDesc_2GHZ[ChIdx].RegionIndex) {
			pChDesc = Country_Region_ChDesc_2GHZ[ChIdx].pChDesc;
			num = TotalChNum(pChDesc);
			pAd->CommonCfg.pChDesc2G = (PUCHAR)pChDesc;
			bRegionFound = TRUE;
			break;
		}
	}

	if (!bRegionFound) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("CountryRegion=%d not support", pAd->CommonCfg.CountryRegion));
		goto done;
	}

	if (num > 0) {
		os_alloc_mem(NULL, (UCHAR **)&pChannelList, num * sizeof(UCHAR));

		if (!pChannelList) {
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s:Allocate memory for ChannelList failed\n", __func__));
			goto done;
		}

		os_alloc_mem(NULL, (UCHAR **)&pChannelListFlag, num * sizeof(UCHAR));

		if (!pChannelListFlag) {
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s:Allocate memory for ChannelListFlag failed\n", __func__));
			os_free_mem(pChannelList);
			goto done;
		}

		for (ChIdx = 0; ChIdx < num; ChIdx++) {
			pChannelList[ChIdx] = GetChannel_2GHZ(pChDesc, ChIdx);
			pChannelListFlag[ChIdx] = GetChannelFlag(pChDesc, ChIdx);
		}

		for (ChIdx = 0; ChIdx < num; ChIdx++) {
			for (ChIdx_TxPwr = 0; ChIdx_TxPwr < MAX_NUM_OF_CHANNELS; ChIdx_TxPwr++) {
				if (pChannelList[ChIdx] == pAd->TxPower[ChIdx_TxPwr].Channel)
					hc_set_ChCtrl(pChCtrl, pAd, ChIdx, ChIdx_TxPwr);
			}
#ifdef CONFIG_AP_SUPPORT
			if (pChannelList[ChIdx] == 14) {
				if (!strncmp((RTMP_STRING *) pAd->CommonCfg.CountryCode, "JP", 2)) {
					/* for JP, ch14 can only be used when PhyMode is "B only" */
					if (!WMODE_EQUAL(PhyMode, WMODE_B)) {
						num--;
						break;
					}
				} else {
					/* Ch14 can only be used in Japan */
					num--;
					break;
				}
			}
#endif
			pChCtrl->ChList[ChIdx].Channel = pChannelList[ChIdx];
			if (!strncmp((RTMP_STRING *) pAd->CommonCfg.CountryCode, "CN", 2))
				pChCtrl->ChList[ChIdx].MaxTxPwr = pAd->MaxTxPwr;/*for CN CountryCode*/
			else
				pChCtrl->ChList[ChIdx].MaxTxPwr = 20;
			pChCtrl->ChList[ChIdx].Flags = pChannelListFlag[ChIdx];

#ifdef RT_CFG80211_SUPPORT
			CFG80211OS_ChanInfoInit(
				pAd->pCfg80211_CB,
				ChIdx,
				pChCtrl->ChList[ChIdx].Channel,
				pChCtrl->ChList[ChIdx].MaxTxPwr,
				TRUE,
				TRUE);
#endif /* RT_CFG80211_SUPPORT */
		}

		pChCtrl->ChListNum = num;

		os_free_mem(pChannelList);
		os_free_mem(pChannelListFlag);
	}

#ifdef RT_CFG80211_SUPPORT

	if (CFG80211OS_UpdateRegRuleByRegionIdx(pAd->pCfg80211_CB, pChDesc, NULL) != 0)
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Update RegRule failed!\n"));

#endif /* RT_CFG80211_SUPPORT */
done:
	return num;
}


static UCHAR BuildChannelListFor5G(RTMP_ADAPTER *pAd, CHANNEL_CTRL *pChCtrl, UCHAR PhyMode)
{
	UCHAR ChIdx, ChIdx2, num = 0;
	PCH_DESC pChDesc = NULL;
	BOOLEAN bRegionFound = FALSE;
	PUCHAR pChannelList;
	PUCHAR pChannelListFlag;

	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("[BuildChannelListFor5G] \n"));



#ifdef RT_CFG80211_SUPPORT
	UCHAR bw;
	int apidx;

	for (apidx = 0; apidx < pAd->ApCfg.BssidNum; apidx++) {
		BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[apidx];
		struct wifi_dev *wdev = &pMbss->wdev;

		bw = HcGetBw(pAd, wdev);
	}
	PhyMode = HcGetRadioPhyMode(pAd);
#endif

	for (ChIdx = 0; ChIdx < Country_Region_GroupNum_5GHZ; ChIdx++) {
		if ((pAd->CommonCfg.CountryRegionForABand & 0x7f) ==
			Country_Region_ChDesc_5GHZ[ChIdx].RegionIndex) {
			pChDesc = Country_Region_ChDesc_5GHZ[ChIdx].pChDesc;
			num = TotalChNum(pChDesc);
			pAd->CommonCfg.pChDesc5G = (PUCHAR)pChDesc;
			bRegionFound = TRUE;
			break;
		}
	}

	if (!bRegionFound) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("CountryRegionABand=%d not support",
				 pAd->CommonCfg.CountryRegionForABand));
		goto done;
	}

	if (num > 0) {
		UCHAR RadarCh[16] = {52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144};
#ifdef CONFIG_AP_SUPPORT
		UCHAR q = 0;
#endif
		os_alloc_mem(NULL, (UCHAR **)&pChannelList, num * sizeof(UCHAR));

		if (!pChannelList) {
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s:Allocate memory for ChannelList failed\n", __func__));
			goto done;
		}

		os_alloc_mem(NULL, (UCHAR **)&pChannelListFlag, num * sizeof(UCHAR));

		if (!pChannelListFlag) {
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s:Allocate memory for ChannelListFlag failed\n", __func__));
			os_free_mem(pChannelList);
			goto done;
		}

		for (ChIdx = 0; ChIdx < num; ChIdx++) {
			pChannelList[ChIdx] = GetChannel_5GHZ(pChDesc, ChIdx);
			pChannelListFlag[ChIdx] = GetChannelFlag(pChDesc, ChIdx);
		}
#ifdef CONFIG_AP_SUPPORT

		for (ChIdx = 0; ChIdx < num; ChIdx++) {
			if ((pAd->CommonCfg.bIEEE80211H == 0) || ((pAd->CommonCfg.bIEEE80211H == 1) && (pAd->CommonCfg.RDDurRegion != FCC))) {
				/* Profile parameter - ChannelGrp is enabled */
				if (MTChGrpValid(pChCtrl)) {
					/* Get channels according to ChannelGrp */
					if (MTChGrpChannelChk(pChCtrl, GetChannel_5GHZ(pChDesc, ChIdx))) {
						pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
						pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
						q++;
					}
				} else {
					pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
					pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
					q++;
				}
				MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("[BuildChannelListFor5G] - RDDurRegion != FCC - q=%d!\n", q));
			}
			/*Based on the requiremnt of FCC, some channles could not be used anymore when test DFS function.*/
			else if ((pAd->CommonCfg.bIEEE80211H == 1) &&
					 (pAd->CommonCfg.RDDurRegion == FCC) &&
					 (pAd->Dot11_H[0].bDFSIndoor == 1)) {
				if (MTChGrpValid(pChCtrl)) {
					if (MTChGrpChannelChk(pChCtrl, GetChannel_5GHZ(pChDesc, ChIdx))) {
						pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
						pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
						q++;
					}
				} else {
					pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
					pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
					q++;
				}
			} else if ((pAd->CommonCfg.bIEEE80211H == 1) &&
					   (pAd->CommonCfg.RDDurRegion == FCC) &&
					   (pAd->Dot11_H[0].bDFSIndoor == 0)) {
				if ((GetChannel_5GHZ(pChDesc, ChIdx) < 100) || (GetChannel_5GHZ(pChDesc, ChIdx) > 140)) {
					if (MTChGrpValid(pChCtrl)) {
						if (MTChGrpChannelChk(pChCtrl, GetChannel_5GHZ(pChDesc, ChIdx))) {
							pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
							pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
							q++;
						}
					} else {
						pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
						pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
						q++;
					}
				}
			} else if (MTChGrpValid(pChCtrl) && MTChGrpChannelChk(pChCtrl, GetChannel_5GHZ(pChDesc, ChIdx))) {
				pChannelList[q] = GetChannel_5GHZ(pChDesc, ChIdx);
				pChannelListFlag[q] = GetChannelFlag(pChDesc, ChIdx);
				q++;
			}
		}

		num = q;

		/* If Channel group is exclusive of supported channels of CountryRegionForABand */
		/* Build channel list based on channel group to avoid NULL channel list */
		if (num == 0 && MTChGrpValid(pChCtrl)) {
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR,
				("[%s] Build channel list based on channel group to avoid NULL channel list!\n",
				__func__));
			for (ChIdx = 0; ChIdx < pChCtrl->ChGrpABandChNum; ChIdx++) {
				pChannelList[ChIdx] = pChCtrl->ChGrpABandChList[ChIdx];
				pChannelListFlag[ChIdx] = CHANNEL_DEFAULT_PROP;
			}

			num = pChCtrl->ChGrpABandChNum;
		}

#endif /* CONFIG_AP_SUPPORT */

		for (ChIdx = 0; ChIdx < num; ChIdx++) {
			for (ChIdx2 = 0; ChIdx2 < MAX_NUM_OF_CHANNELS; ChIdx2++) {
				if (pChannelList[ChIdx] == pAd->TxPower[ChIdx2].Channel)
					hc_set_ChCtrl(pChCtrl, pAd, ChIdx, ChIdx2);

				pChCtrl->ChList[ChIdx].Channel = pChannelList[ChIdx];
				pChCtrl->ChList[ChIdx].Flags = pChannelListFlag[ChIdx];
			}

			for (ChIdx2 = 0; ChIdx2 < 16; ChIdx2++) {
				if (pChannelList[ChIdx] == RadarCh[ChIdx2]) {
				pChCtrl->ChList[ChIdx].DfsReq = TRUE;
				}
			}
			if (!strncmp((RTMP_STRING *) pAd->CommonCfg.CountryCode, "CN", 2))
				pChCtrl->ChList[ChIdx].MaxTxPwr = pAd->MaxTxPwr;/*for CN CountryCode*/
			else
				pChCtrl->ChList[ChIdx].MaxTxPwr = 20;

#ifdef RT_CFG80211_SUPPORT
			CFG80211OS_ChanInfoInit(
				pAd->pCfg80211_CB,
				ChIdx,
				pChCtrl->ChList[ChIdx].Channel,
				pChCtrl->ChList[ChIdx].MaxTxPwr,
				TRUE,
				TRUE);
#endif /*RT_CFG80211_SUPPORT*/
		}

		pChCtrl->ChListNum = num;
		os_free_mem(pChannelList);
		os_free_mem(pChannelListFlag);
	}

#ifdef RT_CFG80211_SUPPORT

	if (CFG80211OS_UpdateRegRuleByRegionIdx(pAd->pCfg80211_CB, NULL, pChDesc) != 0)
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("Update RegRule failed!\n"));

#endif /*RT_CFG80211_SUPPORT*/
done:
	return num;
}


static bool Set_Diff_Bw(RTMP_ADAPTER *pAd, UCHAR wcid, UCHAR bw, UCHAR ext_ch)
{
	PMAC_TABLE_ENTRY pEntry = NULL;
	struct wifi_dev *wdev = NULL;
	UCHAR op_ht_bw, op_ext_ch;
	struct _RTMP_CHIP_CAP *cap;
	pEntry = &pAd->MacTab.Content[wcid];
	wdev = pEntry->wdev;

	if (!wdev)
		return FALSE;

	op_ht_bw = wlan_operate_get_ht_bw(wdev);
	op_ext_ch = wlan_operate_get_ext_cha(wdev);
	cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (op_ht_bw != bw) {
		wlan_operate_set_ht_bw(wdev, bw, ext_ch);
		pEntry->HTPhyMode.field.BW = bw;
#ifdef RACTRL_FW_OFFLOAD_SUPPORT

		if (cap->fgRateAdaptFWOffload == TRUE) {
			CMD_STAREC_AUTO_RATE_UPDATE_T rRaParam;
			NdisZeroMemory(&rRaParam, sizeof(CMD_STAREC_AUTO_RATE_UPDATE_T));

			if (bw == HT_BW_40)
				rRaParam.u4Field = RA_PARAM_HT_2040_BACK;
			else
				rRaParam.u4Field = RA_PARAM_HT_2040_COEX;

			RAParamUpdate(pAd, pEntry, &rRaParam);
			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("FallBack APClient BW to %s\n",
					 rRaParam.u4Field == (RA_PARAM_HT_2040_BACK) ? "BW_40" : "BW_20"));
		}

#endif /* RACTRL_FW_OFFLOAD_SUPPORT */
	}

	return TRUE;
}

VOID BuildChannelList(RTMP_ADAPTER *pAd, struct wifi_dev *wdev)
{
	UCHAR ChIdx = 0;
	/*Get Band index from wdev*/
	UCHAR BandIdx = HcGetBandByWdev(wdev);
	UCHAR PhyMode = wdev->PhyMode;
	/* Get channel ctrl address */
	CHANNEL_CTRL *pChCtrl = hc_get_channel_ctrl(pAd->hdev_ctrl, BandIdx);

	/* Check state of channel list */
	if (hc_check_ChCtrlChListStat(pChCtrl, CH_LIST_STATE_DONE)) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("[BuildChannelList][BandIdx=%d] Channel list is already DONE\n", BandIdx));
		return;
	}

	/* Initialize channel list*/
	os_zero_mem(pChCtrl->ChList, MAX_NUM_OF_CHANNELS * sizeof(CHANNEL_TX_POWER));
	pChCtrl->ChListNum = 0;

	/* Build channel list based on PhyMode */
	if (WMODE_CAP_2G(PhyMode))
		BuildChannelListFor2G(pAd, pChCtrl, PhyMode);
	else if (WMODE_CAP_5G(PhyMode))
		BuildChannelListFor5G(pAd, pChCtrl, PhyMode);

	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("[BuildChannelList] BandIdx = %d, PhyMode = %d, ChListNum = %d:\n", BandIdx, PhyMode, pChCtrl->ChListNum));

	/* Build Channel CAP, should after get ChListNum */
#ifdef DOT11_N_SUPPORT

    for (ChIdx = 0; ChIdx < pChCtrl->ChListNum; ChIdx++) {
		if (N_ChannelGroupCheck(pAd, pChCtrl->ChList[ChIdx].Channel, wdev))
			hc_set_ChCtrlFlags_CAP(pChCtrl, CHANNEL_40M_CAP, ChIdx);

#ifdef DOT11_VHT_AC

		if (vht80_channel_group(pAd, pChCtrl->ChList[ChIdx].Channel))
			hc_set_ChCtrlFlags_CAP(pChCtrl, CHANNEL_80M_CAP, ChIdx);

		if (vht160_channel_group(pAd, pChCtrl->ChList[ChIdx].Channel))
			hc_set_ChCtrlFlags_CAP(pChCtrl, CHANNEL_160M_CAP, ChIdx);

#endif /* DOT11_VHT_AC */
	}

#endif /* DOT11_N_SUPPORT */

	if (WMODE_CAP_2G(PhyMode)) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
				 ("CountryCode(2.4G)=%d, RFIC=%d, support %d channels\n", pAd->CommonCfg.CountryRegion, pAd->RfIcType, pChCtrl->ChListNum));
	} else if (WMODE_CAP_5G(PhyMode)) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
				 ("CountryCode(5G)=%d, RFIC=%d, support %d channels\n", pAd->CommonCfg.CountryRegionForABand, pAd->RfIcType, pChCtrl->ChListNum));
	}

#ifdef MT_DFS_SUPPORT
	DfsBuildChannelList(pAd, wdev);
#endif
	/* Set state of channel list*/
	hc_set_ChCtrlChListStat(pChCtrl, CH_LIST_STATE_DONE);
	MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("[BuildChannelList] SupportedChannelList (ChCtrlStat = DONE):\n"));

	for (ChIdx = 0; ChIdx < pChCtrl->ChListNum; ChIdx++) {
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("\tChannel # %d: Pwr0/1 = %d/%d, Flags = %x\n ",
				 pChCtrl->ChList[ChIdx].Channel,
				 pChCtrl->ChList[ChIdx].Power,
				 pChCtrl->ChList[ChIdx].Power2,
				 pChCtrl->ChList[ChIdx].Flags));
	}

}


/*
	==========================================================================
	Description:
		This routine return the first channel number according to the country
		code selection and RF IC selection (signal band or dual band). It is called
		whenever driver need to start a site survey of all supported channels.
	Return:
		ch - the first channel number of current country code setting

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
UCHAR FirstChannel(RTMP_ADAPTER *pAd, struct wifi_dev *wdev)
{
	UCHAR BandIdx = HcGetBandByWdev(wdev);
	CHANNEL_CTRL *pChCtrl = hc_get_channel_ctrl(pAd->hdev_ctrl, BandIdx);
#ifdef MT_DFS_SUPPORT
	int ChnIdx, DfsChnIdx;
	UCHAR first_channel = 0;
	PDFS_PARAM pDfsParam = &pAd->CommonCfg.DfsParameter;
	BOOLEAN ChannelFound = FALSE;

	for (ChnIdx = 0; ChnIdx < (pChCtrl->ChListNum - 1); ChnIdx++) {
		/* For each channel from pChCtrl->ChList[i], check if the channel is present in DFS channel list with NOP == 0 */
		for (DfsChnIdx = 0; DfsChnIdx  < pDfsParam->ChannelListNum; DfsChnIdx++) {
			if (pChCtrl->ChList[ChnIdx].Channel == pDfsParam->DfsChannelList[DfsChnIdx].Channel) {

				if (pDfsParam->DfsChannelList[DfsChnIdx].NonOccupancy == 0)
					ChannelFound = TRUE;
				break;
			}
		}

		/* If channel is found to be DFS with NOP = 0 or it is non DFS channel, then get the current channel */
		if (ChannelFound ||
			(DfsChnIdx == pDfsParam->ChannelListNum)) {
			first_channel = pChCtrl->ChList[ChnIdx].Channel;
			break;
		}
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("Skip scanning channel %u due to remaining %u sec NOP\n",
			pChCtrl->ChList[ChnIdx].Channel, pDfsParam->DfsChannelList[DfsChnIdx].NonOccupancy));
	}

	return first_channel;
#else
	return pChCtrl->ChList[0].Channel;
#endif /* MT_DFS_SUPPORT */
}


/*
	==========================================================================
	Description:
		This routine returns the next channel number. This routine is called
		during driver need to start a site survey of all supported channels.
	Return:
		next_channel - the next channel number valid in current country code setting.
	Note:
		return 0 if no more next channel
	==========================================================================
 */
UCHAR NextChannel(
	RTMP_ADAPTER *pAd,
	SCAN_CTRL *ScanCtrl,
	UCHAR channel,
	struct wifi_dev *wdev)
{
	int i;
#ifdef MT_DFS_SUPPORT
	int DfsChnIdx;
#endif /* MT_DFS_SUPPORT */
	UCHAR next_channel = 0;
	UCHAR BandIdx = HcGetBandByWdev(wdev);
	CHANNEL_CTRL *pChCtrl = hc_get_channel_ctrl(pAd->hdev_ctrl, BandIdx);
#ifdef MT_DFS_SUPPORT
	PDFS_PARAM pDfsParam = &pAd->CommonCfg.DfsParameter;
	BOOLEAN ChannelFound = FALSE;
#endif /* MT_DFS_SUPPORT */

	for (i = 0; i < (pChCtrl->ChListNum - 1); i++) {
		if (channel == pChCtrl->ChList[i].Channel) {
#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3

			/* Only scan effected channel if this is a SCAN_2040_BSS_COEXIST*/
			/* 2009 PF#2: Nee to handle the second channel of AP fall into affected channel range.*/
			if ((ScanCtrl->ScanType == SCAN_2040_BSS_COEXIST) && (pChCtrl->ChList[i + 1].Channel > 14)) {
				channel = pChCtrl->ChList[i + 1].Channel;
				continue;
			} else
#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */
			{
#ifdef MT_DFS_SUPPORT
				/* For each channel from pChCtrl->ChList[i], check if the channel is present in DFS channel list with NOP == 0 */
				for (DfsChnIdx = 0; DfsChnIdx  < pDfsParam->ChannelListNum; DfsChnIdx++) {
					if (pChCtrl->ChList[i + 1].Channel == pDfsParam->DfsChannelList[DfsChnIdx].Channel) {

						if (pDfsParam->DfsChannelList[DfsChnIdx].NonOccupancy == 0)
							ChannelFound = TRUE;
						break;
					}
				}

				/* If channel is found to be DFS with NOP = 0 or it is non DFS channel, then get the current channel */
				if (ChannelFound ||
					(DfsChnIdx == pDfsParam->ChannelListNum)) {
					next_channel = pChCtrl->ChList[i + 1].Channel;
					break;
				}

				MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("Skip scanning channel %u due to remaining %u sec NOP\n",
					pChCtrl->ChList[i + 1].Channel, pDfsParam->DfsChannelList[DfsChnIdx].NonOccupancy));
				channel = pChCtrl->ChList[i + 1].Channel;
#else
				/* Record this channel's idx in ChannelList array.*/
				next_channel = pChCtrl->ChList[i + 1].Channel;
				break;
#endif /* MT_DFS_SUPPORT */
			}
		}
	}

	return next_channel;
}


/*
	==========================================================================
	Description:
		This routine is for Cisco Compatible Extensions 2.X
		Spec31. AP Control of Client Transmit Power
	Return:
		None
	Note:
	   Required by Aironet dBm(mW)
		   0dBm(1mW),   1dBm(5mW), 13dBm(20mW), 15dBm(30mW),
		  17dBm(50mw), 20dBm(100mW)

	   We supported
		   3dBm(Lowest), 6dBm(10%), 9dBm(25%), 12dBm(50%),
		  14dBm(75%),   15dBm(100%)

		The client station's actual transmit power shall be within +/- 5dB of
		the minimum value or next lower value.
	==========================================================================
 */
VOID ChangeToCellPowerLimit(RTMP_ADAPTER *pAd, UCHAR AironetCellPowerLimit)
{
	/*
	    valud 0xFF means that hasn't found power limit information
	    from the AP's Beacon/Probe response
	*/
	if (AironetCellPowerLimit == 0xFF)
		return;

	if (AironetCellPowerLimit < 6) { /*Used Lowest Power Percentage.*/
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 6;
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 6;
#endif /* DBDC_MODE */
	} else if (AironetCellPowerLimit < 9) {
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 10;
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 10;
#endif /* DBDC_MODE */
	} else if (AironetCellPowerLimit < 12) {
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 25;
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 25;
#endif /* DBDC_MODE */
	} else if (AironetCellPowerLimit < 14) {
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 50;
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 50;
#endif /* DBDC_MODE */
	} else if (AironetCellPowerLimit < 15) {
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 75;
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 75;
#endif /* DBDC_MODE */
	} else {
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = 100; /*else used maximum*/
#ifdef DBDC_MODE
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = 100;
#endif /* DBDC_MODE */
	}

	if (pAd->CommonCfg.ucTxPowerPercentage[BAND0] > pAd->CommonCfg.ucTxPowerDefault[BAND0])
		pAd->CommonCfg.ucTxPowerPercentage[BAND0] = pAd->CommonCfg.ucTxPowerDefault[BAND0];

#ifdef DBDC_MODE

	if (pAd->CommonCfg.ucTxPowerPercentage[BAND1] > pAd->CommonCfg.ucTxPowerDefault[BAND1])
		pAd->CommonCfg.ucTxPowerPercentage[BAND1] = pAd->CommonCfg.ucTxPowerDefault[BAND1];

#endif /* DBDC_MODE */
}

CHAR ConvertToRssi(RTMP_ADAPTER *pAd, struct raw_rssi_info *rssi_info, UCHAR rssi_idx)
{
	UCHAR RssiOffset = 0, LNAGain;
	CHAR BaseVal;
	CHAR rssi, ret = 0;
	UCHAR FeOffset[4] = {0};

	/* Rssi equals to zero or rssi_idx larger than 3 should be an invalid value*/
	if (rssi_idx >= pAd->Antenna.field.RxPath)
		return -99;

	rssi = rssi_info->raw_rssi[rssi_idx];

	if (rssi == 0)
		return -99;

	LNAGain = pAd->hw_cfg.lan_gain;
#ifdef CONNAC_EFUSE_FORMAT_SUPPORT
	MtFeLossGet(pAd, pAd->LatchRfRegs.Channel, FeOffset);
	RssiOffset = FeOffset[rssi_idx];
#else
	if (pAd->LatchRfRegs.Channel > 14)
		RssiOffset = pAd->ARssiOffset[rssi_idx];
	else
		RssiOffset = pAd->BGRssiOffset[rssi_idx];
#endif /* CONNAC_EFUSE_FORMAT_SUPPORT */

	BaseVal = -12;

	ret = (rssi + (CHAR)RssiOffset - (CHAR)LNAGain);
	return ret;
}


CHAR ConvertToSnr(RTMP_ADAPTER *pAd, UCHAR Snr)
{
	CHAR ret = 0;
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (cap->SnrFormula == SNR_FORMULA2) {
		ret = (Snr * 3 + 8) >> 4;
		return ret;
	} else if (cap->SnrFormula == SNR_FORMULA3) {
		ret = (Snr * 3 / 16);
		return ret; /* * 0.1881 */
	} else {
		ret = ((0xeb	- Snr) * 3) /	16;
		return ret;
	}
}

#ifdef CONFIG_STA_SUPPORT
BOOLEAN AdjustBwToSyncAp(RTMP_ADAPTER *pAd, BCN_IE_LIST *ie_list, UCHAR  Wcid, UCHAR fun_tb_idx)
{
	BOOLEAN bAdjust = FALSE;
#ifdef DOT11_N_SUPPORT
	PMAC_TABLE_ENTRY pEntry = NULL;
	struct wifi_dev *wdev = NULL;
	UCHAR op_ht_bw;
	UCHAR cfg_ht_bw;
	UCHAR ExtCha;
	UCHAR channel_bw_cap;
#ifdef DOT11_VHT_AC
	UCHAR vht_config_bw;
	PSTA_ADMIN_CONFIG pStaCfg = &pAd->StaCfg[fun_tb_idx];
	UINT16 VhtOperationBW = wlan_operate_get_vht_bw(&pStaCfg->wdev);
	UINT16 HtOperationBW = wlan_operate_get_ht_bw(&pStaCfg->wdev);
	BOOLEAN bSupportVHTMCS1SS = FALSE;
	BOOLEAN bSupportVHTMCS2SS = FALSE;
	BOOLEAN bSupportVHTMCS3SS = FALSE;
	BOOLEAN bSupportVHTMCS4SS = FALSE;
	CMD_STAREC_AUTO_RATE_UPDATE_T rRaParam = {0};
	UINT nss_update_flag = 0;
	UINT vht_update_flag = WLAN_OPER_FAIL;
	UINT ht_update_flag = WLAN_OPER_FAIL;
#endif /* DOT11_VHT_AC */

	pEntry = &pAd->MacTab.Content[Wcid];
	wdev = pEntry->wdev;
	if (!wdev || !ie_list)
		return FALSE;
#ifdef DOT11_VHT_AC
	vht_config_bw = wlan_config_get_vht_bw(wdev);
#endif /* DOT11_VHT_AC */

	cfg_ht_bw = wlan_config_get_ht_bw(wdev);

	if (WMODE_CAP_N(wdev->PhyMode) && ie_list->AddHtInfoLen != 0 && wdev->channel < 14) {
		op_ht_bw = wlan_operate_get_ht_bw(wdev);
		ExtCha = wlan_operate_get_ext_cha(wdev);

		/* BW 40 -> 20 */
		if (op_ht_bw == HT_BW_40) {
			/* Check if root-ap change BW to 20 */
			if ((ie_list->AddHtInfo.AddHtInfo.ExtChanOffset == EXTCHA_NONE &&
				 ie_list->AddHtInfo.AddHtInfo.RecomWidth == 0)
				|| (ie_list->NewExtChannelOffset == 0x0)) {
				bAdjust = TRUE;
				Set_Diff_Bw(pAd, Wcid, BW_20, EXTCHA_NONE);
				MTWF_LOG(DBG_CAT_CLIENT, CATCLIENT_APCLI, DBG_LVL_TRACE, ("FallBack APClient BW to 20MHz\n"));
			}

			/* BW 20 -> 40 */
		} else if (op_ht_bw == HT_BW_20 && cfg_ht_bw != HT_BW_20) {
			/* Check if root-ap change BW to 40 */
			if (ie_list->AddHtInfo.AddHtInfo.ExtChanOffset != EXTCHA_NONE &&
				ie_list->AddHtInfo.AddHtInfo.RecomWidth == 1 &&
				ie_list->HtCapabilityLen > 0 &&
				ie_list->HtCapability.HtCapInfo.ChannelWidth == 1) {
				/* if extension channel is same with root ap else keep 20MHz */
				if (ExtCha == EXTCHA_NONE || ie_list->AddHtInfo.AddHtInfo.ExtChanOffset == ExtCha) {
					/* UCHAR RFChannel; */
					bAdjust = TRUE;
					Set_Diff_Bw(pAd, Wcid, BW_40, ie_list->AddHtInfo.AddHtInfo.ExtChanOffset);
					/*update channel for all of wdev belong this band*/
					wlan_operate_set_prim_ch(wdev, wdev->channel);
					MTWF_LOG(DBG_CAT_CLIENT, CATCLIENT_APCLI, DBG_LVL_TRACE, ("FallBack Client/APClient BW to 40MHz\n"));
				}
			}
		}

		if (bAdjust == TRUE) {
#ifdef MAC_REPEATER_SUPPORT

			if (pAd->ApCfg.bMACRepeaterEn) {
				UINT ifIndex;
				UCHAR CliIdx, update_bw, update_ext_ch;
				REPEATER_CLIENT_ENTRY *pReptEntry = NULL;
				RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);
				ifIndex = pEntry->func_tb_idx;
				update_bw = wlan_operate_get_ht_bw(wdev);
				update_ext_ch = wlan_operate_get_ext_cha(wdev);

				for (CliIdx = 0; CliIdx < GET_MAX_REPEATER_ENTRY_NUM(cap); CliIdx++) {
					pReptEntry = &pAd->ApCfg.pRepeaterCliPool[CliIdx];

					if ((pReptEntry->CliEnable) && (pReptEntry->CliValid) &&
						(pReptEntry->MatchApCliIdx == ifIndex)) {
						pEntry = &pAd->MacTab.Content[pReptEntry->MacTabWCID];

						if (pEntry)
							Set_Diff_Bw(pAd, pReptEntry->MacTabWCID, update_bw, update_ext_ch);
					}
				}

				MTWF_LOG(DBG_CAT_CLIENT, CATCLIENT_APCLI, DBG_LVL_TRACE, ("FallBack APClient BW to (%d)\n", update_bw));
			}

#endif /* MAC_REPEATER_SUPPORT */
		}
	}

#ifdef DOT11_VHT_AC
	/**
	 * This code is for VHT BW switching which follows VHT Operation mode.
	 * AP communicates VHT operation mode in VHT Operation IE
	 */
	if (WMODE_CAP_AC(wdev->PhyMode) && (pStaCfg->StaActive.SupportedPhyInfo.bVhtEnable == TRUE)) {
		/* Currently supporting bw switch only below 80 Mhz */
		if ((ie_list->vht_op_len != 0) && (ie_list->AddHtInfoLen != 0) &&
			((VhtOperationBW != ie_list->vht_op_ie.vht_op_info.ch_width) ||
			(HtOperationBW != ie_list->AddHtInfo.AddHtInfo.RecomWidth)) &&
			(VhtOperationBW <= VHT_BW_80) &&
			(ie_list->vht_op_ie.vht_op_info.ch_width <= VHT_BW_80)) {

			/* Get Channel Bandwidth capability */
			channel_bw_cap = get_channel_bw_cap(&pStaCfg->wdev, pStaCfg->wdev.channel);
			MTWF_LOG(DBG_CAT_CLIENT, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
				("SYNC - Peer AP Changed VHT BW[old:new] = [%u:%u], HT BW[old:new] = [%u:%u]\n",
				VhtOperationBW,
				ie_list->vht_op_ie.vht_op_info.ch_width,
				HtOperationBW,
				ie_list->AddHtInfo.AddHtInfo.RecomWidth));
			if (ie_list->vht_op_ie.vht_op_info.ch_width <= vht_config_bw) {
			pStaCfg->MlmeAux.vht_op_len = ie_list->vht_op_len;
			NdisMoveMemory(&pStaCfg->MlmeAux.vht_op, &ie_list->vht_op_ie, pStaCfg->MlmeAux.vht_op_len);
			}

			/* If IE VHT BW is 80, then need to check our prim channel has support for 80 in that region */
			/* 80 MHz operation is prevented in CE and JAP for channel 132~144 */
			if (((ie_list->vht_op_ie.vht_op_info.ch_width == VHT_BW_80) && (channel_bw_cap > BW_40)) ||
					(HtOperationBW != ie_list->AddHtInfo.AddHtInfo.RecomWidth))
				vht_update_flag = wlan_operate_set_vht_bw(&pStaCfg->wdev, ie_list->vht_op_ie.vht_op_info.ch_width);
			else if (ie_list->vht_op_ie.vht_op_info.ch_width != VHT_BW_80)
				vht_update_flag = wlan_operate_set_vht_bw(&pStaCfg->wdev, ie_list->vht_op_ie.vht_op_info.ch_width);


			/**
			 * It is seen that as a part of change in BW notification some AP
			 * also changes Rx Mcs as well as NSS
			 */
			if ((pEntry) && (ie_list->vht_cap_len > 0)) {
				UCHAR TxStreamIdx = 0;

				/* Currently we only support for 4 Tx Stream, so check only for 4 Rx Nss */
				bSupportVHTMCS1SS = FALSE;
				bSupportVHTMCS2SS = FALSE;
				bSupportVHTMCS3SS = FALSE;
				bSupportVHTMCS4SS = FALSE;
				for (TxStreamIdx = wlan_operate_get_tx_stream(pEntry->wdev);
					TxStreamIdx > 0;
					TxStreamIdx--) {
					switch (TxStreamIdx) {
					case 1:
						if (ie_list->vht_cap_ie.mcs_set.rx_mcs_map.mcs_ss1 < VHT_MCS_CAP_NA)
							bSupportVHTMCS1SS = TRUE;
						break;

					case 2:
						if (ie_list->vht_cap_ie.mcs_set.rx_mcs_map.mcs_ss2 < VHT_MCS_CAP_NA)
							bSupportVHTMCS2SS = TRUE;

						break;

					case 3:
						if (ie_list->vht_cap_ie.mcs_set.rx_mcs_map.mcs_ss3 < VHT_MCS_CAP_NA)
							bSupportVHTMCS3SS = TRUE;

						break;

					case 4:
						if (ie_list->vht_cap_ie.mcs_set.rx_mcs_map.mcs_ss4 < VHT_MCS_CAP_NA)
							bSupportVHTMCS4SS = TRUE;

						break;

					default:
						break;
					}
				}
			}
			if (pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth != ie_list->AddHtInfo.AddHtInfo.RecomWidth ||
				pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset != ie_list->AddHtInfo.AddHtInfo.ExtChanOffset) {
			pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth = ie_list->AddHtInfo.AddHtInfo.RecomWidth;
			pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset = ie_list->AddHtInfo.AddHtInfo.ExtChanOffset;
			ht_update_flag = wlan_operate_set_ht_bw(wdev, pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth, pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.ExtChanOffset);
			pEntry->HTPhyMode.field.BW = pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth;
			}

			if ((vht_update_flag != WLAN_OPER_FAIL)) {
			switch (ie_list->vht_op_ie.vht_op_info.ch_width) {
			case VHT_BW_80:
				pEntry->operating_mode.ch_width = BW_80;
				break;

			case VHT_BW_2040:
				if (pStaCfg->MlmeAux.AddHtInfo.AddHtInfo.RecomWidth == BW_40)
					pEntry->operating_mode.ch_width = BW_40;
				else
					pEntry->operating_mode.ch_width = BW_20;
				break;

			default:
				MTWF_LOG(DBG_CAT_CLIENT, DBG_SUBCAT_ALL, DBG_LVL_WARN,
					("SYNC - Channel Width = %u not implemented\n",
					pStaCfg->MlmeAux.vht_op.vht_op_info.ch_width));
				pEntry->operating_mode.ch_width = BW_80;
				break;
			}
			}

			/**
			 * Set to 0 to indicate that the Rx NSS subfield carries the maximum number of
			 * spatial streams that the STA can receive.
			 */
			nss_update_flag = pEntry->operating_mode.rx_nss;
			pEntry->operating_mode.rx_nss_type = 0;

			/**
			 * Find RX Mcs using following as mentioned in spec
			 * The Max VHT-MCS For n SS subfield (where n = 1, ..., 8) is encoded as follows:
			 * 0 indicates support for VHT-MCS 0-7 for n spatial streams
			 * 1 indicates support for VHT-MCS 0-8 for n spatial streams
			 * 2 indicates support for VHT-MCS 0-9 for n spatial streams
			 * 3 indicates that n spatial streams is not supported
			 * Set to 0 for NSS = 1
			 * Set to 1 for NSS = 2
			 * ...
			 * Set to 7 for NSS = 8
			 */
			if (bSupportVHTMCS4SS == TRUE)
				pEntry->operating_mode.rx_nss = 3;
			else if (bSupportVHTMCS3SS == TRUE)
				pEntry->operating_mode.rx_nss = 2;
			else if (bSupportVHTMCS2SS == TRUE)
				pEntry->operating_mode.rx_nss = 1;
			else
				pEntry->operating_mode.rx_nss = 0;

			pEntry->operating_mode.rsv2 = 0;
			if ((vht_update_flag != WLAN_OPER_FAIL) ||
				(nss_update_flag != pEntry->operating_mode.rx_nss) ||
				(ht_update_flag != WLAN_OPER_FAIL)) {
				MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_TRACE,
				 ("Update Reuiqred!!!!\n"
				 ));
				pStaCfg->MlmeAux.vht_cap_len = ie_list->vht_cap_len;
				NdisMoveMemory(&pStaCfg->MlmeAux.vht_cap,
						&ie_list->vht_cap_ie,
						pStaCfg->MlmeAux.vht_cap_len);
				pEntry->force_op_mode = TRUE;
				pStaCfg->MlmeAux.force_op_mode = pEntry->force_op_mode;
				NdisMoveMemory(&pStaCfg->MlmeAux.op_mode, &pEntry->operating_mode, 1);
				NdisZeroMemory(&rRaParam, sizeof(CMD_STAREC_AUTO_RATE_UPDATE_T));
				rRaParam.u4Field = RA_PARAM_VHT_OPERATING_MODE;
				RAParamUpdate(pAd, pEntry, &rRaParam);
				bAdjust = TRUE;
			}
		}
	}
#endif /* DOT11_VHT_AC */
#endif /* DOT11_N_SUPPORT */
	return bAdjust;
}
#endif



#ifdef CONFIG_AP_SUPPORT
#ifdef DOT11_N_SUPPORT
extern int DetectOverlappingPeriodicRound;

VOID Handle_BSS_Width_Trigger_Events(RTMP_ADAPTER *pAd, UCHAR Channel)
{
	ULONG Now32;
	UCHAR i;
#ifdef DOT11N_DRAFT3

	if (pAd->CommonCfg.bBssCoexEnable == FALSE)
		return;

#endif /* DOT11N_DRAFT3 */

	if ((Channel <= 14)) {
		for (i = 0; i < WDEV_NUM_MAX; i++) {
			struct wifi_dev *wdev;
			UCHAR ht_bw;
			wdev = pAd->wdev_list[i];

			if (!wdev || (wdev->channel != Channel) || (wdev->wdev_type != WDEV_TYPE_AP))
				continue;

			ht_bw = wlan_operate_get_ht_bw(wdev);

			if (ht_bw < HT_BW_40)
				continue;

			MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_TRACE, ("Rcv BSS Width Trigger Event: 40Mhz --> 20Mhz\n"));
			NdisGetSystemUpTime(&Now32);
			pAd->CommonCfg.LastRcvBSSWidthTriggerEventsTime = Now32;
			pAd->CommonCfg.bRcvBSSWidthTriggerEvents = TRUE;
			wlan_operate_set_ht_bw(wdev, HT_BW_20, EXTCHA_NONE);
		}

		DetectOverlappingPeriodicRound = 31;
	}
}
#endif /* DOT11_N_SUPPORT */
#endif /* CONFIG_AP_SUPPORT */

#ifdef DOT11_N_SUPPORT
#ifdef DOT11N_DRAFT3
VOID BuildEffectedChannelList(
	IN PRTMP_ADAPTER pAd,
	IN struct wifi_dev *wdev
)
{
	UCHAR		EChannel[11];
	UCHAR		k;
	UCHAR BandIdx = HcGetBandByWdev(wdev);
	CHANNEL_CTRL *pChCtrl = hc_get_channel_ctrl(pAd->hdev_ctrl, BandIdx);
#ifdef CONFIG_STA_SUPPORT
	PSTA_ADMIN_CONFIG pStaCfg = GetStaCfgByWdev(pAd, wdev);
	ASSERT(pStaCfg);

	if (!pStaCfg)
		return;

#endif /* CONFIG_STA_SUPPORT */
	RTMPZeroMemory(EChannel, 11);
	/* 802.11n D4 11.14.3.3: If no secondary channel has been selected, all channels in the frequency band shall be scanned. */
	{
		for (k = 0; k < pChCtrl->ChListNum; k++) {
			if (pChCtrl->ChList[k].Channel <= 14)
				pChCtrl->ChList[k].bEffectedChannel = TRUE;
		}

		return;
	}

}


#endif /* DOT11N_DRAFT3 */
#endif /* DOT11_N_SUPPORT */

UCHAR get_channel_bw_cap(struct wifi_dev *wdev, UCHAR channel)
{
	BOOLEAN find = FALSE;
	UCHAR i;
	UCHAR cap = BW_20;
	UINT flag;
	struct _RTMP_ADAPTER *ad = wdev->sys_handle;
	UCHAR BandIdx = HcGetBandByWdev(wdev);
	CHANNEL_CTRL *pChCtrl = hc_get_channel_ctrl(ad->hdev_ctrl, BandIdx);

	for (i = 0; (i < pChCtrl->ChListNum) && (i < MAX_NUM_OF_CHANNELS); i++) {
		if (channel == pChCtrl->ChList[i].Channel) {
			find = TRUE;
			flag = pChCtrl->ChList[i].Flags;
			break;
		}
	}

	if (find) {
		if (flag & CHANNEL_160M_CAP)
			cap = BW_160;
		else if (flag & CHANNEL_80M_CAP)
			cap = BW_80;
		else if (flag & CHANNEL_40M_CAP)
			cap = BW_40;
		else
			cap = BW_20;
	}

	return cap;
}
