/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file PHY/NR_TRANSPORT/pucch_rx.c
* \brief Top-level routines for decoding the PUCCH physical channel
* \author A. Mico Pereperez, Padarthi Naga Prasanth, Francesco Mani, Raymond Knopp
* \date 2020
* \version 0.2
* \company Eurecom
* \email:
* \note
* \warning
*/
#include<stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_gNB.h"
#include "PHY/sse_intrin.h"
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include <openair1/PHY/CODING/nrSmallBlock/nr_small_block_defs.h>
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"

#include "T.h"

//#define DEBUG_NR_PUCCH_RX 1

int get_pucch0_cs_lut_index(PHY_VARS_gNB *gNB,nfapi_nr_pucch_pdu_t* pucch_pdu) {

  int i=0;

#ifdef DEBUG_NR_PUCCH_RX
  printf("getting index for LUT with %d entries, Nid %d\n",gNB->pucch0_lut.nb_id, pucch_pdu->hopping_id);
#endif

  for (i=0;i<gNB->pucch0_lut.nb_id;i++) {
    if (gNB->pucch0_lut.Nid[i] == pucch_pdu->hopping_id) break;
  }
#ifdef DEBUG_NR_PUCCH_RX
  printf("found index %d\n",i);
#endif
  if (i<gNB->pucch0_lut.nb_id) return(i);

#ifdef DEBUG_NR_PUCCH_RX
  printf("Initializing PUCCH0 LUT index %i with Nid %d\n",i, pucch_pdu->hopping_id);
#endif
  // initialize
  gNB->pucch0_lut.Nid[gNB->pucch0_lut.nb_id]=pucch_pdu->hopping_id;
  for (int slot=0;slot<10<<pucch_pdu->subcarrier_spacing;slot++)
    for (int symbol=0;symbol<14;symbol++)
      gNB->pucch0_lut.lut[gNB->pucch0_lut.nb_id][slot][symbol] = (int)floor(nr_cyclic_shift_hopping(pucch_pdu->hopping_id,0,0,symbol,0,slot)/0.5235987756);
  gNB->pucch0_lut.nb_id++;
  return(gNB->pucch0_lut.nb_id-1);
}


  
int16_t idft12_re[12][12] = {
  {23170,23170,23170,23170,23170,23170,23170,23170,23170,23170,23170,23170},
  {23170,20066,11585,0,-11585,-20066,-23170,-20066,-11585,0,11585,20066},
  {23170,11585,-11585,-23170,-11585,11585,23170,11585,-11585,-23170,-11585,11585},
  {23170,0,-23170,0,23170,0,-23170,0,23170,0,-23170,0},
  {23170,-11585,-11585,23170,-11585,-11585,23170,-11585,-11585,23170,-11585,-11585},
  {23170,-20066,11585,0,-11585,20066,-23170,20066,-11585,0,11585,-20066},
  {23170,-23170,23170,-23170,23170,-23170,23170,-23170,23170,-23170,23170,-23170},
  {23170,-20066,11585,0,-11585,20066,-23170,20066,-11585,0,11585,-20066},
  {23170,-11585,-11585,23170,-11585,-11585,23170,-11585,-11585,23170,-11585,-11585},
  {23170,0,-23170,0,23170,0,-23170,0,23170,0,-23170,0},
  {23170,11585,-11585,-23170,-11585,11585,23170,11585,-11585,-23170,-11585,11585},
  {23170,20066,11585,0,-11585,-20066,-23170,-20066,-11585,0,11585,20066}
};

int16_t idft12_im[12][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,11585,20066,23170,20066,11585,0,-11585,-20066,-23170,-20066,-11585},
  {0,20066,20066,0,-20066,-20066,0,20066,20066,0,-20066,-20066},
  {0,23170,0,-23170,0,23170,0,-23170,0,23170,0,-23170},
  {0,20066,-20066,0,20066,-20066,0,20066,-20066,0,20066,-20066},
  {0,11585,-20066,23170,-20066,11585,0,-11585,20066,-23170,20066,-11585},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,-11585,20066,-23170,20066,-11585,0,11585,-20066,23170,-20066,11585},
  {0,-20066,20066,0,-20066,20066,0,-20066,20066,0,-20066,20066},
  {0,-23170,0,23170,0,-23170,0,23170,0,-23170,0,23170},
  {0,-20066,-20066,0,20066,20066,0,-20066,-20066,0,20066,20066},
  {0,-11585,-20066,-23170,-20066,-11585,0,11585,20066,23170,20066,11585}
};


void nr_decode_pucch0(PHY_VARS_gNB *gNB,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t* uci_pdu,
                      nfapi_nr_pucch_pdu_t* pucch_pdu) {


  int32_t **rxdataF = gNB->common_vars.rxdataF;
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;

  int nr_sequences;
  const uint8_t *mcs;

  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);

  AssertFatal(pucch_pdu->bit_len_harq > 0 || pucch_pdu->sr_flag > 0,
	      "Either bit_len_harq (%d) or sr_flag (%d) must be > 0\n",
	      pucch_pdu->bit_len_harq,pucch_pdu->sr_flag);

  if(pucch_pdu->bit_len_harq==0){
    mcs=table1_mcs;
    nr_sequences=1;
  }
  else if(pucch_pdu->bit_len_harq==1){
    mcs=table1_mcs;
    nr_sequences=4>>(1-pucch_pdu->sr_flag);
  }
  else{
    mcs=table2_mcs;
    nr_sequences=8>>(1-pucch_pdu->sr_flag);
  }

  int cs_ind = get_pucch0_cs_lut_index(gNB,pucch_pdu);
  /*
   * Implement TS 38.211 Subclause 6.3.2.3.1 Sequence generation
   *
   */
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // alpha is cyclic shift
  double alpha;
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  //uint8_t lprime;

  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */

  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.1, the sequence x(n) shall be generated according to:
   * x(l*12+n) = r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled by the higher-layer parameter PUCCH-frequency-hopping
  //              n_hop = 0
  // if frequency hopping is enabled by the higher-layer parameter PUCCH-frequency-hopping
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;  // Frequnecy hopping not implemented FIXME!!

  // x_n contains the sequence r_u_v_alpha_delta(n)

  int n,i,l;
  nr_group_sequence_hopping(pucch_GroupHopping,pucch_pdu->hopping_id,n_hop,slot,&u,&v); // calculating u and v value

  uint32_t re_offset=0;
  uint8_t l2;

#ifdef OLD_IMPL
  int16_t x_n_re[nr_sequences][24],x_n_im[nr_sequences][24];

  for(i=0;i<nr_sequences;i++){ 
  // we proceed to calculate alpha according to TS 38.211 Subclause 6.3.2.2.2
    for (l=0; l<pucch_pdu->nr_of_symbols; l++){
      alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id,pucch_pdu->initial_cyclic_shift,mcs[i],l,pucch_pdu->start_symbol_index,slot);
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch0] sequence generation \tu=%d \tv=%d \talpha=%lf \t(for symbol l=%d/%d,mcs %d)\n",u,v,alpha,l,l+pucch_pdu->start_symbol_index,mcs[i]);
      printf("lut output %d\n",gNB->pucch0_lut.lut[cs_ind][slot][l+pucch_pdu->start_symbol_index]);
#endif
      alpha=0.0;
      for (n=0; n<12; n++){
        x_n_re[i][(12*l)+n] = (int16_t)((int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                  - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)))); // Re part of base sequence shifted by alpha
        x_n_im[i][(12*l)+n] =(int16_t)((int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                  + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)))); // Im part of base sequence shifted by alpha
#ifdef DEBUG_NR_PUCCH_RX
          printf("\t [nr_generate_pucch0] sequence generation \tu=%d \tv=%d \talpha=%lf \tx_n(l=%d,n=%d)=(%d,%d) %d,%d\n",
		 u,v,alpha,l,n,x_n_re[i][(12*l)+n],x_n_im[i][(12*l)+n],
		 (int32_t)(round(32767*cos(alpha*n))),
		 (int32_t)(round(32767*sin(alpha*n))));
