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

/*! \file pre_processor.c
 * \brief eNB scheduler preprocessing fuction prior to scheduling
 * \author Navid Nikaein and Ankit Bhamri
 * \date 2013 - 2014
 * \email navid.nikaein@eurecom.fr
 * \version 1.0
 * @ingroup _mac

 */

#define _GNU_SOURCE
#include <stdlib.h>

#include "assertions.h"
#include "LAYER2/MAC/mac.h"
#include "LAYER2/MAC/mac_proto.h"
#include "LAYER2/MAC/mac_extern.h"
#include <openair2/LAYER2/NR_MAC_COMMON/nr_mac_extern.h>
#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "OCG.h"
#include "OCG_extern.h"
#include "RRC/LTE/rrc_extern.h"
#include "RRC/L2_INTERFACE/openair_rrc_L2_interface.h"
#include "rlc.h"
#include "PHY/LTE_TRANSPORT/transport_common_proto.h"

#include "common/ran_context.h"

extern RAN_CONTEXT_t RC;

#define DEBUG_eNB_SCHEDULER
#define DEBUG_HEADER_PARSING 1

int next_ue_list_looped(UE_list_t* list, int UE_id) {
  if (UE_id < 0)
    return list->head;
  return list->next[UE_id] < 0 ? list->head : list->next[UE_id];
}

int get_rbg_size_last(module_id_t Mod_id, int CC_id) {
  const int RBGsize = get_min_rb_unit(Mod_id, CC_id);
  const int N_RB_DL = to_prb(RC.mac[Mod_id]->common_channels[CC_id].mib->message.dl_Bandwidth);
  if (N_RB_DL == 15 || N_RB_DL == 25 || N_RB_DL == 50 || N_RB_DL == 75)
    return RBGsize - 1;
  else
    return RBGsize;
}

