/*
 * PROJECT: GEMMapper
 * FILE: search_state_region_profile.c
 * DATE: 06/06/2012
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 */

#include "search_pipeline/search_stage_region_profile.h"
#include "search_pipeline/search_stage_region_profile_buffer.h"
#include "archive/archive_search_se_stepwise.h"

/*
 * Profile
 */
#define PROFILE_LEVEL PMED

/*
 * Error Messages
 */
#define GEM_ERROR_SEARCH_STAGE_RP_UNPAIRED_QUERY "Search-Stage Region-Profile. Couldn't retrieve query-pair"

/*
 * Internal Accessors
 */
search_stage_region_profile_buffer_t* search_stage_rp_get_buffer(
    search_stage_region_profile_t* const search_stage_rp,
    const uint64_t buffer_pos) {
  return *vector_get_elm(search_stage_rp->buffers,buffer_pos,search_stage_region_profile_buffer_t*);
}
search_stage_region_profile_buffer_t* search_stage_rp_get_current_buffer(
    search_stage_region_profile_t* const search_stage_rp) {
  return search_stage_rp_get_buffer(search_stage_rp,search_stage_rp->iterator.current_buffer_idx);
}
/*
 * Setup
 */
search_stage_region_profile_t* search_stage_region_profile_new(
    const gpu_buffer_collection_t* const gpu_buffer_collection,
    const uint64_t buffers_offset,
    const uint64_t num_buffers,
    const bool region_profile_enabled,
    const uint32_t occ_min_threshold,
    const uint32_t extra_search_steps,
    const uint32_t alphabet_size) {
  // Alloc
  search_stage_region_profile_t* const search_stage_rp = mm_alloc(search_stage_region_profile_t);
  // Init Buffers
  uint64_t i;
  search_stage_rp->buffers = vector_new(num_buffers,search_stage_region_profile_buffer_t*);
  for (i=0;i<num_buffers;++i) {
    search_stage_region_profile_buffer_t* const buffer_vc =
        search_stage_region_profile_buffer_new(
            gpu_buffer_collection,buffers_offset+i,region_profile_enabled,
            occ_min_threshold,extra_search_steps,alphabet_size);
    vector_insert(search_stage_rp->buffers,buffer_vc,search_stage_region_profile_buffer_t*);
  }
  search_stage_rp->iterator.num_buffers = num_buffers;
  search_stage_region_profile_clear(search_stage_rp,NULL); // Clear buffers
  // Return
  return search_stage_rp;
}
void search_stage_region_profile_clear(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_cache_t* const archive_search_cache) {
  // Init state
  search_stage_rp->search_stage_mode = search_group_buffer_phase_sending;
  // Clear & Init buffers
  const uint64_t num_buffers = search_stage_rp->iterator.num_buffers;
  uint64_t i;
  for (i=0;i<num_buffers;++i) {
    search_stage_region_profile_buffer_clear(
        search_stage_rp_get_buffer(search_stage_rp,i),archive_search_cache);
  }
  search_stage_rp->iterator.current_buffer_idx = 0; // Init iterator
}
void search_stage_region_profile_delete(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_cache_t* const archive_search_cache) {
  // Delete buffers
  const uint64_t num_buffers = search_stage_rp->iterator.num_buffers;
  uint64_t i;
  for (i=0;i<num_buffers;++i) {
    search_stage_region_profile_buffer_delete(
        search_stage_rp_get_buffer(search_stage_rp,i),archive_search_cache);
  }
  vector_delete(search_stage_rp->buffers); // Delete vector
  mm_free(search_stage_rp); // Free handler
}
/*
 * Send Searches (buffered)
 */
