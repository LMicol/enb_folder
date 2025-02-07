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

#include "executables/nr-softmodem-common.h"
#include "PHY/defs_gNB.h"
#include "PHY/phy_extern.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "PHY/INIT/phy_init.h"
#include "PHY/CODING/nrPolar_tools/nr_polar_pbch_defs.h"
#include "PHY/NR_TRANSPORT/nr_transport.h"
/*#include "RadioResourceConfigCommonSIB.h"
#include "RadioResourceConfigDedicated.h"
#include "TDD-Config.h"
#include "MBSFN-SubframeConfigList.h"*/
#include "openair1/PHY/defs_RU.h"
#include "openair1/PHY/CODING/nrLDPC_extern.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "assertions.h"
#include <math.h>

#include "PHY/NR_TRANSPORT/nr_ulsch.h"
#include "PHY/NR_REFSIG/nr_refsig.h"
#include "SCHED_NR/fapi_nr_l1.h"
#include "openair2/LAYER2/NR_MAC_gNB/mac_proto.h"
#include "nfapi_nr_interface.h"

/*
extern uint32_t from_nrarfcn(int nr_bandP,uint32_t dl_nrarfcn);

extern int32_t get_nr_uldl_offset(int nr_bandP);
extern openair0_config_t openair0_cfg[MAX_CARDS];
*/

int l1_north_init_gNB() {

  if (RC.nb_nr_L1_inst > 0 &&  RC.gNB != NULL) {

    AssertFatal(RC.nb_nr_L1_inst>0,"nb_nr_L1_inst=%d\n",RC.nb_nr_L1_inst);
    AssertFatal(RC.gNB!=NULL,"RC.gNB is null\n");
    LOG_I(PHY,"%s() RC.nb_nr_L1_inst:%d\n", __FUNCTION__, RC.nb_nr_L1_inst);

    for (int i=0; i<RC.nb_nr_L1_inst; i++) {
      AssertFatal(RC.gNB[i]!=NULL,"RC.gNB[%d] is null\n",i);

      if ((RC.gNB[i]->if_inst =  NR_IF_Module_init(i))<0) return(-1);
      
      LOG_I(PHY,"%s() RC.gNB[%d] installing callbacks\n", __FUNCTION__, i);
      RC.gNB[i]->if_inst->NR_PHY_config_req = nr_phy_config_request;
      RC.gNB[i]->if_inst->NR_Schedule_response = nr_schedule_response;
    }
  } else {
    LOG_I(PHY,"%s() Not installing PHY callbacks - RC.nb_nr_L1_inst:%d RC.gNB:%p\n", __FUNCTION__, RC.nb_nr_L1_inst, RC.gNB);
  }

  return(0);
}