int g_start_ue_dl = -1;
int round_robin_dl(module_id_t Mod_id,
                   int CC_id,
                   int frame,
                   int subframe,
                   UE_list_t *UE_list,
                   int max_num_ue,
                   int n_rbg_sched,
                   uint8_t *rbgalloc_mask) {
  DevAssert(UE_list->head >= 0);
  DevAssert(n_rbg_sched > 0);
  const int N_RBG = to_rbg(RC.mac[Mod_id]->common_channels[CC_id].mib->message.dl_Bandwidth);
  const int RBGsize = get_min_rb_unit(Mod_id, CC_id);
  const int RBGlastsize = get_rbg_size_last(Mod_id, CC_id);
  int num_ue_req = 0;
  UE_info_t *UE_info = &RC.mac[Mod_id]->UE_info;

  int rb_required[MAX_MOBILES_PER_ENB]; // how much UEs request
  memset(rb_required, 0, sizeof(rb_required));

  int rbg = 0;
  for (; !rbgalloc_mask[rbg]; rbg++)
    ; /* fast-forward to first allowed RBG */

  // Calculate the amount of RBs every UE wants to send.
  for (int UE_id = UE_list->head; UE_id >= 0; UE_id = UE_list->next[UE_id]) {
    // check whether there are HARQ retransmissions
    const COMMON_channels_t *cc = &RC.mac[Mod_id]->common_channels[CC_id];
    const uint8_t harq_pid = frame_subframe2_dl_harq_pid(cc->tdd_Config, frame, subframe);
    UE_sched_ctrl_t *ue_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    const uint8_t round = ue_ctrl->round[CC_id][harq_pid];
    if (round != 8) { // retransmission: allocate
      const int nb_rb = UE_info->UE_template[CC_id][UE_id].nb_rb[harq_pid];
      if (nb_rb == 0)
        continue;
      int nb_rbg = (nb_rb + (nb_rb % RBGsize)) / RBGsize;
      // needs more RBGs than we can allocate
      if (nb_rbg > n_rbg_sched) {
        LOG_D(MAC,
              "retransmission of UE %d needs more RBGs (%d) than we have (%d)\n",
              UE_id, nb_rbg, n_rbg_sched);
        continue;
      }
      // ensure that the number of RBs can be contained by the RBGs (!), i.e.
      // if we allocate the last RBG this one should have the full RBGsize
      if ((nb_rb % RBGsize) == 0 && nb_rbg == n_rbg_sched
          && rbgalloc_mask[N_RBG - 1] && RBGlastsize != RBGsize) {
        LOG_D(MAC,
              "retransmission of UE %d needs %d RBs, but the last RBG %d is too small (%d, normal %d)\n",
              UE_id, nb_rb, N_RBG - 1, RBGlastsize, RBGsize);
        continue;
      }
      const uint8_t cqi = ue_ctrl->dl_cqi[CC_id];
      const int idx = CCE_try_allocate_dlsch(Mod_id, CC_id, subframe, UE_id, cqi);
      if (idx < 0)
        continue; // cannot allocate CCE
      ue_ctrl->pre_dci_dl_pdu_idx = idx;
      // retransmissions: directly allocate
      n_rbg_sched -= nb_rbg;
      ue_ctrl->pre_nb_available_rbs[CC_id] += nb_rb;
      for (; nb_rbg > 0; rbg++) {
        if (!rbgalloc_mask[rbg])
          continue;
        ue_ctrl->rballoc_sub_UE[CC_id][rbg] = 1;
        nb_rbg--;
      }
      LOG_D(MAC,
            "%4d.%d n_rbg_sched %d after retransmission reservation for UE %d "
            "round %d retx nb_rb %d pre_nb_available_rbs %d\n",
            frame, subframe, n_rbg_sched, UE_id, round,
            UE_info->UE_template[CC_id][UE_id].nb_rb[harq_pid],
            ue_ctrl->pre_nb_available_rbs[CC_id]);
      /* if there are no more RBG to give, return */
      if (n_rbg_sched <= 0)
        return 0;
      max_num_ue--;
      /* if there are no UEs that can be allocated anymore, return */
      if (max_num_ue == 0)
        return n_rbg_sched;
      for (; !rbgalloc_mask[rbg]; rbg++) /* fast-forward */ ;
    } else {
      const int dlsch_mcs1 = cqi_to_mcs[UE_info->UE_sched_ctrl[UE_id].dl_cqi[CC_id]];
      UE_info->eNB_UE_stats[CC_id][UE_id].dlsch_mcs1 = dlsch_mcs1;
      rb_required[UE_id] =
          find_nb_rb_DL(dlsch_mcs1,
                        UE_info->UE_template[CC_id][UE_id].dl_buffer_total,
                        n_rbg_sched * RBGsize,
                        RBGsize);
      if (rb_required[UE_id] > 0)
        num_ue_req++;
      LOG_D(MAC,
            "%d/%d UE_id %d rb_required %d n_rbg_sched %d\n",
            frame,
            subframe,
            UE_id,
            rb_required[UE_id],
            n_rbg_sched);
    }
  }

  if (num_ue_req == 0)
    return n_rbg_sched; // no UE has a transmission

  // after allocating retransmissions: build list of UE to allocate.
  // if start_UE does not exist anymore (detach etc), start at first UE
  if (g_start_ue_dl == -1)
    g_start_ue_dl = UE_list->head;
  int UE_id = g_start_ue_dl;
  UE_list_t UE_sched;
  int rb_required_total = 0;
  int num_ue_sched = 0;
  max_num_ue = min(min(max_num_ue, num_ue_req), n_rbg_sched);
  int *cur_UE = &UE_sched.head;
  while (num_ue_sched < max_num_ue) {
    while (rb_required[UE_id] == 0)
      UE_id = next_ue_list_looped(UE_list, UE_id);
    const uint8_t cqi = UE_info->UE_sched_ctrl[UE_id].dl_cqi[CC_id];
    const int idx = CCE_try_allocate_dlsch(Mod_id, CC_id, subframe, UE_id, cqi);
    if (idx < 0) {
      LOG_D(MAC, "cannot allocate CCE for UE %d, skipping\n", UE_id);
      num_ue_req--;
      max_num_ue = min(min(max_num_ue, num_ue_req), n_rbg_sched);
      UE_id = next_ue_list_looped(UE_list, UE_id); // next candidate
      continue;
    }
    UE_info->UE_sched_ctrl[UE_id].pre_dci_dl_pdu_idx = idx;
    *cur_UE = UE_id;
    cur_UE = &UE_sched.next[UE_id];
    rb_required_total += rb_required[UE_id];
    num_ue_sched++;
    UE_id = next_ue_list_looped(UE_list, UE_id); // next candidate
  }
  *cur_UE = -1;

  /* for one UE after the next: allocate resources */
  for (int UE_id = UE_sched.head; UE_id >= 0;
       UE_id = next_ue_list_looped(&UE_sched, UE_id)) {
    if (rb_required[UE_id] <= 0)
      continue;
    UE_sched_ctrl_t *ue_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    ue_ctrl->rballoc_sub_UE[CC_id][rbg] = 1;
    const int sRBG = rbg == N_RBG - 1 ? RBGlastsize : RBGsize;
    ue_ctrl->pre_nb_available_rbs[CC_id] += sRBG;
    rb_required[UE_id] -= sRBG;
    rb_required_total -= sRBG;
    if (rb_required_total <= 0)
      break;
    n_rbg_sched--;
    if (n_rbg_sched <= 0)
      break;
    for (rbg++; !rbgalloc_mask[rbg]; rbg++) /* fast-forward */ ;
  }

  /* if not all UEs could be allocated in this round */
  if (num_ue_req > max_num_ue) {
    /* go to the first one we missed */
    for (int i = 0; i < max_num_ue; ++i)
      g_start_ue_dl = next_ue_list_looped(UE_list, g_start_ue_dl);
  } else {
    /* else, just start with the next UE next time */
    g_start_ue_dl = next_ue_list_looped(UE_list, g_start_ue_dl);
  }

  return n_rbg_sched;
}