bool search_stage_region_profile_send_se_search(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_t* const archive_search) {
  // Check Occupancy (fits in current buffer)
  search_stage_region_profile_buffer_t* current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
  while (!search_stage_region_profile_buffer_fits(current_buffer,archive_search,NULL)) {
    // Change group
    const uint64_t last_buffer_idx = search_stage_rp->iterator.num_buffers - 1;
    if (search_stage_rp->iterator.current_buffer_idx < last_buffer_idx) {
      // Send the current group to verification
      search_stage_region_profile_buffer_send(current_buffer);
      // Next buffer
      ++(search_stage_rp->iterator.current_buffer_idx);
      current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
    } else {
      return false;
    }
  }
  // Add SE Search
  search_stage_region_profile_buffer_add(current_buffer,archive_search);
  // Copy profile-partitions to the buffer
#ifdef GPU_REGION_PROFILE_ADAPTIVE
  archive_search_se_stepwise_region_profile_adaptive_copy(archive_search,current_buffer->gpu_buffer_fmi_asearch);
#else
  archive_search_se_stepwise_region_profile_static_copy(archive_search,current_buffer->gpu_buffer_fmi_ssearch);
#endif
  // Return ok
  return true;
}
bool search_stage_region_profile_send_pe_search(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_t* const archive_search_end1,
    archive_search_t* const archive_search_end2) {
  // Check Occupancy (fits in current buffer)
  search_stage_region_profile_buffer_t* current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
  while (!search_stage_region_profile_buffer_fits(current_buffer,archive_search_end1,archive_search_end2)) {
    // Change group
    const uint64_t last_buffer_idx = search_stage_rp->iterator.num_buffers - 1;
    if (search_stage_rp->iterator.current_buffer_idx < last_buffer_idx) {
      // Send the current group to verification
      search_stage_region_profile_buffer_send(current_buffer);
      // Next buffer
      ++(search_stage_rp->iterator.current_buffer_idx);
      current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
    } else {
      return false;
    }
  }
  // Add PE Search
  search_stage_region_profile_buffer_add(current_buffer,archive_search_end1);
  search_stage_region_profile_buffer_add(current_buffer,archive_search_end2);
  // Copy profile-partitions to the buffer
#ifdef GPU_REGION_PROFILE_ADAPTIVE
  gpu_buffer_fmi_asearch_t* const gpu_buffer_fmi_asearch = current_buffer->gpu_buffer_fmi_asearch;
  archive_search_se_stepwise_region_profile_adaptive_copy(archive_search_end1,gpu_buffer_fmi_asearch);
  archive_search_se_stepwise_region_profile_adaptive_copy(archive_search_end2,gpu_buffer_fmi_asearch);
#else
  gpu_buffer_fmi_ssearch_t* const gpu_buffer_fmi_ssearch = current_buffer->gpu_buffer_fmi_ssearch;
  archive_search_se_stepwise_region_profile_static_copy(archive_search_end1,gpu_buffer_fmi_ssearch);
  archive_search_se_stepwise_region_profile_static_copy(archive_search_end2,gpu_buffer_fmi_ssearch);
#endif
  // Return ok
  return true;
}
/*
 * Retrieve operators
 */
void search_stage_region_profile_retrieve_begin(search_stage_region_profile_t* const search_stage_rp) {
  search_stage_iterator_t* const iterator = &search_stage_rp->iterator;
  search_stage_region_profile_buffer_t* current_buffer;
  // Change mode
  search_stage_rp->search_stage_mode = search_group_buffer_phase_retrieving;
  PROF_ADD_COUNTER(GP_SEARCH_STAGE_REGION_PROFILE_BUFFERS_USED,iterator->current_buffer_idx+1);
  // Send the current buffer
  current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
  search_stage_region_profile_buffer_send(current_buffer);
  // Initialize the iterator
  iterator->current_buffer_idx = 0;
  // Reset searches iterator
  current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
  iterator->current_search_idx = 0;
  iterator->num_searches = vector_get_used(current_buffer->archive_searches);
  // Fetch first group
  search_stage_region_profile_buffer_receive(current_buffer);
}
bool search_stage_region_profile_retrieve_finished(
    search_stage_region_profile_t* const search_stage_rp) {
  // Mode Sending (Retrieval finished)
  if (search_stage_rp->search_stage_mode==search_group_buffer_phase_sending) return true;
  // Mode Retrieve (Check iterator)
  search_stage_iterator_t* const iterator = &search_stage_rp->iterator;
  return iterator->current_buffer_idx==iterator->num_buffers &&
         iterator->current_search_idx==iterator->num_searches;
}
bool search_stage_region_profile_retrieve_next(
    search_stage_region_profile_t* const search_stage_rp,
    search_stage_region_profile_buffer_t** const current_buffer,
    archive_search_t** const archive_search) {
  // Check state
  if (search_stage_rp->search_stage_mode == search_group_buffer_phase_sending) {
    search_stage_region_profile_retrieve_begin(search_stage_rp);
  }
  // Check end-of-iteration
  *current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
  search_stage_iterator_t* const iterator = &search_stage_rp->iterator;
  if (iterator->current_search_idx==iterator->num_searches) {
    // Next buffer
    ++(iterator->current_buffer_idx);
    if (iterator->current_buffer_idx==iterator->num_buffers) return false;
    // Reset searches iterator
    *current_buffer = search_stage_rp_get_current_buffer(search_stage_rp);
    iterator->current_search_idx = 0;
    iterator->num_searches = vector_get_used((*current_buffer)->archive_searches);
    if (iterator->num_searches==0) return false;
    // Receive Buffer
    search_stage_region_profile_buffer_receive(*current_buffer);
  }
  // Retrieve Search
  search_stage_region_profile_buffer_retrieve(*current_buffer,iterator->current_search_idx,archive_search);
  ++(iterator->current_search_idx); // Next
  return true;
}
/*
 * Retrieve Searches (buffered)
 */