#endif
      }
    }
  }
  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.2 Mapping to physical resources
   */

  int16_t r_re[24],r_im[24];

  for (l=0; l<pucch_pdu->nr_of_symbols; l++) {

    l2 = l+pucch_pdu->start_symbol_index;
    re_offset = (12*pucch_pdu->prb_start) + (12*pucch_pdu->bwp_start) + frame_parms->first_carrier_offset;
    if (re_offset>= frame_parms->ofdm_symbol_size) 
      re_offset-=frame_parms->ofdm_symbol_size;

    for (n=0; n<12; n++){

      r_re[(12*l)+n]=((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[0];
      r_im[(12*l)+n]=((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[1];
      #ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch0] mapping to RE \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \ttxptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
                frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,(l2*frame_parms->ofdm_symbol_size)+re_offset,
                l,n,((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[0],
                ((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[1]);
      #endif
      re_offset++;
      if (re_offset>= frame_parms->ofdm_symbol_size) 
        re_offset-=frame_parms->ofdm_symbol_size;
    }
  }  
  double corr[nr_sequences],corr_re[nr_sequences],corr_im[nr_sequences];
  memset(corr,0,nr_sequences*sizeof(double));
  memset(corr_re,0,nr_sequences*sizeof(double));
  memset(corr_im,0,nr_sequences*sizeof(double));
  for(i=0;i<nr_sequences;i++){
    for(l=0;l<pucch_pdu->nr_of_symbols;l++){
      for(n=0;n<12;n++){
        corr_re[i]+= (double)(r_re[12*l+n])/32767*(double)(x_n_re[i][12*l+n])/32767+(double)(r_im[12*l+n])/32767*(double)(x_n_im[i][12*l+n])/32767;
	corr_im[i]+= (double)(r_re[12*l+n])/32767*(double)(x_n_im[i][12*l+n])/32767-(double)(r_im[12*l+n])/32767*(double)(x_n_re[i][12*l+n])/32767;
      }
    }
    corr[i]=corr_re[i]*corr_re[i]+corr_im[i]*corr_im[i];
  }
  float max_corr=corr[0];
  uint8_t index=0;
  for(i=1;i<nr_sequences;i++){
    if(corr[i]>max_corr){
      index= i;
      max_corr=corr[i];
    }
  }
#else

  const int16_t *x_re = table_5_2_2_2_2_Re[u],*x_im = table_5_2_2_2_2_Im[u];
  int16_t xr[24]  __attribute__((aligned(32)));
  int16_t xrt[24] __attribute__((aligned(32)));
  int32_t xrtmag=0;
  int maxpos=0;
  int n2=0;
  uint8_t index=0;
  memset((void*)xr,0,24*sizeof(int16_t));

  for (l=0; l<pucch_pdu->nr_of_symbols; l++) {

    l2 = l+pucch_pdu->start_symbol_index;
    re_offset = (12*pucch_pdu->prb_start) + frame_parms->first_carrier_offset;
    if (re_offset>= frame_parms->ofdm_symbol_size) 
      re_offset-=frame_parms->ofdm_symbol_size;
  
    AssertFatal(re_offset+12 < frame_parms->ofdm_symbol_size,"pucch straddles DC carrier, handle this!\n");

    int16_t *r=(int16_t*)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size+re_offset)];
    for (n=0;n<12;n++,n2+=2) {
      xr[n2]  =(int16_t)(((int32_t)x_re[n]*r[n2]+(int32_t)x_im[n]*r[n2+1])>>15);
      xr[n2+1]=(int16_t)(((int32_t)x_re[n]*r[n2+1]-(int32_t)x_im[n]*r[n2])>>15);
#ifdef DEBUG_NR_PUCCH_RX
      printf("x (%d,%d), r (%d,%d), xr (%d,%d)\n",
	     x_re[n],x_im[n],r[n2],r[n2+1],xr[n2],xr[n2+1]);
#endif
    }
  }
  int32_t corr_re,corr_im,temp;
  int seq_index;

  for(i=0;i<nr_sequences;i++){
    corr_re=0;corr_im=0;
    n2=0;
    for (l=0;l<pucch_pdu->nr_of_symbols;l++) {

       seq_index = (pucch_pdu->initial_cyclic_shift+
		   mcs[i]+
		   gNB->pucch0_lut.lut[cs_ind][slot][l+pucch_pdu->start_symbol_index])%12;
      for (n=0;n<12;n++,n2+=2) {
	corr_re+=(xr[n2]*idft12_re[seq_index][n]+xr[n2+1]*idft12_im[seq_index][n])>>15;
	corr_im+=(xr[n2]*idft12_im[seq_index][n]-xr[n2+1]*idft12_re[seq_index][n])>>15;
      }
    }

#ifdef DEBUG_NR_PUCCH_RX
    printf("PUCCH IDFT[%d/%d] = (%d,%d)=>%f\n",mcs[i],seq_index,corr_re,corr_im,10*log10(corr_re*corr_re + corr_im*corr_im));
#endif
    if ((temp=corr_re*corr_re + corr_im*corr_im)>xrtmag) {
      xrtmag=temp;
      maxpos=i;
    }
  }

  uint8_t xrtmag_dB = dB_fixed(xrtmag);
  
#ifdef DEBUG_NR_PUCCH_RX
  printf("PUCCH 0 : maxpos %d\n",maxpos);
#endif

  index=maxpos;
#endif
  // first bit of bitmap for sr presence and second bit for acknack presence
  uci_pdu->pdu_bit_map = pucch_pdu->sr_flag | ((pucch_pdu->bit_len_harq>0)<<1);
  uci_pdu->pucch_format = 0; // format 0
  uci_pdu->ul_cqi = 0xff; // currently not valid
  uci_pdu->timing_advance = 0xffff; // currently not valid
  uci_pdu->rssi = 0xffff; // currently not valid

  if (pucch_pdu->bit_len_harq==0) {
    uci_pdu->harq = NULL;
    uci_pdu->sr = calloc(1,sizeof(*uci_pdu->sr));
    if (xrtmag_dB>(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres)) {
      uci_pdu->sr->sr_indication = 1;
      uci_pdu->sr->sr_confidence_level = xrtmag_dB-(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres);
    } else {
      uci_pdu->sr->sr_indication = 0;
      uci_pdu->sr->sr_confidence_level = (gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres)-xrtmag_dB;
    }
  }
  else if (pucch_pdu->bit_len_harq==1) {
    uci_pdu->harq = calloc(1,sizeof(*uci_pdu->harq));
    uci_pdu->harq->num_harq = 1;
    uci_pdu->harq->harq_confidence_level = xrtmag_dB-(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres);
    uci_pdu->harq->harq_list = (nfapi_nr_harq_t*)malloc(1);
    uci_pdu->harq->harq_list[0].harq_value = index&0x01;
    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr = calloc(1,sizeof(*uci_pdu->sr));
      uci_pdu->sr->sr_indication = (index>1) ? 1 : 0;
      uci_pdu->sr->sr_confidence_level = xrtmag_dB-(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres);
    }
  }
  else {
    uci_pdu->harq = calloc(1,sizeof(*uci_pdu->harq));
    uci_pdu->harq->num_harq = 2;
    uci_pdu->harq->harq_confidence_level = xrtmag_dB-(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres);
    uci_pdu->harq->harq_list = (nfapi_nr_harq_t*)malloc(2);

    uci_pdu->harq->harq_list[0].harq_value = index&0x01;
    uci_pdu->harq->harq_list[1].harq_value = (index>>1)&0x01;

    if (pucch_pdu->sr_flag == 1) {
      uci_pdu->sr = calloc(1,sizeof(*uci_pdu->sr));
      uci_pdu->sr->sr_indication = (index>3) ? 1 : 0;
      uci_pdu->sr->sr_confidence_level = xrtmag_dB-(gNB->measurements.n0_subband_power_tot_dB[pucch_pdu->prb_start]+gNB->pucch0_thres);
    }
  }
}





void nr_decode_pucch1(  int32_t **rxdataF,
		        pucch_GroupHopping_t pucch_GroupHopping,
                        uint32_t n_id,       // hoppingID higher layer parameter  
                        uint64_t *payload,
		       	NR_DL_FRAME_PARMS *frame_parms, 
                        int16_t amp,
                        int nr_tti_tx,
                        uint8_t m0,
                        uint8_t nrofSymbols,
                        uint8_t startingSymbolIndex,
                        uint16_t startingPRB,
                        uint16_t startingPRB_intraSlotHopping,
                        uint8_t timeDomainOCC,
                        uint8_t nr_bit) {
#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] start function at slot(nr_tti_tx)=%d payload=%lp m0=%d nrofSymbols=%d startingSymbolIndex=%d startingPRB=%d startingPRB_intraSlotHopping=%d timeDomainOCC=%d nr_bit=%d\n",
         nr_tti_tx,payload,m0,nrofSymbols,startingSymbolIndex,startingPRB,startingPRB_intraSlotHopping,timeDomainOCC,nr_bit);
#endif
  /*
   * Implement TS 38.211 Subclause 6.3.2.4.1 Sequence modulation
   *
   */
  // complex-valued symbol d_re, d_im containing complex-valued symbol d(0):
  int16_t d_re=0, d_im=0,d1_re=0,d1_im=0;
#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] sequence modulation: payload=%lp \tde_re=%d \tde_im=%d\n",payload,d_re,d_im);
#endif
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // alpha is cyclic shift
  double alpha;
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal = 0 ;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  uint8_t lprime = startingSymbolIndex;
  // mcs = 0 except for PUCCH format 0
  uint8_t mcs=0;
  // r_u_v_alpha_delta_re and r_u_v_alpha_delta_im tables containing the sequence y(n) for the PUCCH, when they are multiplied by d(0)
  // r_u_v_alpha_delta_dmrs_re and r_u_v_alpha_delta_dmrs_im tables containing the sequence for the DM-RS.
  int16_t r_u_v_alpha_delta_re[12],r_u_v_alpha_delta_im[12],r_u_v_alpha_delta_dmrs_re[12],r_u_v_alpha_delta_dmrs_im[12];
  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  /*
   * the complex-valued symbol d_0 shall be multiplied with a sequence r_u_v_alpha_delta(n): y(n) = d_0 * r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled, intraSlotFrequencyHopping is not provided
  //              n_hop = 0
  // if frequency hopping is enabled,  intraSlotFrequencyHopping is     provided
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;
  // Intra-slot frequency hopping shall be assumed when the higher-layer parameter intraSlotFrequencyHopping is provided,
  // regardless of whether the frequency-hop distance is zero or not,
  // otherwise no intra-slot frequency hopping shall be assumed
  //uint8_t PUCCH_Frequency_Hopping = 0 ; // from higher layers
  uint8_t intraSlotFrequencyHopping = 0;

  if (startingPRB != startingPRB_intraSlotHopping) {
    intraSlotFrequencyHopping=1;
  }

#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] intraSlotFrequencyHopping = %d \n",intraSlotFrequencyHopping);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.4.2 Mapping to physical resources
   */
  //int32_t *txptr;
  uint32_t re_offset=0;
  int i=0;
#define MAX_SIZE_Z 168 // this value has to be calculated from mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n
  int16_t z_re_rx[MAX_SIZE_Z],z_im_rx[MAX_SIZE_Z],z_re_temp,z_im_temp;
  int16_t z_dmrs_re_rx[MAX_SIZE_Z],z_dmrs_im_rx[MAX_SIZE_Z],z_dmrs_re_temp,z_dmrs_im_temp;
  memset(z_re_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_im_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_dmrs_re_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_dmrs_im_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  int l=0;
  for(l=0;l<nrofSymbols;l++){     //extracting data and dmrs from rxdataF
    if ((intraSlotFrequencyHopping == 1) && (l<floor(nrofSymbols/2))) { // intra-slot hopping enabled, we need to calculate new offset PRB
      startingPRB = startingPRB + startingPRB_intraSlotHopping;
    }

    if ((startingPRB <  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 0)) { // if number RBs in bandwidth is even and current PRB is lower band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    if ((startingPRB >= (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 0)) { // if number RBs in bandwidth is even and current PRB is upper band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*(startingPRB-(frame_parms->N_RB_DL>>1)));
    }

    if ((startingPRB <  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB is lower band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    if ((startingPRB >  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB is upper band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*(startingPRB-(frame_parms->N_RB_DL>>1))) + 6;
    }

    if ((startingPRB == (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB contains DC
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    for (int n=0; n<12; n++) {
      if ((n==6) && (startingPRB == (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) {
        // if number RBs in bandwidth is odd  and current PRB contains DC, we need to recalculate the offset when n=6 (for second half PRB)
        re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size);
      }

      if (l%2 == 1) { // mapping PUCCH according to TS38.211 subclause 6.4.1.3.1
        z_re_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[0];
        z_im_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[1];
#ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch1] mapping PUCCH to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \tz_pucch[%d]=txptr(%u)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp,frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,i+n,re_offset,
               l,n,((int16_t *)&rxdataF[0][re_offset])[0],((int16_t *)&rxdataF[0][re_offset])[1]);
#endif
      }

      if (l%2 == 0) { // mapping DM-RS signal according to TS38.211 subclause 6.4.1.3.1
        z_dmrs_re_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[0];
        z_dmrs_im_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[1];
//	printf("%d\t%d\t%d\n",l,z_dmrs_re_rx[i+n],z_dmrs_im_rx[i+n]);
#ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch1] mapping DM-RS to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \tz_dm-rs[%d]=txptr(%u)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp,frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,i+n,re_offset,
               l,n,((int16_t *)&rxdataF[0][re_offset])[0],((int16_t *)&rxdataF[0][re_offset])[1]);
#endif
//        printf("l=%d\ti=%d\tre_offset=%d\treceived dmrs re=%d\tim=%d\n",l,i,re_offset,z_dmrs_re_rx[i+n],z_dmrs_im_rx[i+n]);
      }

      re_offset++;
    }
    if (l%2 == 1) i+=12;
  }
  int16_t y_n_re[12],y_n_im[12],y1_n_re[12],y1_n_im[12];
  memset(y_n_re,0,12*sizeof(int16_t));
  memset(y_n_im,0,12*sizeof(int16_t));
  memset(y1_n_re,0,12*sizeof(int16_t));
  memset(y1_n_im,0,12*sizeof(int16_t));
  //generating transmitted sequence and dmrs
  for (l=0; l<nrofSymbols; l++) {
#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_generate_pucch1] for symbol l=%d, lprime=%d\n",
           l,lprime);
#endif
    // y_n contains the complex value d multiplied by the sequence r_u_v
   if ((intraSlotFrequencyHopping == 1) && (l >= (int)floor(nrofSymbols/2))) n_hop = 1; // n_hop = 1 for second hop

#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_generate_pucch1] entering function nr_group_sequence_hopping with n_hop=%d, nr_tti_tx=%d\n",
           n_hop,nr_tti_tx);
#endif
    nr_group_sequence_hopping(pucch_GroupHopping,n_id,n_hop,nr_tti_tx,&u,&v); // calculating u and v value
    alpha = nr_cyclic_shift_hopping(n_id,m0,mcs,l,lprime,nr_tti_tx);
    
    for (int n=0; n<12; n++) {  // generating low papr sequences
      if(l%2==1){ 
        r_u_v_alpha_delta_re[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                             - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15))); // Re part of base sequence shifted by alpha
        r_u_v_alpha_delta_im[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                             + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15))); // Im part of base sequence shifted by alpha
      }
      else{
        r_u_v_alpha_delta_dmrs_re[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                       - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15))); // Re part of DMRS base sequence shifted by alpha
        r_u_v_alpha_delta_dmrs_im[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                       + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15))); // Im part of DMRS base sequence shifted by alpha
        r_u_v_alpha_delta_dmrs_re[n] = (int16_t)(((int32_t)(amp*r_u_v_alpha_delta_dmrs_re[n]))>>15);
        r_u_v_alpha_delta_dmrs_im[n] = (int16_t)(((int32_t)(amp*r_u_v_alpha_delta_dmrs_im[n]))>>15);
      }