// This function stores the downlink buffer for all the logical channels
void
store_dlsch_buffer(module_id_t Mod_id,
                   int CC_id,
                   frame_t frameP,
                   sub_frame_t subframeP) {
  UE_info_t *UE_info = &RC.mac[Mod_id]->UE_info;

  for (int UE_id = UE_info->list.head; UE_id >= 0; UE_id = UE_info->list.next[UE_id]) {

    UE_TEMPLATE *UE_template = &UE_info->UE_template[CC_id][UE_id];
    UE_sched_ctrl_t *UE_sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    UE_template->dl_buffer_total = 0;
    UE_template->dl_pdus_total = 0;

    /* loop over all activated logical channels */
    for (int i = 0; i < UE_sched_ctrl->dl_lc_num; ++i) {
      const int lcid = UE_sched_ctrl->dl_lc_ids[i];
      const mac_rlc_status_resp_t rlc_status = mac_rlc_status_ind(Mod_id,
                                                                  UE_template->rnti,
                                                                  Mod_id,
                                                                  frameP,
                                                                  subframeP,
                                                                  ENB_FLAG_YES,
                                                                  MBMS_FLAG_NO,
                                                                  lcid,
                                                                  0,
                                                                  0
                                                                 );
      UE_template->dl_buffer_info[lcid] = rlc_status.bytes_in_buffer;
      UE_template->dl_pdus_in_buffer[lcid] = rlc_status.pdus_in_buffer;
      UE_template->dl_buffer_head_sdu_creation_time[lcid] = rlc_status.head_sdu_creation_time;
      UE_template->dl_buffer_head_sdu_creation_time_max =
        cmax(UE_template->dl_buffer_head_sdu_creation_time_max, rlc_status.head_sdu_creation_time);
      UE_template->dl_buffer_head_sdu_remaining_size_to_send[lcid] = rlc_status.head_sdu_remaining_size_to_send;
      UE_template->dl_buffer_head_sdu_is_segmented[lcid] = rlc_status.head_sdu_is_segmented;
      UE_template->dl_buffer_total += UE_template->dl_buffer_info[lcid];
      UE_template->dl_pdus_total += UE_template->dl_pdus_in_buffer[lcid];

      /* update the number of bytes in the UE_sched_ctrl. The DLSCH will use
       * this to request the corresponding data from the RLC, and this might be
       * limited in the preprocessor */
      UE_sched_ctrl->dl_lc_bytes[i] = rlc_status.bytes_in_buffer;

#ifdef DEBUG_eNB_SCHEDULER
      /* note for dl_buffer_head_sdu_remaining_size_to_send[lcid] :
       * 0 if head SDU has not been segmented (yet), else remaining size not already segmented and sent
       */
      if (UE_template->dl_buffer_info[lcid] > 0)
        LOG_D(MAC,
              "[eNB %d] Frame %d Subframe %d : RLC status for UE %d in LCID%d: total of %d pdus and size %d, head sdu queuing time %d, remaining size %d, is segmeneted %d \n",
              Mod_id, frameP,
              subframeP, UE_id, lcid, UE_template->dl_pdus_in_buffer[lcid],
              UE_template->dl_buffer_info[lcid],
              UE_template->dl_buffer_head_sdu_creation_time[lcid],
              UE_template->dl_buffer_head_sdu_remaining_size_to_send[lcid],
              UE_template->dl_buffer_head_sdu_is_segmented[lcid]);
#endif
    }

    if (UE_template->dl_buffer_total > 0)
      LOG_D(MAC,
            "[eNB %d] Frame %d Subframe %d : RLC status for UE %d : total DL buffer size %d and total number of pdu %d \n",
            Mod_id, frameP, subframeP, UE_id,
            UE_template->dl_buffer_total,
            UE_template->dl_pdus_total);
  }
}