bool search_stage_region_profile_retrieve_se_search(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_t** const archive_search) {
  // Retrieve next
  search_stage_region_profile_buffer_t* current_buffer;
  const bool success = search_stage_region_profile_retrieve_next(search_stage_rp,&current_buffer,archive_search);
  if (!success) return false;
  // Retrieve searched profile-partitions from the buffer
#ifdef GPU_REGION_PROFILE_ADAPTIVE
  gpu_buffer_fmi_asearch_t* const gpu_buffer_fmi_asearch = current_buffer->gpu_buffer_fmi_asearch;
  archive_search_se_stepwise_region_profile_adaptive_retrieve(*archive_search,gpu_buffer_fmi_asearch);
#else
  gpu_buffer_fmi_ssearch_t* const gpu_buffer_fmi_ssearch = current_buffer->gpu_buffer_fmi_ssearch;
  archive_search_se_stepwise_region_profile_static_retrieve(*archive_search,gpu_buffer_fmi_ssearch);
#endif
  // Return
  return true;
}
bool search_stage_region_profile_retrieve_pe_search(
    search_stage_region_profile_t* const search_stage_rp,
    archive_search_t** const archive_search_end1,
    archive_search_t** const archive_search_end2) {
  search_stage_region_profile_buffer_t* current_buffer;
  bool success;
  /*
   * End/1
   */
  // Retrieve (End/1)
  success = search_stage_region_profile_retrieve_next(search_stage_rp,&current_buffer,archive_search_end1);
  if (!success) return false;
  // Retrieve searched profile-partitions from the buffer (End/1)
#ifdef GPU_REGION_PROFILE_ADAPTIVE
  archive_search_se_stepwise_region_profile_adaptive_retrieve(*archive_search_end1,current_buffer->gpu_buffer_fmi_asearch);
#else
  archive_search_se_stepwise_region_profile_static_retrieve(*archive_search_end1,current_buffer->gpu_buffer_fmi_ssearch);
#endif
  /*
   * End/2
   */
  // Retrieve (End/2)
  success = search_stage_region_profile_retrieve_next(search_stage_rp,&current_buffer,archive_search_end2);
  gem_cond_fatal_error(!success,SEARCH_STAGE_RP_UNPAIRED_QUERY);
  // Retrieve searched profile-partitions from the buffer (End/2)
#ifdef GPU_REGION_PROFILE_ADAPTIVE
  archive_search_se_stepwise_region_profile_adaptive_retrieve(*archive_search_end2,current_buffer->gpu_buffer_fmi_asearch);
#else
  archive_search_se_stepwise_region_profile_static_retrieve(*archive_search_end2,current_buffer->gpu_buffer_fmi_ssearch);
#endif
  // Return ok
  return true;
}