//      printf("symbol=%d\tr_u_rx_re=%d\tr_u_rx_im=%d\n",l,r_u_v_alpha_delta_dmrs_re[n], r_u_v_alpha_delta_dmrs_im[n]);
      // PUCCH sequence = DM-RS sequence multiplied by d(0)
/*      y_n_re[n]               = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*d_re)>>15)
                                           - (((int32_t)(r_u_v_alpha_delta_im[n])*d_im)>>15))); // Re part of y(n)
      y_n_im[n]               = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*d_im)>>15)
                                           + (((int32_t)(r_u_v_alpha_delta_im[n])*d_re)>>15))); // Im part of y(n) */
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] sequence generation \tu=%d \tv=%d \talpha=%lf \tr_u_v_alpha_delta[n=%d]=(%d,%d) \ty_n[n=%d]=(%d,%d)\n",
             u,v,alpha,n,r_u_v_alpha_delta_re[n],r_u_v_alpha_delta_im[n],n,y_n_re[n],y_n_im[n]);
#endif
    }
    /*
     * The block of complex-valued symbols y(n) shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     * The block of complex-valued symbols r_u_v_alpha_dmrs_delta(n) for DM-RS shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     */
    // the orthogonal sequence index for wi(m) defined in TS 38.213 Subclause 9.2.1
    // the index of the orthogonal cover code is from a set determined as described in [4, TS 38.211]
    // and is indicated by higher layer parameter PUCCH-F1-time-domain-OCC
    // In the PUCCH_Config IE, the PUCCH-format1, timeDomainOCC field
    uint8_t w_index = timeDomainOCC;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime_PUCCH_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime_PUCCH_DMRS_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime0_PUCCH_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime0_PUCCH_DMRS_1;
    // mprime is 0 if no intra-slot hopping / mprime is {0,1} if intra-slot hopping
    uint8_t mprime = 0;

    if (intraSlotFrequencyHopping == 0) { // intra-slot hopping disabled
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping disabled\n",
             intraSlotFrequencyHopping);