// This function assigns pre-available RBS to each UE in specified sub-bands before scheduling is done
void
dlsch_scheduler_pre_processor(module_id_t Mod_id,
                              int CC_id,
                              frame_t frameP,
                              sub_frame_t subframeP) {
  UE_info_t *UE_info = &RC.mac[Mod_id]->UE_info;
  const int N_RBG = to_rbg(RC.mac[Mod_id]->common_channels[CC_id].mib->message.dl_Bandwidth);
  const int RBGsize = get_min_rb_unit(Mod_id, CC_id);

  store_dlsch_buffer(Mod_id, CC_id, frameP, subframeP);

  UE_list_t UE_to_sched;
  UE_to_sched.head = -1;
  for (int i = 0; i < MAX_MOBILES_PER_ENB; ++i)
    UE_to_sched.next[i] = -1;

  int first = 1;
  int last_UE_id = -1;
  for (int UE_id = UE_info->list.head; UE_id >= 0; UE_id = UE_info->list.next[UE_id]) {
    UE_sched_ctrl_t *ue_sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];
    const UE_TEMPLATE *ue_template = &UE_info->UE_template[CC_id][UE_id];

    /* initialize per-UE scheduling information */
    ue_sched_ctrl->pre_nb_available_rbs[CC_id] = 0;
    ue_sched_ctrl->dl_pow_off[CC_id] = 2;
    memset(ue_sched_ctrl->rballoc_sub_UE[CC_id], 0, sizeof(ue_sched_ctrl->rballoc_sub_UE[CC_id]));
    ue_sched_ctrl->pre_dci_dl_pdu_idx = -1;

    const rnti_t rnti = UE_RNTI(Mod_id, UE_id);
    if (rnti == NOT_A_RNTI) {
      LOG_E(MAC, "UE %d has RNTI NOT_A_RNTI!\n", UE_id);
      continue;
    }
    if (UE_info->active[UE_id] != TRUE) {
      LOG_E(MAC, "UE %d RNTI %x is NOT active!\n", UE_id, rnti);
      continue;
    }
    if (ue_template->rach_resource_type > 0) {
      LOG_D(MAC,
            "UE %d is RACH resource type %d\n",
            UE_id,
            ue_template->rach_resource_type);
      continue;
    }
    if (mac_eNB_get_rrc_status(Mod_id, rnti) < RRC_CONNECTED) {
      LOG_D(MAC, "UE %d is not in RRC_CONNECTED\n", UE_id);
      continue;
    }

    /* define UEs to schedule */
    if (first) {
      first = 0;
      UE_to_sched.head = UE_id;
    } else {
      UE_to_sched.next[last_UE_id] = UE_id;
    }
    UE_to_sched.next[UE_id] = -1;
    last_UE_id = UE_id;
  }

  if (UE_to_sched.head < 0)
    return;

  uint8_t *vrb_map = RC.mac[Mod_id]->common_channels[CC_id].vrb_map;
  uint8_t rbgalloc_mask[N_RBG_MAX];
  int n_rbg_sched = 0;
  for (int i = 0; i < N_RBG; i++) {
    // calculate mask: init to one + "AND" with vrb_map:
    // if any RB in vrb_map is blocked (1), the current RBG will be 0
    rbgalloc_mask[i] = 1;
    for (int j = 0; j < RBGsize; j++)
      rbgalloc_mask[i] &= !vrb_map[RBGsize * i + j];
    n_rbg_sched += rbgalloc_mask[i];
  }

  round_robin_dl(Mod_id,
                 CC_id,
                 frameP,
                 subframeP,
                 &UE_to_sched,
                 4, // max_num_ue
                 n_rbg_sched,
                 rbgalloc_mask);

  // the following block is meant for validation of the pre-processor to check
  // whether all UE allocations are non-overlapping and is not necessary for
  // scheduling functionality
#ifdef DEBUG_eNB_SCHEDULER
  char t[26] = "_________________________";
  t[N_RBG] = 0;
  for (int i = 0; i < N_RBG; i++)
    for (int j = 0; j < RBGsize; j++)
      if (vrb_map[RBGsize*i+j] != 0)
        t[i] = 'x';
  int print = 0;
  for (int UE_id = UE_info->list.head; UE_id >= 0; UE_id = UE_info->list.next[UE_id]) {
    const UE_sched_ctrl_t *ue_sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];

    if (ue_sched_ctrl->pre_nb_available_rbs[CC_id] == 0)
      continue;

    LOG_D(MAC,
          "%4d.%d UE%d %d RBs allocated, pre MCS %d\n",
          frameP,
          subframeP,
          UE_id,
          ue_sched_ctrl->pre_nb_available_rbs[CC_id],
          UE_info->eNB_UE_stats[CC_id][UE_id].dlsch_mcs1);

    print = 1;

    for (int i = 0; i < N_RBG; i++) {
      if (!ue_sched_ctrl->rballoc_sub_UE[CC_id][i])
        continue;
      for (int j = 0; j < RBGsize; j++) {
        if (vrb_map[RBGsize*i+j] != 0) {
          LOG_I(MAC, "%4d.%d DL scheduler allocation list: %s\n", frameP, subframeP, t);
          LOG_E(MAC, "%4d.%d: UE %d allocated at locked RB %d/RBG %d\n", frameP,
                subframeP, UE_id, RBGsize * i + j, i);
        }
        vrb_map[RBGsize*i+j] = 1;
      }
      t[i] = '0' + UE_id;
    }
  }
  if (print)
    LOG_D(MAC, "%4d.%d DL scheduler allocation list: %s\n", frameP, subframeP, t);
#endif
}

/// ULSCH PRE_PROCESSOR

