#include "p2p_precomp.h"



UINT_32
p2pCalculate_IEForAssocReq (

    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    UINT_32 u4RetValue = 0;

    do {
        ASSERT_BREAK((eNetTypeIndex == NETWORK_TYPE_P2P_INDEX) && (prAdapter != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        u4RetValue = prConnReqInfo->u4BufLength;
// merge JRD377531(For_JRDSH77_CU_JB_ALPS.JB.MP.V1_P224).tar.gz  by chenglong.zhao for [Wifi][WifiP2P][TCP]Wifi p2p TCP can't work sometimes when wifi connect APN/A
        // ADD HT Capability
        u4RetValue += (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP);
		
		// ADD WMM Information Element
        u4RetValue += (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_INFO);

    } while (FALSE);
//end
    return u4RetValue;
} /* p2pCalculate_IEForAssocReq */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate P2P IE for Beacon frame.
*
* @param[in] prMsduInfo             Pointer to the composed MSDU_INFO_T.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
p2pGenerate_IEForAssocReq (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        pucIEBuf = (PUINT_8)((UINT_32)prMsduInfo->prPacket + (UINT_32)prMsduInfo->u2FrameLength);

        kalMemCopy(pucIEBuf, prConnReqInfo->aucIEBuf, prConnReqInfo->u4BufLength);

        prMsduInfo->u2FrameLength += prConnReqInfo->u4BufLength;
// merge JRD377531(For_JRDSH77_CU_JB_ALPS.JB.MP.V1_P224).tar.gz  by chenglong.zhao for [Wifi][WifiP2P][TCP]Wifi p2p TCP can't work sometimes when wifi connect APN/A
        rlmReqGenerateHtCapIE (prAdapter,prMsduInfo);
		mqmGenerateWmmInfoIE (prAdapter,prMsduInfo);

//end
    } while (FALSE);

    return;

} /* p2pGenerate_IEForAssocReq */