#endif
      N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled (PUCCH)
      N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled (DM-RS)
      N_SF_mprime0_PUCCH_1      =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled mprime = 0 (PUCCH)
      N_SF_mprime0_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif
      if(l%2==1){
        for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
	  if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)){
            for (int n=0; n<12 ; n++) {
              z_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                  + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                  - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]=z_re_temp; 
              z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]=z_im_temp; 
//	      printf("symbol=%d\tz_re_rx=%d\tz_im_rx=%d\t",l,(int)z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#ifdef DEBUG_NR_PUCCH_RX
              printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                     mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],
                     z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif   
	      // multiplying with conjugate of low papr sequence  
	      z_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                                              + (((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 
              z_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                                              - (((int32_t)(r_u_v_alpha_delta_im[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp;
              z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp;
/*	      if(z_re_temp<0){
                      printf("\nBug detection %d\t%d\t%d\t%d\n",r_u_v_alpha_delta_re[n],z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15),(((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15));
              }
	      printf("z1_re_rx=%d\tz1_im_rx=%d\n",(int)z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]); */ 
	    }
	  }
        }
      }

      else{
        for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
          if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)){
            for (int n=0; n<12 ; n++) {
              z_dmrs_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                  + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
              z_dmrs_im_temp =  (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                  - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
              z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp;
              z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp;
//              printf("symbol=%d\tz_dmrs_re_rx=%d\tz_dmrs_im_rx=%d\t",l,(int)z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#ifdef DEBUG_NR_PUCCH_RX
              printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                     mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],
                     z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif
              //finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and z_dmrs_im_rx arrays
              z_dmrs_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                           + (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1); 
              z_dmrs_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                           - (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
/*	      if(z_dmrs_re_temp<0){
		      printf("\nBug detection %d\t%d\t%d\t%d\n",r_u_v_alpha_delta_dmrs_re[n],z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15),(((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15));
	      }*/
	      z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp;
	      z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 
//	      printf("z1_dmrs_re_rx=%d\tz1_dmrs_im_rx=%d\n",(int)z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
	     /* z_dmrs_re_rx[(int)(l/2)*12+n]=z_dmrs_re_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_re[n]; 
              z_dmrs_im_rx[(int)(l/2)*12+n]=z_dmrs_im_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_im[n]; */
	    }
	  }
        }
      }
    }

    if (intraSlotFrequencyHopping == 1) { // intra-slot hopping enabled
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping enabled\n",
             intraSlotFrequencyHopping);