void calculate_max_mcs_min_rb(module_id_t mod_id,
                              int CC_id,
                              int bytes,
                              int phr,
                              int max_mcs,
                              int *mcs,
                              int max_rbs,
                              int *rb_index,
                              int *tx_power) {
  const int Ncp = RC.mac[mod_id]->common_channels[CC_id].Ncp;
  /* TODO shouldn't we consider the SRS or other quality indicators? */
  *mcs = max_mcs;
  *rb_index = 2;
  int tbs = get_TBS_UL(*mcs, rb_table[*rb_index]);

  // fixme: set use_srs flag
  *tx_power = estimate_ue_tx_power(tbs * 8, rb_table[*rb_index], 0, Ncp, 0);

  /* find maximum MCS */
  while ((phr - *tx_power < 0 || tbs > bytes) && *mcs > 3) {
    mcs--;
    tbs = get_TBS_UL(*mcs, rb_table[*rb_index]);
    *tx_power = estimate_ue_tx_power(tbs * 8, rb_table[*rb_index], 0, Ncp, 0);
  }

  /* find minimum necessary RBs */
  while (tbs < bytes
         && *rb_index < 32
         && rb_table[*rb_index] < max_rbs
         && phr - *tx_power > 0) {
    (*rb_index)++;
    tbs = get_TBS_UL(*mcs, rb_table[*rb_index]);
    *tx_power = estimate_ue_tx_power(tbs * 8, rb_table[*rb_index], 0, Ncp, 0);
  }

  /* Decrease if we went to far in last iteration */
  if (rb_table[*rb_index] > max_rbs)
    (*rb_index)--;

  // 1 or 2 PRB with cqi enabled does not work well
  if (rb_table[*rb_index] < 3) {
    *rb_index = 2; //3PRB
  }
}

int pp_find_rb_table_index(int approximate) {
  int lo = 2;
  if (approximate <= rb_table[lo])
    return lo;
  int hi = sizeof(rb_table) - 1;
  if (approximate >= rb_table[hi])
    return hi;
  int p = (hi + lo) / 2;
  for (; lo + 1 != hi; p = (hi + lo) / 2) {
    if (approximate <= rb_table[p])
      hi = p;
    else
      lo = p;
  }
  return p + 1;
}