int phy_init_nr_gNB(PHY_VARS_gNB *gNB,
                    unsigned char is_secondary_gNB,
                    unsigned char abstraction_flag) {
  // shortcuts
  NR_DL_FRAME_PARMS *const fp       = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  NR_gNB_COMMON *const common_vars  = &gNB->common_vars;
  NR_gNB_PRACH *const prach_vars   = &gNB->prach_vars;
  NR_gNB_PUSCH **const pusch_vars   = gNB->pusch_vars;
  /*LTE_eNB_SRS *const srs_vars       = gNB->srs_vars;
  LTE_eNB_PRACH *const prach_vars   = &gNB->prach_vars;*/

  int i;
  int Ptx=cfg->carrier_config.num_tx_ant.value;
  int Prx=cfg->carrier_config.num_rx_ant.value;

  AssertFatal(Ptx>0 && Ptx<9,"Ptx %d is not supported\n",Ptx);
  AssertFatal(Prx>0 && Prx<9,"Prx %d is not supported\n",Prx);
  LOG_I(PHY,"[gNB %d] %s() About to wait for gNB to be configured\n", gNB->Mod_id, __FUNCTION__);

  while(gNB->configured == 0) usleep(10000);

  load_dftslib();
  /*
    LOG_I(PHY,"[gNB %"PRIu8"] Initializing DL_FRAME_PARMS : N_RB_DL %"PRIu8", PHICH Resource %d, PHICH Duration %d nb_antennas_tx:%u nb_antennas_rx:%u PRACH[rootSequenceIndex:%u prach_Config_enabled:%u configIndex:%u highSpeed:%u zeroCorrelationZoneConfig:%u freqOffset:%u]\n",
          gNB->Mod_id,
          fp->N_RB_DL,fp->phich_config_common.phich_resource,
          fp->phich_config_common.phich_duration,
          fp->nb_antennas_tx, fp->nb_antennas_rx,
          fp->prach_config_common.rootSequenceIndex,
          fp->prach_config_common.prach_Config_enabled,
          fp->prach_config_common.prach_ConfigInfo.prach_ConfigIndex,
          fp->prach_config_common.prach_ConfigInfo.highSpeedFlag,
          fp->prach_config_common.prach_ConfigInfo.zeroCorrelationZoneConfig,
          fp->prach_config_common.prach_ConfigInfo.prach_FreqOffset
          );*/
  LOG_D(PHY,"[MSC_NEW][FRAME 00000][PHY_gNB][MOD %02"PRIu8"][]\n", gNB->Mod_id);
  crcTableInit();
  init_scrambling_luts();
  init_pucch2_luts();
  load_nrLDPClib();
  // PBCH DMRS gold sequences generation
  nr_init_pbch_dmrs(gNB);
  //PDCCH DMRS init
  gNB->nr_gold_pdcch_dmrs = (uint32_t ***)malloc16(fp->slots_per_frame*sizeof(uint32_t **));
  uint32_t ***pdcch_dmrs             = gNB->nr_gold_pdcch_dmrs;
  AssertFatal(pdcch_dmrs!=NULL, "NR init: pdcch_dmrs malloc failed\n");

  for (int slot=0; slot<fp->slots_per_frame; slot++) {
    pdcch_dmrs[slot] = (uint32_t **)malloc16(fp->symbols_per_slot*sizeof(uint32_t *));
    AssertFatal(pdcch_dmrs[slot]!=NULL, "NR init: pdcch_dmrs for slot %d - malloc failed\n", slot);

    for (int symb=0; symb<fp->symbols_per_slot; symb++) {
      pdcch_dmrs[slot][symb] = (uint32_t *)malloc16(NR_MAX_PDCCH_DMRS_INIT_LENGTH_DWORD*sizeof(uint32_t));
      AssertFatal(pdcch_dmrs[slot][symb]!=NULL, "NR init: pdcch_dmrs for slot %d symbol %d - malloc failed\n", slot, symb);
    }
  }

  nr_init_pdcch_dmrs(gNB, cfg->cell_config.phy_cell_id.value);
  nr_init_pbch_interleaver(gNB->nr_pbch_interleaver);
  //PDSCH DMRS init
  gNB->nr_gold_pdsch_dmrs = (uint32_t ****)malloc16(fp->slots_per_frame*sizeof(uint32_t ***));
  uint32_t ****pdsch_dmrs             = gNB->nr_gold_pdsch_dmrs;

  for (int slot=0; slot<fp->slots_per_frame; slot++) {
    pdsch_dmrs[slot] = (uint32_t ***)malloc16(fp->symbols_per_slot*sizeof(uint32_t **));
    AssertFatal(pdsch_dmrs[slot]!=NULL, "NR init: pdsch_dmrs for slot %d - malloc failed\n", slot);

    for (int symb=0; symb<fp->symbols_per_slot; symb++) {
      pdsch_dmrs[slot][symb] = (uint32_t **)malloc16(NR_MAX_NB_CODEWORDS*sizeof(uint32_t *));
      AssertFatal(pdsch_dmrs[slot][symb]!=NULL, "NR init: pdsch_dmrs for slot %d symbol %d - malloc failed\n", slot, symb);

      for (int q=0; q<NR_MAX_NB_CODEWORDS; q++) {
        pdsch_dmrs[slot][symb][q] = (uint32_t *)malloc16(NR_MAX_PDSCH_DMRS_INIT_LENGTH_DWORD*sizeof(uint32_t));
        AssertFatal(pdsch_dmrs[slot][symb][q]!=NULL, "NR init: pdsch_dmrs for slot %d symbol %d codeword %d - malloc failed\n", slot, symb, q);
      }
    }
  }


  nr_init_pdsch_dmrs(gNB, cfg->cell_config.phy_cell_id.value);

  //PUSCH DMRS init
  gNB->nr_gold_pusch_dmrs = (uint32_t ****)malloc16(2*sizeof(uint32_t ***));
  uint32_t ****pusch_dmrs = gNB->nr_gold_pusch_dmrs;

  for(int nscid=0; nscid<2; nscid++) {
    pusch_dmrs[nscid] = (uint32_t ***)malloc16(fp->slots_per_frame*sizeof(uint32_t **));
    AssertFatal(pusch_dmrs[nscid]!=NULL, "NR init: pusch_dmrs for nscid %d - malloc failed\n", nscid);

    for (int slot=0; slot<fp->slots_per_frame; slot++) {
      pusch_dmrs[nscid][slot] = (uint32_t **)malloc16(fp->symbols_per_slot*sizeof(uint32_t *));
      AssertFatal(pusch_dmrs[nscid][slot]!=NULL, "NR init: pusch_dmrs for slot %d - malloc failed\n", slot);

      for (int symb=0; symb<fp->symbols_per_slot; symb++) {
        pusch_dmrs[nscid][slot][symb] = (uint32_t *)malloc16(NR_MAX_PUSCH_DMRS_INIT_LENGTH_DWORD*sizeof(uint32_t));
        AssertFatal(pusch_dmrs[nscid][slot][symb]!=NULL, "NR init: pusch_dmrs for slot %d symbol %d - malloc failed\n", slot, symb);
      }
    }
  }

  uint32_t Nid_pusch[2] = {cfg->cell_config.phy_cell_id.value,cfg->cell_config.phy_cell_id.value};
  nr_gold_pusch(gNB, &Nid_pusch[0]);

  /// Transport init necessary for NR synchro
  init_nr_transport(gNB);

  gNB->first_run_I0_measurements = 1;

  common_vars->rxdata  = (int32_t **)malloc16(Prx*sizeof(int32_t*));
  common_vars->txdataF = (int32_t **)malloc16(Ptx*sizeof(int32_t*));
  common_vars->rxdataF = (int32_t **)malloc16(Prx*sizeof(int32_t*));

  for (i=0;i<Ptx;i++){
      common_vars->txdataF[i] = (int32_t*)malloc16_clear(fp->samples_per_frame_wCP*sizeof(int32_t)); // [hna] samples_per_frame without CP
      LOG_D(PHY,"[INIT] common_vars->txdataF[%d] = %p (%lu bytes)\n",
	    i,common_vars->txdataF[i],
	    fp->samples_per_frame_wCP*sizeof(int32_t));
      
  }
  for (i=0;i<Prx;i++){
    common_vars->rxdataF[i] = (int32_t*)malloc16_clear(fp->samples_per_frame_wCP*sizeof(int32_t));
    common_vars->rxdata[i] = (int32_t*)malloc16_clear(fp->samples_per_frame*sizeof(int32_t));
  }


  // Channel estimates for SRS
/*
  for (UE_id=0; UE_id<NUMBER_OF_UE_MAX; UE_id++) {
    srs_vars[UE_id].srs_ch_estimates      = (int32_t **)malloc16( 64*sizeof(int32_t *) );
    srs_vars[UE_id].srs_ch_estimates_time = (int32_t **)malloc16( 64*sizeof(int32_t *) );

    for (i=0; i<64; i++) {
      srs_vars[UE_id].srs_ch_estimates[i]      = (int32_t *)malloc16_clear( sizeof(int32_t)*fp->ofdm_symbol_size );
      srs_vars[UE_id].srs_ch_estimates_time[i] = (int32_t *)malloc16_clear( sizeof(int32_t)*fp->ofdm_symbol_size*2 );
    }
  } //UE_id
*/
  /*generate_ul_ref_sigs_rx();

  init_ulsch_power_LUT();*/

/*
  // SRS
  for (UE_id=0; UE_id<NUMBER_OF_UE_MAX; UE_id++) {
    srs_vars[UE_id].srs = (int32_t *)malloc16_clear(2*fp->ofdm_symbol_size*sizeof(int32_t));
  }
*/
  // PRACH
  prach_vars->prachF = (int16_t *)malloc16_clear( 1024*2*sizeof(int16_t) );
  prach_vars->rxsigF = (int16_t **)malloc16_clear(Prx*sizeof(int16_t*));
  /* 
  for (i=0;i<Prx;i++){
    prach_vars->rxsigF[i] = (int16_t *)malloc16_clear( 1024*2*sizeof(int16_t) );
  }
  */
  prach_vars->prach_ifft       = (int32_t *)malloc16_clear(1024*2*sizeof(int32_t));

  int N_RB_UL = cfg->carrier_config.ul_grid_size[cfg->ssb_config.scs_common.value].value;

  printf("Before ULSCH init : %p\n",gNB->dlsch[0][0]->harq_processes[0]);
  for (int ULSCH_id=0; ULSCH_id<NUMBER_OF_NR_ULSCH_MAX; ULSCH_id++) {
    printf("ULSCH_id %d : %p\n",ULSCH_id,gNB->dlsch[0][0]->harq_processes[0]);
    pusch_vars[ULSCH_id] = (NR_gNB_PUSCH *)malloc16_clear( sizeof(NR_gNB_PUSCH) );
    pusch_vars[ULSCH_id]->rxdataF_ext           = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->rxdataF_ext2          = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_estimates       = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_estimates_ext   = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates     = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates_ext = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_estimates_time  = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->rxdataF_comp          = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_mag0             = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_magb0            = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_mag             = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->ul_ch_magb            = (int32_t **)malloc16(Prx*sizeof(int32_t *) );
    pusch_vars[ULSCH_id]->rho                   = (int32_t **)malloc16_clear(Prx*sizeof(int32_t*) );

    printf("ULSCH_id %d (before rx antenna alloc) : %p\n",ULSCH_id,gNB->dlsch[0][0]->harq_processes[0]);
    for (i=0; i<Prx; i++) {
      pusch_vars[ULSCH_id]->rxdataF_ext[i]           = (int32_t *)malloc16_clear( sizeof(int32_t)*N_RB_UL*12*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->rxdataF_ext2[i]          = (int32_t *)malloc16_clear( sizeof(int32_t)*N_RB_UL*12*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->ul_ch_estimates[i]       = (int32_t *)malloc16_clear( sizeof(int32_t)*N_RB_UL*12*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->ul_ch_estimates_ext[i]   = (int32_t *)malloc16_clear( sizeof(int32_t)*N_RB_UL*12*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->ul_ch_estimates_time[i]  = (int32_t *)malloc16_clear( 2*sizeof(int32_t)*fp->ofdm_symbol_size );
      pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates[i]       = (int32_t *)malloc16_clear( sizeof(int32_t)*fp->ofdm_symbol_size*2*fp->symbols_per_slot ); // max intensity in freq is 1 sc every 2 RBs
      pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates_ext[i]   = (int32_t *)malloc16_clear( sizeof(int32_t)*fp->ofdm_symbol_size*2*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->rxdataF_comp[i]          = (int32_t *)malloc16_clear( sizeof(int32_t)*N_RB_UL*12*fp->symbols_per_slot );
      pusch_vars[ULSCH_id]->ul_ch_mag0[i]             = (int32_t *)malloc16_clear( fp->symbols_per_slot*sizeof(int32_t)*N_RB_UL*12 );
      pusch_vars[ULSCH_id]->ul_ch_magb0[i]            = (int32_t *)malloc16_clear( fp->symbols_per_slot*sizeof(int32_t)*N_RB_UL*12 );
      pusch_vars[ULSCH_id]->ul_ch_mag[i]             = (int32_t *)malloc16_clear( fp->symbols_per_slot*sizeof(int32_t)*N_RB_UL*12 );
      pusch_vars[ULSCH_id]->ul_ch_magb[i]            = (int32_t *)malloc16_clear( fp->symbols_per_slot*sizeof(int32_t)*N_RB_UL*12 );
      pusch_vars[ULSCH_id]->rho[i]                   = (int32_t *)malloc16_clear( sizeof(int32_t)*(fp->N_RB_UL*12*7*2) );
    }
    printf("ULSCH_id %d (before llr alloc) : %p\n",ULSCH_id,gNB->dlsch[0][0]->harq_processes[0]);
    pusch_vars[ULSCH_id]->llr = (int16_t *)malloc16_clear( (8*((3*8*6144)+12))*sizeof(int16_t) ); // [hna] 6144 is LTE and (8*((3*8*6144)+12)) is not clear
    printf("ULSCH_id %d (after llr alloc) : %p\n",ULSCH_id,gNB->dlsch[0][0]->harq_processes[0]); 
  } //ulsch_id
/*
  for (ulsch_id=0; ulsch_id<NUMBER_OF_UE_MAX; ulsch_id++)
    gNB->UE_stats_ptr[ulsch_id] = &gNB->UE_stats[ulsch_id];
*/
  printf("After ULSCH init : %p\n",gNB->dlsch[0][0]->harq_processes[0]);
  return (0);
}

void phy_free_nr_gNB(PHY_VARS_gNB *gNB)
{
  //NR_DL_FRAME_PARMS* const fp       = &gNB->frame_parms;
  NR_gNB_COMMON *const common_vars  = &gNB->common_vars;
  NR_gNB_PUSCH **const pusch_vars   = gNB->pusch_vars;
  /*LTE_eNB_SRS *const srs_vars        = gNB->srs_vars;
  LTE_eNB_PRACH *const prach_vars    = &gNB->prach_vars;*/
  uint32_t ***pdcch_dmrs             = gNB->nr_gold_pdcch_dmrs;
  int Ptx=gNB->gNB_config.carrier_config.num_tx_ant.value;

  for (int i = 0; i < Ptx; i++) {
    free_and_zero(common_vars->txdataF[i]);
    /* rxdataF[i] is not allocated -> don't free */
  }

  free_and_zero(common_vars->txdataF);
  free_and_zero(common_vars->rxdataF);
  // PDCCH DMRS sequences
  free_and_zero(pdcch_dmrs);
/*
  // Channel estimates for SRS
  for (UE_id = 0; UE_id < NUMBER_OF_UE_MAX; UE_id++) {
    for (i=0; i<64; i++) {
      free_and_zero(srs_vars[UE_id].srs_ch_estimates[i]);
      free_and_zero(srs_vars[UE_id].srs_ch_estimates_time[i]);
    }

    free_and_zero(srs_vars[UE_id].srs_ch_estimates);
    free_and_zero(srs_vars[UE_id].srs_ch_estimates_time);
  } //UE_id

  //free_ul_ref_sigs();

  for (UE_id=0; UE_id<NUMBER_OF_UE_MAX; UE_id++) free_and_zero(srs_vars[UE_id].srs);

  free_and_zero(prach_vars->prachF);

  for (i = 0; i < 64; i++) free_and_zero(prach_vars->prach_ifft[0][i]);

  free_and_zero(prach_vars->prach_ifft[0]);
  free_and_zero(prach_vars->rxsigF[0]);
*/
  for (int ULSCH_id=0; ULSCH_id<NUMBER_OF_NR_ULSCH_MAX; ULSCH_id++) {
    for (int i = 0; i < 2; i++) {
      free_and_zero(pusch_vars[ULSCH_id]->rxdataF_ext[i]);
      free_and_zero(pusch_vars[ULSCH_id]->rxdataF_ext2[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates_ext[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates_time[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates_ext[i]);
      free_and_zero(pusch_vars[ULSCH_id]->rxdataF_comp[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_mag0[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_magb0[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_mag[i]);
      free_and_zero(pusch_vars[ULSCH_id]->ul_ch_magb[i]);
      free_and_zero(pusch_vars[ULSCH_id]->rho[i]);
    }

    free_and_zero(pusch_vars[ULSCH_id]->rxdataF_ext);
    free_and_zero(pusch_vars[ULSCH_id]->rxdataF_ext2);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates_ext);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_ptrs_estimates_ext);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_estimates_time);
    free_and_zero(pusch_vars[ULSCH_id]->rxdataF_comp);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_mag0);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_magb0);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_mag);
    free_and_zero(pusch_vars[ULSCH_id]->ul_ch_magb);
    free_and_zero(pusch_vars[ULSCH_id]->rho);

    free_and_zero(pusch_vars[ULSCH_id]->llr);
    free_and_zero(pusch_vars[ULSCH_id]);
  } //ULSCH_id
/*
  for (UE_id = 0; UE_id < NUMBER_OF_UE_MAX; UE_id++) gNB->UE_stats_ptr[UE_id] = NULL;
*/
}
/*
void install_schedule_handlers(IF_Module_t *if_inst)
{
  if_inst->PHY_config_req = phy_config_request;
  if_inst->schedule_response = schedule_response;
}*/

/// this function is a temporary addition for NR configuration


void nr_phy_config_request_sim(PHY_VARS_gNB *gNB,
                               int N_RB_DL,
                               int N_RB_UL,
                               int mu,
                               int Nid_cell,
                               uint64_t position_in_burst)
{
  NR_DL_FRAME_PARMS *fp                                   = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *gNB_config               = &gNB->gNB_config;
  //overwrite for new NR parameters

  uint64_t rev_burst=0;
  for (int i=0; i<64; i++)
    rev_burst |= (((position_in_burst>>(63-i))&0x01)<<i);

  gNB_config->cell_config.phy_cell_id.value             = Nid_cell;
  gNB_config->ssb_config.scs_common.value               = mu;
  gNB_config->ssb_table.ssb_subcarrier_offset.value     = 0;
  gNB_config->ssb_table.ssb_offset_point_a.value        = (N_RB_DL-20)>>1;
  gNB_config->ssb_table.ssb_mask_list[1].ssb_mask.value = (rev_burst)&(0xFFFFFFFF);
  gNB_config->ssb_table.ssb_mask_list[0].ssb_mask.value = (rev_burst>>32)&(0xFFFFFFFF);
  gNB_config->cell_config.frame_duplex_type.value       = TDD;
  gNB_config->ssb_table.ssb_period.value		= 1; //10ms
  gNB_config->carrier_config.dl_grid_size[mu].value     = N_RB_DL;
  gNB_config->carrier_config.ul_grid_size[mu].value     = N_RB_UL;
  gNB_config->carrier_config.num_tx_ant.value           = fp->nb_antennas_tx;
  gNB_config->carrier_config.num_rx_ant.value           = fp->nb_antennas_rx;

  gNB_config->tdd_table.tdd_period.value = 0;
  //gNB_config->subframe_config.dl_cyclic_prefix_type.value = (fp->Ncp == NORMAL) ? NFAPI_CP_NORMAL : NFAPI_CP_EXTENDED;

  gNB->mac_enabled   = 1;
  fp->dl_CarrierFreq = 3500000000;//from_nrarfcn(gNB_config->nfapi_config.rf_bands.rf_band[0],gNB_config->nfapi_config.nrarfcn.value);
  fp->ul_CarrierFreq = 3500000000;//fp->dl_CarrierFreq - (get_uldl_offset(gNB_config->nfapi_config.rf_bands.rf_band[0])*100000);
  fp->nr_band = 78;
  fp->threequarter_fs= 0;

  gNB_config->carrier_config.dl_bandwidth.value = config_bandwidth(mu, N_RB_DL, fp->nr_band);

  nr_init_frame_parms(gNB_config, fp);
  gNB->configured    = 1;
  LOG_I(PHY,"gNB configured\n");
}


void nr_phy_config_request(NR_PHY_Config_t *phy_config) {
  uint8_t Mod_id = phy_config->Mod_id;
  NR_DL_FRAME_PARMS *fp = &RC.gNB[Mod_id]->frame_parms;
  nfapi_nr_config_request_scf_t *gNB_config = &RC.gNB[Mod_id]->gNB_config;

  /*
  gNB_config->cell_config.phy_cell_id.value             = phy_config->cfg->cell_config.phy_cell_id.value;
  gNB_config->carrier_config.dl_frequency.value         = phy_config->cfg->carrier_config.dl_frequency.value;
  gNB_config->carrier_config.uplink_frequency.value     = phy_config->cfg->carrier_config.uplink_frequency.value;
  gNB_config->ssb_config.scs_common.value               = phy_config->cfg->ssb_config.scs_common.value;
  gNB_config->carrier_config.dl_bandwidth.value         = phy_config->cfg->carrier_config.dl_bandwidth.value;
  gNB_config->carrier_config.uplink_bandwidth.value     = phy_config->cfg->carrier_config.uplink_bandwidth.value;
  gNB_config->ssb_table.ssb_subcarrier_offset.value     = phy_config->cfg->ssb_table.ssb_subcarrier_offset.value;
  gNB_config->ssb_table.ssb_offset_point_a.value        = phy_config->cfg->ssb_table.ssb_offset_point_a.value;
  gNB_config->ssb_table.ssb_mask_list[0].ssb_mask.value = phy_config->cfg->ssb_table.ssb_mask_list[0].ssb_mask.value;
  gNB_config->ssb_table.ssb_mask_list[1].ssb_mask.value = phy_config->cfg->ssb_table.ssb_mask_list[1].ssb_mask.value;
  gNB_config->ssb_table.ssb_period.value		= phy_config->cfg->ssb_table.ssb_period.value;
  for (int i=0; i<5; i++) {
    gNB_config->carrier_config.dl_grid_size[i].value    = phy_config->cfg->carrier_config.dl_grid_size[i].value;
    gNB_config->carrier_config.ul_grid_size[i].value    = phy_config->cfg->carrier_config.ul_grid_size[i].value;
    gNB_config->carrier_config.dl_k0[i].value           = phy_config->cfg->carrier_config.dl_k0[i].value;
    gNB_config->carrier_config.ul_k0[i].value           = phy_config->cfg->carrier_config.ul_k0[i].value;
  }


  if (phy_config->cfg->cell_config.frame_duplex_type.value == 0) {
    gNB_config->cell_config.frame_duplex_type.value = FDD;
  } else {
    gNB_config->cell_config.frame_duplex_type.value = TDD;
  }

  memcpy((void*)&gNB_config->prach_config,(void*)&phy_config->cfg->prach_config,sizeof(phy_config->cfg->prach_config));
  memcpy((void*)&gNB_config->tdd_table,(void*)&phy_config->cfg->tdd_table,sizeof(phy_config->cfg->tdd_table));
  */
  memcpy((void*)gNB_config,phy_config->cfg,sizeof(*phy_config->cfg));
  RC.gNB[Mod_id]->mac_enabled     = 1;

  uint64_t dl_bw_khz = (12*gNB_config->carrier_config.dl_grid_size[gNB_config->ssb_config.scs_common.value].value)*(15<<gNB_config->ssb_config.scs_common.value);
  fp->dl_CarrierFreq = ((dl_bw_khz>>1) + gNB_config->carrier_config.dl_frequency.value)*1000 ;

  int32_t dlul_offset = 0;
  lte_frame_type_t frame_type = 0;
  
  get_band(fp->dl_CarrierFreq,&fp->nr_band,&dlul_offset,&frame_type);

  uint64_t ul_bw_khz = (12*gNB_config->carrier_config.ul_grid_size[gNB_config->ssb_config.scs_common.value].value)*(15<<gNB_config->ssb_config.scs_common.value);
  fp->ul_CarrierFreq = ((ul_bw_khz>>1) + gNB_config->carrier_config.uplink_frequency.value)*1000 ;

  AssertFatal(fp->ul_CarrierFreq==(fp->dl_CarrierFreq+dlul_offset), "Disagreement in uplink frequency for band %d\n", fp->nr_band);
  
  fp->threequarter_fs = openair0_cfg[0].threequarter_fs;
  LOG_I(PHY,"Configuring MIB for instance %d, : (Nid_cell %d,DL freq %llu, UL freq %llu)\n",
        Mod_id,
        gNB_config->cell_config.phy_cell_id.value,
        (unsigned long long)fp->dl_CarrierFreq,
        (unsigned long long)fp->ul_CarrierFreq);

  nr_init_frame_parms(gNB_config, fp);
  

  if (RC.gNB[Mod_id]->configured == 1) {
    LOG_E(PHY,"Already gNB already configured, do nothing\n");
    return;
  }

  RC.gNB[Mod_id]->configured     = 1;
  LOG_I(PHY,"gNB %d configured\n",Mod_id);
}

void init_nr_transport(PHY_VARS_gNB *gNB) {
  int i;
  int j;
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  nfapi_nr_config_request_scf_t *cfg = &gNB->gNB_config;
  LOG_I(PHY, "Initialise nr transport\n");
  uint16_t grid_size = cfg->carrier_config.dl_grid_size[fp->numerology_index].value;

  for (i=0; i<NUMBER_OF_NR_DLSCH_MAX; i++) {

    LOG_I(PHY,"Allocating Transport Channel Buffers for DLSCH %d/%d\n",i,NUMBER_OF_NR_DLSCH_MAX);

    for (j=0; j<2; j++) {
      gNB->dlsch[i][j] = new_gNB_dlsch(fp,1,16,NSOFT,0,grid_size);
      AssertFatal(gNB->dlsch[i][j]!=NULL,"Can't initialize dlsch %d \n", i);
    }
  }

  for (i=0; i<NUMBER_OF_NR_ULSCH_MAX; i++) {

    LOG_I(PHY,"Allocating Transport Channel Buffer for ULSCH, UE %d\n",i);

    for (j=0; j<2; j++) {
      // ULSCH for data
      gNB->ulsch[i][j] = new_gNB_ulsch(MAX_LDPC_ITERATIONS, fp->N_RB_UL, 0);

      if (!gNB->ulsch[i][j]) {
        LOG_E(PHY,"Can't get gNB ulsch structures\n");
        exit(-1);
      }

      /*
      LOG_I(PHY,"Initializing nFAPI for ULSCH, UE %d\n",i);
      // [hna] added here for RT implementation
      uint8_t harq_pid = 0;
      nfapi_nr_ul_config_ulsch_pdu *rel15_ul = &gNB->ulsch[i+1][j]->harq_processes[harq_pid]->ulsch_pdu;
  
      // --------- setting rel15_ul parameters ----------
      rel15_ul->rnti                           = 0x1234;
      rel15_ul->ulsch_pdu_rel15.start_rb       = 0;
      rel15_ul->ulsch_pdu_rel15.number_rbs     = 50;
      rel15_ul->ulsch_pdu_rel15.start_symbol   = 2;
      rel15_ul->ulsch_pdu_rel15.number_symbols = 12;
      rel15_ul->ulsch_pdu_rel15.length_dmrs    = gNB->dmrs_UplinkConfig.pusch_maxLength;
      rel15_ul->ulsch_pdu_rel15.Qm             = 2;
      rel15_ul->ulsch_pdu_rel15.R              = 679;
      rel15_ul->ulsch_pdu_rel15.mcs            = 9;
      rel15_ul->ulsch_pdu_rel15.rv             = 0;
      rel15_ul->ulsch_pdu_rel15.n_layers       = 1;
      ///////////////////////////////////////////////////
      */

    }

  }

  gNB->rx_total_gain_dB=130;


  //fp->pucch_config_common.deltaPUCCH_Shift = 1;
}