#endif
      N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
      N_SF_mprime0_PUCCH_1      =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      N_SF_mprime0_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif

      for (mprime = 0; mprime<2; mprime++) { // mprime can get values {0,1}
	if(l%2==1){
          for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
            if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)){
              for (int n=0; n<12 ; n++) {
                z_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                             + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
                z_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                              - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
                z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp;
                z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp;
#ifdef DEBUG_NR_PUCCH_RX
                printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                       mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],
                       z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif 
                z_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                            + (((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 
                z_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                             - (((int32_t)(r_u_v_alpha_delta_im[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 	  
	        z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp; 
                z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp; 
	      }
	    }
	  }
        }

	else{
	  for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
            if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)){
              for (int n=0; n<12 ; n++) {
                z_dmrs_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                   + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
                z_dmrs_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                   - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
                z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp; 
                z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 
#ifdef DEBUG_NR_PUCCH_RX
                printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                       mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],
                       z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif
                //finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and z_dmrs_im_rx arrays
                z_dmrs_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                  + (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1); 
                z_dmrs_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                  - (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
	        z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp; 
                z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 

	    /* 	z_dmrs_re_rx[(int)(l/2)*12+n]=z_dmrs_re_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_re[n]; 
                z_dmrs_im_rx[(int)(l/2)*12+n]=z_dmrs_im_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_im[n]; */
	      }
	    }
	  }
        }

        N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 1 (PUCCH)
        N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 1 (DM-RS)
      }
    }
  }
  int16_t H_re[12],H_im[12],H1_re[12],H1_im[12];
  memset(H_re,0,12*sizeof(int16_t));
  memset(H_im,0,12*sizeof(int16_t));
  memset(H1_re,0,12*sizeof(int16_t));
  memset(H1_im,0,12*sizeof(int16_t)); 
  //averaging channel coefficients
  for(l=0;l<=ceil(nrofSymbols/2);l++){
    if(intraSlotFrequencyHopping==0){
      for(int n=0;n<12;n++){
        H_re[n]=round(z_dmrs_re_rx[l*12+n]/ceil(nrofSymbols/2))+H_re[n];
        H_im[n]=round(z_dmrs_im_rx[l*12+n]/ceil(nrofSymbols/2))+H_im[n];
      }
    }
    else{
      if(l<round(nrofSymbols/4)){
        for(int n=0;n<12;n++){
          H_re[n]=round(z_dmrs_re_rx[l*12+n]/round(nrofSymbols/4))+H_re[n];
          H_im[n]=round(z_dmrs_im_rx[l*12+n]/round(nrofSymbols/4))+H_im[n];
	}
      }
      else{
        for(int n=0;n<12;n++){
          H1_re[n]=round(z_dmrs_re_rx[l*12+n]/(ceil(nrofSymbols/2)-round(nrofSymbols/4)))+H1_re[n];
          H1_im[n]=round(z_dmrs_im_rx[l*12+n]/(ceil(nrofSymbols/2))-round(nrofSymbols/4))+H1_im[n];
	} 
      }
    }
  }
  //averaging information sequences
  for(l=0;l<floor(nrofSymbols/2);l++){
    if(intraSlotFrequencyHopping==0){
      for(int n=0;n<12;n++){
        y_n_re[n]=round(z_re_rx[l*12+n]/floor(nrofSymbols/2))+y_n_re[n];
        y_n_im[n]=round(z_im_rx[l*12+n]/floor(nrofSymbols/2))+y_n_im[n];
      }
    }
    else{
      if(l<floor(nrofSymbols/4)){
        for(int n=0;n<12;n++){
          y_n_re[n]=round(z_re_rx[l*12+n]/floor(nrofSymbols/4))+y_n_re[n];
          y_n_im[n]=round(z_im_rx[l*12+n]/floor(nrofSymbols/4))+y_n_im[n];
      }	     
    }
      else{
        for(int n=0;n<12;n++){
          y1_n_re[n]=round(z_re_rx[l*12+n]/round(nrofSymbols/4))+y1_n_re[n];
          y1_n_im[n]=round(z_im_rx[l*12+n]/round(nrofSymbols/4))+y1_n_im[n];
        }
      }	
    }
  }
  // mrc combining to obtain z_re and z_im
  if(intraSlotFrequencyHopping==0){
    for(int n=0;n<12;n++){
      d_re = round(((int16_t)(((((int32_t)(H_re[n])*y_n_re[n])>>15) + (((int32_t)(H_im[n])*y_n_im[n])>>15))>>1))/12)+d_re; 
      d_im = round(((int16_t)(((((int32_t)(H_re[n])*y_n_im[n])>>15) - (((int32_t)(H_im[n])*y_n_re[n])>>15))>>1))/12)+d_im; 
    }
  }
  else{
    for(int n=0;n<12;n++){
      d_re = round(((int16_t)(((((int32_t)(H_re[n])*y_n_re[n])>>15) + (((int32_t)(H_im[n])*y_n_im[n])>>15))>>1))/12)+d_re; 
      d_im = round(((int16_t)(((((int32_t)(H_re[n])*y_n_im[n])>>15) - (((int32_t)(H_im[n])*y_n_re[n])>>15))>>1))/12)+d_im;
      d1_re = round(((int16_t)(((((int32_t)(H1_re[n])*y1_n_re[n])>>15) + (((int32_t)(H1_im[n])*y1_n_im[n])>>15))>>1))/12)+d1_re; 
      d1_im = round(((int16_t)(((((int32_t)(H1_re[n])*y1_n_im[n])>>15) - (((int32_t)(H1_im[n])*y1_n_re[n])>>15))>>1))/12)+d1_im; 
    }
    d_re=round(d_re/2);
    d_im=round(d_im/2);
    d1_re=round(d1_re/2);
    d1_im=round(d1_im/2);
    d_re=d_re+d1_re;
    d_im=d_im+d1_im;
  }
  //Decoding QPSK or BPSK symbols to obtain payload bits
  if(nr_bit==1){
   if((d_re+d_im)>0){
     *payload=0;
   }
   else{
     *payload=1;
   } 
  }
  else if(nr_bit==2){
    if((d_re>0)&&(d_im>0)){
      *payload=0;
    }
    else if((d_re<0)&&(d_im>0)){
      *payload=1;
    } 
    else if((d_re>0)&&(d_im<0)){
      *payload=2;
    }
    else{
      *payload=3;
    }
  }
}

__m256i pucch2_3bit[8*2];
__m256i pucch2_4bit[16*2];
__m256i pucch2_5bit[32*2];
__m256i pucch2_6bit[64*2];
__m256i pucch2_7bit[128*2];
__m256i pucch2_8bit[256*2];
__m256i pucch2_9bit[512*2];
__m256i pucch2_10bit[1024*2];
__m256i pucch2_11bit[2048*2];

__m256i *pucch2_lut[9]={pucch2_3bit,
			pucch2_4bit,
			pucch2_5bit,
			pucch2_6bit,
			pucch2_7bit,
			pucch2_8bit,
			pucch2_9bit,
			pucch2_10bit,
			pucch2_11bit};

void init_pucch2_luts() {

  uint32_t out;
  int8_t bit; 
  
  for (int b=3;b<12;b++) {
    for (uint16_t i=0;i<(1<<b);i++) {
      out=encodeSmallBlock(&i,b);
      if (b==3) printf("in %d, out %x\n",i,out);
      __m256i *lut_i=&pucch2_lut[b-3][i<<1];
      __m256i *lut_ip1=&pucch2_lut[b-3][1+(i<<1)];
      bit = (out&0x1) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,0);
      bit = (out&0x2) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,0);
      bit = (out&0x4) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,1);
      bit = (out&0x8) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,1);
      bit = (out&0x10) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,2);
      bit = (out&0x20) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,2);
      bit = (out&0x40) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,3);
      bit = (out&0x80) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,3);
      bit = (out&0x100) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,4);
      bit = (out&0x200) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,4);
      bit = (out&0x400) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,5);
      bit = (out&0x800) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,5);
      bit = (out&0x1000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,6);
      bit = (out&0x2000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,6);
      bit = (out&0x4000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,7);
      bit = (out&0x8000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,7);
      bit = (out&0x10000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,8);
      bit = (out&0x20000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,8);
      bit = (out&0x40000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,9);
      bit = (out&0x80000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,9);
      bit = (out&0x100000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,10);
      bit = (out&0x200000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,10);
      bit = (out&0x400000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,11);
      bit = (out&0x800000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,11);
      bit = (out&0x1000000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,12);
      bit = (out&0x2000000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,12);
      bit = (out&0x4000000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,13);
      bit = (out&0x8000000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,13);
      bit = (out&0x10000000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,14);
      bit = (out&0x20000000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,14);
      bit = (out&0x40000000) > 0 ? -1 : 1;
      *lut_i = _mm256_insert_epi16(*lut_i,bit,15);
      bit = (out&0x80000000) > 0 ? -1 : 1;
      *lut_ip1 = _mm256_insert_epi16(*lut_ip1,bit,15);
    }
  }
}


void nr_decode_pucch2(PHY_VARS_gNB *gNB,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_2_3_4_t* uci_pdu,
                      nfapi_nr_pucch_pdu_t* pucch_pdu) {

  int32_t **rxdataF = gNB->common_vars.rxdataF;
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);

  AssertFatal(pucch_pdu->nr_of_symbols==1 || pucch_pdu->nr_of_symbols==2,
	      "Illegal number of symbols  for PUCCH 2 %d\n",pucch_pdu->nr_of_symbols);


  //extract pucch and dmrs first

  int l2=-1;
  int re_offset = (12*pucch_pdu->prb_start) + (12*pucch_pdu->bwp_start) + frame_parms->first_carrier_offset;
  if (re_offset>= frame_parms->ofdm_symbol_size) 
    re_offset-=frame_parms->ofdm_symbol_size;
  
  AssertFatal(pucch_pdu->prb_size*pucch_pdu->nr_of_symbols > 1,"number of PRB*SYMB (%d,%d)< 2",
	      pucch_pdu->prb_size,pucch_pdu->nr_of_symbols);

  int Prx = gNB->gNB_config.carrier_config.num_rx_ant.value;
  int Prx2 = (Prx==1)?2:Prx;
  // use 2 for Nb antennas in case of single antenna to allow the following allocations
  int16_t r_re_ext[Prx2][8*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));
  int16_t r_im_ext[Prx2][8*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));
  int16_t r_re_ext2[Prx2][8*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));
  int16_t r_im_ext2[Prx2][8*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));
  int16_t rd_re_ext[Prx2][4*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));
  int16_t rd_im_ext[Prx2][4*pucch_pdu->nr_of_symbols*pucch_pdu->prb_size] __attribute__((aligned(32)));

  int16_t *rp[Prx2];
  __m64 dmrs_re,dmrs_im;

  for (int aa=0;aa<Prx;aa++) rp[aa] = ((int16_t *)&rxdataF[aa][(l2*frame_parms->ofdm_symbol_size)+re_offset]);