int g_start_ue_ul = -1;
int round_robin_ul(module_id_t Mod_id,
                   int CC_id,
                   int frame,
                   int subframe,
                   int sched_frame,
                   int sched_subframe,
                   UE_list_t *UE_list,
                   int max_num_ue,
                   int num_contig_rb,
                   contig_rbs_t *rbs) {
  AssertFatal(num_contig_rb <= 2, "cannot handle more than two contiguous RB regions\n");
  UE_info_t *UE_info = &RC.mac[Mod_id]->UE_info;
  const int max_rb = num_contig_rb > 1 ? MAX(rbs[0].length, rbs[1].length) : rbs[0].length;

  /* for every UE: check whether we have to handle a retransmission (and
   * allocate, if so). If not, compute how much RBs this UE would need */
  int rb_idx_required[MAX_MOBILES_PER_ENB];
  memset(rb_idx_required, 0, sizeof(rb_idx_required));
  int num_ue_req = 0;
  for (int UE_id = UE_list->head; UE_id >= 0; UE_id = UE_list->next[UE_id]) {
    UE_TEMPLATE *UE_template = &UE_info->UE_template[CC_id][UE_id];
    uint8_t harq_pid = subframe2harqpid(&RC.mac[Mod_id]->common_channels[CC_id],
                                        sched_frame, sched_subframe);
    if (UE_info->UE_sched_ctrl[UE_id].round_UL[CC_id][harq_pid] > 0) {
      /* this UE has a retransmission, allocate it right away */
      const int nb_rb = UE_template->nb_rb_ul[harq_pid];
      if (nb_rb == 0) {
        LOG_E(MAC,
              "%4d.%d UE %d retransmission of 0 RBs in round %d, ignoring\n",
              sched_frame, sched_subframe, UE_id,
              UE_info->UE_sched_ctrl[UE_id].round_UL[CC_id][harq_pid]);
        continue;
      }
      const uint8_t cqi = UE_info->UE_sched_ctrl[UE_id].dl_cqi[CC_id];
      const int idx = CCE_try_allocate_ulsch(Mod_id, CC_id, subframe, UE_id, cqi);
      if (idx < 0)
        continue; // cannot allocate CCE
      UE_template->pre_dci_ul_pdu_idx = idx;
      if (rbs[0].length >= nb_rb) { // fits in first contiguous region
        UE_template->pre_first_nb_rb_ul = rbs[0].start;
        rbs[0].length -= nb_rb;
        rbs[0].start += nb_rb;
      } else if (num_contig_rb == 2 && rbs[1].length >= nb_rb) { // in second
        UE_template->pre_first_nb_rb_ul = rbs[1].start;
        rbs[1].length -= nb_rb;
        rbs[1].start += nb_rb;
      } else if (num_contig_rb == 2
          && rbs[1].start + rbs[1].length - rbs[0].start >= nb_rb) { // overlapping the middle
        UE_template->pre_first_nb_rb_ul = rbs[0].start;
        rbs[0].length = 0;
        int ol = nb_rb - (rbs[1].start - rbs[0].start); // how much overlap in second region
        if (ol > 0) {
          rbs[1].length -= ol;
          rbs[1].start += ol;
        }
      } else {
        LOG_W(MAC,
              "cannot allocate UL retransmission for UE %d (nb_rb %d)\n",
              UE_id,
              nb_rb);
        UE_template->pre_dci_ul_pdu_idx = -1; // do not need CCE
        RC.mac[Mod_id]->HI_DCI0_req[CC_id][subframe].hi_dci0_request_body.number_of_dci--;
        continue;
      }
      LOG_D(MAC, "%4d.%d UE %d retx %d RBs at start %d\n",
            sched_frame,
            sched_subframe,
            UE_id,
            UE_template->pre_allocated_nb_rb_ul,
            UE_template->pre_first_nb_rb_ul);
      UE_template->pre_allocated_nb_rb_ul = nb_rb;
      max_num_ue--;
      if (max_num_ue == 0) /* in this case, cannot allocate any other UE anymore */
        return rbs[0].length + (num_contig_rb > 1 ? rbs[1].length : 0);
      continue;
    }

    const int B = cmax(UE_template->estimated_ul_buffer - UE_template->scheduled_ul_bytes, 0);
    const int UE_to_be_scheduled = UE_is_to_be_scheduled(Mod_id, CC_id, UE_id);
    if (B == 0 && !UE_to_be_scheduled)
      continue;

    num_ue_req++;

    /* if UE has pending scheduling request then pre-allocate 3 RBs */
    if (B == 0 && UE_to_be_scheduled) {
      UE_template->pre_assigned_mcs_ul = 10; /* use QPSK mcs only */
      rb_idx_required[UE_id] = 2;
      //UE_template->pre_allocated_nb_rb_ul = 3;
      continue;
    }

    int mcs;
    int rb_table_index;
    int tx_power;
    calculate_max_mcs_min_rb(
        Mod_id,
        CC_id,
        B,
        UE_template->phr_info,
        UE_info->UE_sched_ctrl[UE_id].phr_received == 1 ? 20 : 10,
        &mcs,
        max_rb,
        &rb_table_index,
        &tx_power);

    UE_template->pre_assigned_mcs_ul = mcs;
    rb_idx_required[UE_id] = rb_table_index;
    //UE_template->pre_allocated_nb_rb_ul = rb_table[rb_table_index];
    /* only print log when PHR changed */
    static int phr = 0;
    if (phr != UE_template->phr_info) {
      phr = UE_template->phr_info;
      LOG_D(MAC, "%d.%d UE %d CC %d: pre mcs %d, pre rb_table[%d]=%d RBs (phr %d, tx power %d, bytes %d)\n",
            frame,
            subframe,
            UE_id,
            CC_id,
            UE_template->pre_assigned_mcs_ul,
            UE_template->pre_allocated_rb_table_index_ul,
            UE_template->pre_allocated_nb_rb_ul,
            UE_template->phr_info,
            tx_power,
            B);
    }
  }

  if (num_ue_req == 0)
    return rbs[0].length + (num_contig_rb > 1 ? rbs[1].length : 0);

  // calculate how many users should be in both regions, and to maximize usage,
  // go from the larger to the smaller one which at least will handle a single
  // full load case better.
  const int n = min(num_ue_req, max_num_ue);
  int nr[2] = {n, 0};
  int step = 1; // the order if we have two regions
  int start = 0;
  int end = 1;
  if (num_contig_rb > 1) {
    // proportionally divide between both regions
    int la = rbs[0].length > 0 ? rbs[0].length : 1;
    int lb = rbs[1].length > 0 ? rbs[1].length : 1;
    nr[1] = min(max(n/(la/lb + 1), 1), n - 1);
    nr[0] = n - nr[1];
    step = la > lb ? 1 : -1; // 1: from 0 to 1, -1: from 1 to 0
    start = la > lb ? 0 : 1;
    end = la > lb ? 2 : -1;
  }

  if (g_start_ue_ul == -1)
    g_start_ue_ul = UE_list->head;
  int sUE_id = g_start_ue_ul;
  int rb_idx_given[MAX_MOBILES_PER_ENB];
  memset(rb_idx_given, 0, sizeof(rb_idx_given));

  for (int r = start; r != end; r += step) {
    // don't allocate if we have too little RBs
    if (rbs[r].length < 3)
      continue;
    if (nr[r] <= 0)
      continue;

    UE_list_t UE_sched;
    // average RB index: just below the index that fits all UEs
    int start_idx = pp_find_rb_table_index(rbs[r].length / nr[r]) - 1;
    int num_ue_sched = 0;
    int rb_required_add = 0;
    int *cur_UE = &UE_sched.head;
    while (num_ue_sched < nr[r]) {
      while (rb_idx_required[sUE_id] == 0)
        sUE_id = next_ue_list_looped(UE_list, sUE_id);
      const int cqi = UE_info->UE_sched_ctrl[sUE_id].dl_cqi[CC_id];
      const int idx = CCE_try_allocate_ulsch(Mod_id, CC_id, subframe, sUE_id, cqi);
      if (idx < 0) {
        LOG_D(MAC, "cannot allocate CCE for UE %d, skipping\n", sUE_id);
        nr[r]--;
        sUE_id = next_ue_list_looped(UE_list, sUE_id); // next candidate
        continue;
      }
      UE_info->UE_template[CC_id][sUE_id].pre_dci_ul_pdu_idx = idx;
      *cur_UE = sUE_id;
      cur_UE = &UE_sched.next[sUE_id];
      rb_idx_given[sUE_id] = min(start_idx, rb_idx_required[sUE_id]);
      rb_required_add += rb_table[rb_idx_required[sUE_id]] - rb_table[rb_idx_given[sUE_id]];
      rbs[r].length -= rb_table[rb_idx_given[sUE_id]];
      num_ue_sched++;
      sUE_id = next_ue_list_looped(UE_list, sUE_id);
    }
    *cur_UE = -1;

    /* give remaining RBs in RR fashion. Since we don't know in advance the
     * amount of RBs we can give (the "step size" in rb_table is non-linear), go
     * through all UEs and try to give a bit more. Continue until no UE can be
     * given a higher index because the remaining RBs do not suffice to increase */
    int UE_id = UE_sched.head;
    int rb_required_add_old;
    do {
      rb_required_add_old = rb_required_add;
      for (int UE_id = UE_sched.head; UE_id >= 0; UE_id = UE_sched.next[UE_id]) {
        if (rb_idx_given[UE_id] >= rb_idx_required[UE_id])
          continue; // this UE does not need more
        const int new_idx = rb_idx_given[UE_id] + 1;
        const int rb_inc = rb_table[new_idx] - rb_table[rb_idx_given[UE_id]];
        if (rbs[r].length < rb_inc)
          continue;
        rb_idx_given[UE_id] = new_idx;
        rbs[r].length -= rb_inc;
        rb_required_add -= rb_inc;
      }
    } while (rb_required_add != rb_required_add_old);

    for (UE_id = UE_sched.head; UE_id >= 0; UE_id = UE_sched.next[UE_id]) {
      UE_TEMPLATE *UE_template = &UE_info->UE_template[CC_id][UE_id];

      /* MCS has been allocated previously */
      UE_template->pre_first_nb_rb_ul = rbs[r].start;
      UE_template->pre_allocated_rb_table_index_ul = rb_idx_given[UE_id];
      UE_template->pre_allocated_nb_rb_ul = rb_table[rb_idx_given[UE_id]];
      rbs[r].start += rb_table[rb_idx_given[UE_id]];
      LOG_D(MAC, "%4d.%d UE %d allocated %d RBs start %d new start %d\n",
            sched_frame,
            sched_subframe,
            UE_id,
            UE_template->pre_allocated_nb_rb_ul,
            UE_template->pre_first_nb_rb_ul,
            rbs[r].start);
    }
  }

  /* if not all UEs could be allocated in this round */
  if (num_ue_req > max_num_ue) {
    /* go to the first one we missed */
    for (int i = 0; i < max_num_ue; ++i)
      g_start_ue_ul = next_ue_list_looped(UE_list, g_start_ue_ul);
  } else {
    /* else, just start with the next UE next time */
    g_start_ue_ul = next_ue_list_looped(UE_list, g_start_ue_ul);
  }

  return rbs[0].length + (num_contig_rb > 1 ? rbs[1].length : 0);
}