#ifdef DEBUG_NR_PUCCH_RX
  printf("Decoding pucch2 for %d symbols, %d PRB\n",pucch_pdu->nr_of_symbols,pucch_pdu->prb_size);
#endif

  int nc_group_size=1; // 2 PRB
  int ngroup = pucch_pdu->prb_size/nc_group_size/2;
  int32_t corr32_re[ngroup][Prx2],corr32_im[ngroup][Prx2];
  for (int aa=0;aa<Prx;aa++) for (int group=0;group<ngroup;group++) { corr32_re[group][aa]=0; corr32_im[group][aa]=0;}

  if (pucch_pdu->nr_of_symbols == 1) {
     AssertFatal((pucch_pdu->prb_size&1) == 0,"prb_size %d is not a multiple of 2\n",pucch_pdu->prb_size);
     // 24 PRBs contains 48x16-bit, so 6x8x16-bit 
     for (int prb=0;prb<pucch_pdu->prb_size;prb+=2) {
        for (int aa=0;aa<Prx;aa++) {

	  r_re_ext[aa][0]=rp[aa][0];
	  r_im_ext[aa][0]=rp[aa][1];
	  rd_re_ext[aa][0]=rp[aa][2];
	  rd_im_ext[aa][0]=rp[aa][3];
	  r_re_ext[aa][1]=rp[aa][4];
	  r_im_ext[aa][1]=rp[aa][5];

	  r_re_ext[aa][2]=rp[aa][6];
	  r_im_ext[aa][2]=rp[aa][7];
	  rd_re_ext[aa][1]=rp[aa][8];
	  rd_im_ext[aa][1]=rp[aa][9];
	  r_re_ext[aa][3]=rp[aa][10];
	  r_im_ext[aa][3]=rp[aa][11];

	  r_re_ext[aa][4]=rp[aa][12];
	  r_im_ext[aa][4]=rp[aa][13];
	  rd_re_ext[aa][2]=rp[aa][14];
	  rd_im_ext[aa][2]=rp[aa][15];
	  r_re_ext[aa][5]=rp[aa][16];
	  r_im_ext[aa][5]=rp[aa][17];

	  r_re_ext[aa][6]=rp[aa][18];
	  r_im_ext[aa][6]=rp[aa][19];
	  rd_re_ext[aa][3]=rp[aa][20];
	  rd_im_ext[aa][3]=rp[aa][21];
	  r_re_ext[aa][7]=rp[aa][22];
	  r_im_ext[aa][7]=rp[aa][23];

	  r_re_ext[aa][8]=rp[aa][24];
	  r_im_ext[aa][8]=rp[aa][25];
	  rd_re_ext[aa][4]=rp[aa][26];
	  rd_im_ext[aa][4]=rp[aa][27];
	  r_re_ext[aa][9]=rp[aa][28];
	  r_im_ext[aa][9]=rp[aa][29];

	  r_re_ext[aa][10]=rp[aa][30];
	  r_im_ext[aa][10]=rp[aa][31];
	  rd_re_ext[aa][5]=rp[aa][32];
	  rd_im_ext[aa][5]=rp[aa][33];
	  r_re_ext[aa][11]=rp[aa][34];
	  r_im_ext[aa][11]=rp[aa][35];

	  r_re_ext[aa][12]=rp[aa][36];
	  r_im_ext[aa][12]=rp[aa][37];
	  rd_re_ext[aa][6]=rp[aa][38];
	  rd_im_ext[aa][6]=rp[aa][39];
	  r_re_ext[aa][13]=rp[aa][40];
	  r_im_ext[aa][13]=rp[aa][41];

	  r_re_ext[aa][14]=rp[aa][42];
	  r_im_ext[aa][14]=rp[aa][43];
	  rd_re_ext[aa][7]=rp[aa][44];
	  rd_im_ext[aa][7]=rp[aa][45];
	  r_re_ext[aa][15]=rp[aa][46];
	  r_im_ext[aa][15]=rp[aa][47];
		  
#ifdef DEBUG_NR_PUCCH_RX
	  for (int i=0;i<8;i++) printf("Ant %d PRB %d dmrs[%d] -> (%d,%d)\n",aa,prb+(i>>2),i,rd_re_ext[aa][i],rd_im_ext[aa]);
#endif
	} // aa
     } // prb


     // first compute DMRS component
     uint32_t x1, x2, s=0;
     x2 = (((1<<17)*((14*slot) + (pucch_pdu->start_symbol_index) + 1)*((2*pucch_pdu->dmrs_scrambling_id) + 1)) + (2*pucch_pdu->dmrs_scrambling_id))%(1U<<31); // c_init calculation according to TS38.211 subclause
#ifdef DEBUG_NR_PUCCH_RX
     printf("slot %d, start_symbol_index %d, dmrs_scrambling_id %d\n",
       slot,pucch_pdu->start_symbol_index,pucch_pdu->dmrs_scrambling_id);
#endif
     s = lte_gold_generic(&x1, &x2, 1);


     for (int group=0;group<ngroup;group++) {
     // each group has 8*nc_group_size elements, compute 1 complex correlation with DMRS per group
     // non-coherent combining across groups
       dmrs_re = byte2m64_re[((uint8_t*)&s)[(group&1)<<1]];
       dmrs_im = byte2m64_im[((uint8_t*)&s)[(group&1)<<1]];
#ifdef DEBUG_NR_PUCCH_RX
       printf("Group %d: s %x x2 %x ((%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
	      group,
	      ((uint16_t*)&s)[0],x2,
	      ((int16_t*)&dmrs_re)[0],((int16_t*)&dmrs_im)[0],    
	      ((int16_t*)&dmrs_re)[1],((int16_t*)&dmrs_im)[1],    
	      ((int16_t*)&dmrs_re)[2],((int16_t*)&dmrs_im)[2],    
	      ((int16_t*)&dmrs_re)[3],((int16_t*)&dmrs_im)[3]);   
#endif
       for (int aa=0;aa<Prx;aa++) {
#ifdef DEBUG_NR_PUCCH_RX
	 printf("Group %d: rd ((%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		group,
		rd_re_ext[aa][0],rd_im_ext[aa][0],
		rd_re_ext[aa][1],rd_im_ext[aa][1],
		rd_re_ext[aa][2],rd_im_ext[aa][2],
		rd_re_ext[aa][3],rd_im_ext[aa][3]);
#endif
	 corr32_re[group][aa]+=(rd_re_ext[aa][0]*((int16_t*)&dmrs_re)[0] + rd_im_ext[aa][0]*((int16_t*)&dmrs_im)[0]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][0]*((int16_t*)&dmrs_im)[0] + rd_im_ext[aa][0]*((int16_t*)&dmrs_re)[0]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][1]*((int16_t*)&dmrs_re)[1] + rd_im_ext[aa][1]*((int16_t*)&dmrs_im)[1]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][1]*((int16_t*)&dmrs_im)[1] + rd_im_ext[aa][1]*((int16_t*)&dmrs_re)[1]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][2]*((int16_t*)&dmrs_re)[2] + rd_im_ext[aa][2]*((int16_t*)&dmrs_im)[2]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][2]*((int16_t*)&dmrs_im)[2] + rd_im_ext[aa][2]*((int16_t*)&dmrs_re)[2]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][3]*((int16_t*)&dmrs_re)[3] + rd_im_ext[aa][3]*((int16_t*)&dmrs_im)[3]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][3]*((int16_t*)&dmrs_im)[3] + rd_im_ext[aa][3]*((int16_t*)&dmrs_re)[3]); 
       }
       dmrs_re = byte2m64_re[((uint8_t*)&s)[1+((group&1)<<1)]];
       dmrs_im = byte2m64_im[((uint8_t*)&s)[1+((group&1)<<1)]];
#ifdef DEBUG_NR_PUCCH_RX
       printf("Group %d: s %x ((%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
	      group,
	      ((uint16_t*)&s)[1],
	      ((int16_t*)&dmrs_re)[0],((int16_t*)&dmrs_im)[0],    
	      ((int16_t*)&dmrs_re)[1],((int16_t*)&dmrs_im)[1],    
	      ((int16_t*)&dmrs_re)[2],((int16_t*)&dmrs_im)[2],    
	      ((int16_t*)&dmrs_re)[3],((int16_t*)&dmrs_im)[3]);
#endif
       for (int aa=0;aa<Prx;aa++) {
#ifdef DEBUG_NR_PUCCH_RX
	 printf("Group %d: rd ((%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		group,
		rd_re_ext[aa][4],rd_im_ext[aa][4],
		rd_re_ext[aa][5],rd_im_ext[aa][5],
		rd_re_ext[aa][6],rd_im_ext[aa][6],
		rd_re_ext[aa][7],rd_im_ext[aa][7]);
#endif
	 corr32_re[group][aa]+=(rd_re_ext[aa][4]*((int16_t*)&dmrs_re)[0] + rd_im_ext[aa][4]*((int16_t*)&dmrs_im)[0]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][4]*((int16_t*)&dmrs_im)[0] + rd_im_ext[aa][4]*((int16_t*)&dmrs_re)[0]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][5]*((int16_t*)&dmrs_re)[1] + rd_im_ext[aa][5]*((int16_t*)&dmrs_im)[1]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][5]*((int16_t*)&dmrs_im)[1] + rd_im_ext[aa][5]*((int16_t*)&dmrs_re)[1]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][6]*((int16_t*)&dmrs_re)[2] + rd_im_ext[aa][6]*((int16_t*)&dmrs_im)[2]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][6]*((int16_t*)&dmrs_im)[2] + rd_im_ext[aa][6]*((int16_t*)&dmrs_re)[2]); 
	 corr32_re[group][aa]+=(rd_re_ext[aa][7]*((int16_t*)&dmrs_re)[3] + rd_im_ext[aa][7]*((int16_t*)&dmrs_im)[3]); 
	 corr32_im[group][aa]+=(-rd_re_ext[aa][7]*((int16_t*)&dmrs_im)[3] + rd_im_ext[aa][7]*((int16_t*)&dmrs_re)[3]); 
	 corr32_re[group][aa]>>=5;
	 corr32_im[group][aa]>>=5;
#ifdef DEBUG_NR_PUCCH_RX
	 printf("Group %d: corr32 (%d,%d)\n",group,corr32_re[group][aa],corr32_im[group][aa]);
#endif
       } //aa    
       
       if ((group&3) == 3) s = lte_gold_generic(&x1, &x2, 0);
     } // group
  }
  else { // 2 symbol case
     AssertFatal(1==0, "Fill in 2 symbol PUCCH2 case\n");
  }

  uint32_t x1, x2, s=0;  
  // unscrambling
  x2 = ((pucch_pdu->rnti)<<15)+pucch_pdu->data_scrambling_id;
  s = lte_gold_generic(&x1, &x2, 1);
#ifdef DEBUG_NR_PUCCH_RX
  printf("x2 %x, s %x\n",x2,s);
#endif
  __m64 c_re0,c_im0,c_re1,c_im1,c_re2,c_im2,c_re3,c_im3;
  re_offset=0;
  for (int prb=0;prb<pucch_pdu->prb_size;prb+=2,re_offset+=16) {
    c_re0 = byte2m64_re[((uint8_t*)&s)[0]];
    c_im0 = byte2m64_im[((uint8_t*)&s)[0]];
    c_re1 = byte2m64_re[((uint8_t*)&s)[1]];
    c_im1 = byte2m64_im[((uint8_t*)&s)[1]];
    c_re2 = byte2m64_re[((uint8_t*)&s)[2]];
    c_im2 = byte2m64_im[((uint8_t*)&s)[2]];
    c_re3 = byte2m64_re[((uint8_t*)&s)[3]];
    c_im3 = byte2m64_im[((uint8_t*)&s)[3]];

    for (int aa=0;aa<Prx;aa++) {
#ifdef DEBUG_NR_PUCCH_RX
      printf("prb %d: rd ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		prb,
		r_re_ext[aa][re_offset],r_im_ext[aa][re_offset],
		r_re_ext[aa][re_offset+1],r_im_ext[aa][re_offset+1],
		r_re_ext[aa][re_offset+2],r_im_ext[aa][re_offset+2],
		r_re_ext[aa][re_offset+3],r_im_ext[aa][re_offset+3],
		r_re_ext[aa][re_offset+4],r_im_ext[aa][re_offset+4],
		r_re_ext[aa][re_offset+5],r_im_ext[aa][re_offset+5],
		r_re_ext[aa][re_offset+6],r_im_ext[aa][re_offset+6],
		r_re_ext[aa][re_offset+7],r_im_ext[aa][re_offset+7]);
      printf("prb %d: c ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		prb,
		((int16_t*)&c_re0)[0],((int16_t*)&c_im0)[0],
		((int16_t*)&c_re0)[1],((int16_t*)&c_im0)[1],
		((int16_t*)&c_re0)[2],((int16_t*)&c_im0)[2],
		((int16_t*)&c_re0)[3],((int16_t*)&c_im0)[3],
		((int16_t*)&c_re1)[0],((int16_t*)&c_im1)[0],
		((int16_t*)&c_re1)[1],((int16_t*)&c_im1)[1],
		((int16_t*)&c_re1)[2],((int16_t*)&c_im1)[2],
		((int16_t*)&c_re1)[3],((int16_t*)&c_im1)[3]
		);
      printf("prb %d: rd ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		prb+1,
		r_re_ext[aa][re_offset+8],r_im_ext[aa][re_offset+8],
		r_re_ext[aa][re_offset+9],r_im_ext[aa][re_offset+9],
		r_re_ext[aa][re_offset+10],r_im_ext[aa][re_offset+10],
		r_re_ext[aa][re_offset+11],r_im_ext[aa][re_offset+11],
		r_re_ext[aa][re_offset+12],r_im_ext[aa][re_offset+12],
		r_re_ext[aa][re_offset+13],r_im_ext[aa][re_offset+13],
		r_re_ext[aa][re_offset+14],r_im_ext[aa][re_offset+14],
		r_re_ext[aa][re_offset+15],r_im_ext[aa][re_offset+15]);
      printf("prb %d: c ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
		prb+1,
		((int16_t*)&c_re2)[0],((int16_t*)&c_im2)[0],
		((int16_t*)&c_re2)[1],((int16_t*)&c_im2)[1],
		((int16_t*)&c_re2)[2],((int16_t*)&c_im2)[2],
		((int16_t*)&c_re2)[3],((int16_t*)&c_im2)[3],
		((int16_t*)&c_re3)[0],((int16_t*)&c_im3)[0],
		((int16_t*)&c_re3)[1],((int16_t*)&c_im3)[1],
		((int16_t*)&c_re3)[2],((int16_t*)&c_im3)[2],
		((int16_t*)&c_re3)[3],((int16_t*)&c_im3)[3]
		);
#endif

      ((__m64*)&r_re_ext2[aa][re_offset])[0] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[0],c_im0);
      ((__m64*)&r_re_ext[aa][re_offset])[0] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[0],c_re0);
      ((__m64*)&r_im_ext2[aa][re_offset])[0] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[0],c_re0);
      ((__m64*)&r_im_ext[aa][re_offset])[0] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[0],c_im0);

      ((__m64*)&r_re_ext2[aa][re_offset])[1] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[1],c_im1);
      ((__m64*)&r_re_ext[aa][re_offset])[1] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[1],c_re1);
      ((__m64*)&r_im_ext2[aa][re_offset])[1] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[1],c_re1);
      ((__m64*)&r_im_ext[aa][re_offset])[1] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[1],c_im1);

      ((__m64*)&r_re_ext2[aa][re_offset])[2] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[2],c_im2);
      ((__m64*)&r_re_ext[aa][re_offset])[2] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[2],c_re2);
      ((__m64*)&r_im_ext2[aa][re_offset])[2] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[2],c_re2);
      ((__m64*)&r_im_ext[aa][re_offset])[2] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[2],c_im2);

      ((__m64*)&r_re_ext2[aa][re_offset])[3] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[3],c_im3);
      ((__m64*)&r_re_ext[aa][re_offset])[3] = _mm_mullo_pi16(((__m64*)&r_re_ext[aa][re_offset])[3],c_re3);
      ((__m64*)&r_im_ext2[aa][re_offset])[3] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[3],c_re3);
      ((__m64*)&r_im_ext[aa][re_offset])[3] = _mm_mullo_pi16(((__m64*)&r_im_ext[aa][re_offset])[3],c_im3);