void ulsch_scheduler_pre_processor(module_id_t Mod_id,
                                   int CC_id,
                                   int frameP,
                                   sub_frame_t subframeP,
                                   int sched_frameP,
                                   unsigned char sched_subframeP) {
  UE_info_t *UE_info = &RC.mac[Mod_id]->UE_info;
  const int N_RB_UL = to_prb(RC.mac[Mod_id]->common_channels[CC_id].ul_Bandwidth);
  COMMON_channels_t *cc = &RC.mac[Mod_id]->common_channels[CC_id];

  UE_list_t UE_to_sched;
  UE_to_sched.head = -1;
  for (int i = 0; i < MAX_MOBILES_PER_ENB; ++i)
    UE_to_sched.next[i] = -1;

  int last_UE_id = -1;
  for (int UE_id = UE_info->list.head; UE_id >= 0; UE_id = UE_info->list.next[UE_id]) {
    UE_TEMPLATE *UE_template = &UE_info->UE_template[CC_id][UE_id];
    UE_sched_ctrl_t *ue_sched_ctrl = &UE_info->UE_sched_ctrl[UE_id];

    /* initialize per-UE scheduling information */
    UE_template->pre_assigned_mcs_ul = 0;
    UE_template->pre_allocated_nb_rb_ul = 0;
    UE_template->pre_allocated_rb_table_index_ul = -1;
    UE_template->pre_first_nb_rb_ul = 0;
    UE_template->pre_dci_ul_pdu_idx = -1;

    const rnti_t rnti = UE_RNTI(Mod_id, UE_id);
    if (rnti == NOT_A_RNTI) {
      LOG_E(MAC, "UE %d has RNTI NOT_A_RNTI!\n", UE_id);
      continue;
    }
    if (ue_sched_ctrl->cdrx_configured && !ue_sched_ctrl->in_active_time)
      continue;
    if (UE_info->UE_template[CC_id][UE_id].rach_resource_type > 0)
      continue;

    /* define UEs to schedule */
    if (UE_to_sched.head < 0)
      UE_to_sched.head = UE_id;
    else
      UE_to_sched.next[last_UE_id] = UE_id;
    UE_to_sched.next[UE_id] = -1;
    last_UE_id = UE_id;
  }

  if (UE_to_sched.head < 0)
    return;

  int last_rb_blocked = 1;
  int n_contig = 0;
  contig_rbs_t rbs[2]; // up to two contig RBs for PRACH in between
  for (int i = 0; i < N_RB_UL; ++i) {
    if (cc->vrb_map_UL[i] == 0 && last_rb_blocked == 1) {
      last_rb_blocked = 0;
      n_contig++;
      AssertFatal(n_contig <= 2, "cannot handle more than two contiguous RB regions\n");
      rbs[n_contig - 1].start = i;
    }
    if (cc->vrb_map_UL[i] == 1 && last_rb_blocked == 0) {
      last_rb_blocked = 1;
      rbs[n_contig - 1].length = i - rbs[n_contig - 1].start;
    }
  }

  round_robin_ul(Mod_id,
                 CC_id,
                 frameP,
                 subframeP,
                 sched_frameP,
                 sched_subframeP,
                 &UE_to_sched,
                 4, // max_num_ue
                 n_contig,
                 rbs);

  // the following block is meant for validation of the pre-processor to check
  // whether all UE allocations are non-overlapping and is not necessary for
  // scheduling functionality
#ifdef DEBUG_eNB_SCHEDULER
  char t[101] = "__________________________________________________"
                "__________________________________________________";
  t[N_RB_UL] = 0;
  for (int j = 0; j < N_RB_UL; j++)
    if (cc->vrb_map_UL[j] != 0)
      t[j] = 'x';
  int print = 0;
  for (int UE_id = UE_info->list.head; UE_id >= 0; UE_id = UE_info->list.next[UE_id]) {
    UE_TEMPLATE *UE_template = &UE_info->UE_template[CC_id][UE_id];
    if (UE_template->pre_allocated_nb_rb_ul == 0)
      continue;

    print = 1;
    uint8_t harq_pid = subframe2harqpid(&RC.mac[Mod_id]->common_channels[CC_id],
                                        sched_frameP, sched_subframeP);
    LOG_D(MAC, "%4d.%d UE%d %d RBs (index %d) at start %d, pre MCS %d %s\n",
          frameP,
          subframeP,
          UE_id,
          UE_template->pre_allocated_nb_rb_ul,
          UE_template->pre_allocated_rb_table_index_ul,
          UE_template->pre_first_nb_rb_ul,
          UE_template->pre_assigned_mcs_ul,
          UE_info->UE_sched_ctrl[UE_id].round_UL[CC_id][harq_pid] > 0 ? "(retx)" : "");

    for (int i = 0; i < UE_template->pre_allocated_nb_rb_ul; ++i) {
      /* only check if this is not a retransmission */
      if (UE_info->UE_sched_ctrl[UE_id].round_UL[CC_id][harq_pid] == 0
          && cc->vrb_map_UL[UE_template->pre_first_nb_rb_ul + i] == 1) {

        LOG_I(MAC, "%4d.%d UL scheduler allocation list: %s\n", frameP, subframeP, t);
        LOG_E(MAC,
              "%4d.%d: UE %d allocated at locked RB %d (is: allocated start "
              "%d/length %d)\n",
              frameP, subframeP, UE_id, UE_template->pre_first_nb_rb_ul + i,
              UE_template->pre_first_nb_rb_ul,
              UE_template->pre_allocated_nb_rb_ul);
      }
      cc->vrb_map_UL[UE_template->pre_first_nb_rb_ul + i] = 1;
      t[UE_template->pre_first_nb_rb_ul + i] = UE_id + '0';
    }
  }
  if (print)
    LOG_D(MAC,
          "%4d.%d UL scheduler allocation list: %s\n",
          sched_frameP,
          sched_subframeP,
          t);
#endif
}