#ifdef DEBUG_NR_PUCCH_RX
      printf("prb %d: r ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
	     prb,
	     r_re_ext[aa][re_offset],r_im_ext[aa][re_offset],
	     r_re_ext[aa][re_offset+1],r_im_ext[aa][re_offset+1],
	     r_re_ext[aa][re_offset+2],r_im_ext[aa][re_offset+2],
	     r_re_ext[aa][re_offset+3],r_im_ext[aa][re_offset+3],
	     r_re_ext[aa][re_offset+4],r_im_ext[aa][re_offset+4],
	     r_re_ext[aa][re_offset+5],r_im_ext[aa][re_offset+5],
	     r_re_ext[aa][re_offset+6],r_im_ext[aa][re_offset+6],
	     r_re_ext[aa][re_offset+7],r_im_ext[aa][re_offset+7]);
      printf("prb %d: r ((%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d))\n",
	     prb+1,
	     r_re_ext[aa][re_offset+8],r_im_ext[aa][re_offset+8],
	     r_re_ext[aa][re_offset+9],r_im_ext[aa][re_offset+9],
	     r_re_ext[aa][re_offset+10],r_im_ext[aa][re_offset+10],
	     r_re_ext[aa][re_offset+11],r_im_ext[aa][re_offset+11],
	     r_re_ext[aa][re_offset+12],r_im_ext[aa][re_offset+12],
	     r_re_ext[aa][re_offset+13],r_im_ext[aa][re_offset+13],
	     r_re_ext[aa][re_offset+14],r_im_ext[aa][re_offset+14],
	     r_re_ext[aa][re_offset+15],r_im_ext[aa][re_offset+15]);
#endif      
    }
    s = lte_gold_generic(&x1, &x2, 0);
  }
  AssertFatal(pucch_pdu->bit_len_csi_part1 + pucch_pdu->bit_len_csi_part2 == 0,"no csi for now\n");
  AssertFatal((pucch_pdu->bit_len_harq+pucch_pdu->sr_flag > 2 ) && (pucch_pdu->bit_len_harq+pucch_pdu->sr_flag < 12),"illegal length (%d,%d)\n",pucch_pdu->bit_len_harq,pucch_pdu->sr_flag);
  int nb_bit = pucch_pdu->bit_len_harq+pucch_pdu->sr_flag;
  __m256i *rp_re[Prx2];
  __m256i *rp2_re[Prx2];
  __m256i *rp_im[Prx2];
  __m256i *rp2_im[Prx2];
  for (int aa=0;aa<Prx;aa++) {
    rp_re[aa] = (__m256i*)r_re_ext[aa];
    rp_im[aa] = (__m256i*)r_im_ext[aa];
    rp2_re[aa] = (__m256i*)r_re_ext2[aa];
    rp2_im[aa] = (__m256i*)r_im_ext2[aa];
  }
  __m256i prod_re[Prx2],prod_im[Prx2];
  int64_t corr=0;
  int cw_ML=0;

  for (int cw=0;cw<1<<nb_bit;cw++) {
#ifdef DEBUG_NR_PUCCH_RX
    printf("cw %d:",cw);
    for (int i=0;i<32;i+=2) {
      printf("%d,%d,",
	     ((int16_t*)&pucch2_lut[nb_bit-3][cw<<1])[i>>1],
	     ((int16_t*)&pucch2_lut[nb_bit-3][cw<<1])[1+(i>>1)]);
    }
    printf("\n");
#endif
    // do complex correlation
    for (int aa=0;aa<Prx;aa++) {
      prod_re[aa] = _mm256_srai_epi16(_mm256_adds_epi16(_mm256_mullo_epi16(pucch2_lut[nb_bit-3][cw<<1],rp_re[aa][0]),
							_mm256_mullo_epi16(pucch2_lut[nb_bit-3][(cw<<1)+1],rp_im[aa][0])),5);
      prod_im[aa] = _mm256_srai_epi16(_mm256_subs_epi16(_mm256_mullo_epi16(pucch2_lut[nb_bit-3][cw<<1],rp2_im[aa][0]),
							_mm256_mullo_epi16(pucch2_lut[nb_bit-3][(cw<<1)+1],rp2_re[aa][0])),5);
      prod_re[aa] = _mm256_hadds_epi16(prod_re[aa],prod_re[aa]);// 0+1
      prod_im[aa] = _mm256_hadds_epi16(prod_im[aa],prod_im[aa]);
      prod_re[aa] = _mm256_hadds_epi16(prod_re[aa],prod_re[aa]);// 0+1+2+3
      prod_im[aa] = _mm256_hadds_epi16(prod_im[aa],prod_im[aa]);
      prod_re[aa] = _mm256_hadds_epi16(prod_re[aa],prod_re[aa]);// 0+1+2+3+4+5+6+7
      prod_im[aa] = _mm256_hadds_epi16(prod_im[aa],prod_im[aa]);
      prod_re[aa] = _mm256_hadds_epi16(prod_re[aa],prod_re[aa]);// 0+1+2+3+4+5+6+7+8+9+10+11+12+13+14+15
      prod_im[aa] = _mm256_hadds_epi16(prod_im[aa],prod_im[aa]);
    }
    int64_t corr_re=0,corr_im=0;

    for (int aa=0;aa<Prx;aa++) {
      LOG_D(PHY,"pucch2 cw %d aa %d: (%d,%d)+(%d,%d) = (%d,%d)\n",cw,aa,
	    corr32_re[0][aa],corr32_im[0][aa],
	    ((int16_t*)(&prod_re[aa]))[0],
	    ((int16_t*)(&prod_im[aa]))[0],
	    corr32_re[0][aa]+((int16_t*)(&prod_re[0]))[0],
	    corr32_im[0][aa]+((int16_t*)(&prod_im[0]))[0]);
	     
      corr_re += ( corr32_re[0][aa]+((int16_t*)(&prod_re[0]))[0]);
      corr_im += ( corr32_im[0][aa]+((int16_t*)(&prod_im[0]))[0]);

    }
    int64_t corr_tmp = corr_re*corr_re + corr_im*corr_im;
    if (corr_tmp > corr) {
      corr = corr_tmp;
      cw_ML=cw;
    }
  }
  uint8_t corr_dB = dB_fixed64((uint64_t)corr);
  LOG_D(PHY,"cw_ML %d, metric %d dB\n",cw_ML,corr_dB);
  uci_pdu->harq.harq_bit_len = pucch_pdu->bit_len_harq;

  
  int harq_bytes=pucch_pdu->bit_len_harq>>3;
  if ((pucch_pdu->bit_len_harq&7) > 0) harq_bytes++;
  uci_pdu->harq.harq_payload = (uint8_t*)malloc(harq_bytes);
  uci_pdu->harq.harq_crc = 2;
  for (int i=0;i<harq_bytes;i++) {
    uci_pdu->harq.harq_payload[i] = cw_ML & 255;
    cw_ML>>=8;
  }
  
  if (pucch_pdu->sr_flag == 1) {
    uci_pdu->sr.sr_bit_len = 1;
    uci_pdu->sr.sr_payload = malloc(1);
    uci_pdu->sr.sr_payload[0] = cw_ML;
  }
}
    
