#include "gc/flexHeap/flexStateMachine.hpp"
#include "gc/g1/g1HeapSizingPolicy.hpp"
#include "gc/g1/g1IHOPControl.hpp"
#include "gc/g1/g1Policy.hpp"
#include <math.h>

#define BUFFER_SIZE 1024
#define EPSILON 50

// Read the memory statistics for the cgroup
size_t FlexStateMachine::read_cgroup_mem_stats(bool read_page_cache) {
  static int is_v2 = -1;  // Static variable to cache cgroup version detection
  if (is_v2 == -1) {
    struct stat buffer;
    is_v2 = (stat("/sys/fs/cgroup/cgroup.controllers", &buffer) == 0);
  }

  // Determine memory.stat path
  const char* file_path = is_v2 ? "/sys/fs/cgroup/memlim/memory.stat" : "/sys/fs/cgroup/memory/memlim/memory.stat";

  // Open the file for reading
  FILE* file = fopen(file_path, "r");

  if (file == NULL) {
    fprintf(stderr, "Failed to open memory.stat\n");
    return 0;
  }

  char line[BUFFER_SIZE];
  size_t res = 0;
  const char* search_key = read_page_cache ? (is_v2 ? "file" : "cache") : (is_v2 ? "anon" : "rss");

  // Read file and find the required value
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, search_key, strlen(search_key)) == 0) { // Match the key at start
      res = atoll(line + strlen(search_key) + 1); // Extract the value
      break;
    }
  }

  // Close the file
  fclose(file);
  return res;
}

void LostComputeCyclesPolicy::fsm(fh_states *cur_state,
                                  fh_actions *cur_action,
                                  double gc_time_ms,
                                  double io_time_ms,
                                  double last_pause_time) {
  switch (*cur_state) {
    case FHS_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms, last_pause_time);
      break;
    case FHS_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, gc_time_ms, io_time_ms, last_pause_time);
      break;
    case FHS_NO_ACTION:
      state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms, last_pause_time);
      break;
    default:
      break;
  }
}

bool LostComputeCyclesPolicy::analyze_gc_io_behavior(double gc_time_ms, double io_time_ms, fh_states *cur_state) {
  relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;

  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  size_t marking_initiating_used_threshold = g1h->policy()->get_ihop_control()->get_conc_mark_start_threshold();
  size_t cur_used_bytes = g1h->non_young_capacity_bytes();

  is_ihop_low = marking_initiating_used_threshold == 0 ? false : ((double) cur_used_bytes / marking_initiating_used_threshold < 1.0);

  double gc_diff_ratio = (prev_gc_time == 0) ? 1 : (gc_time_ms - prev_gc_time) / prev_gc_time;
  double io_diff_ratio = (prev_io_time == 0) ? 1 : (io_time_ms - prev_io_time) / prev_io_time;

  prev_gc_time = gc_time_ms;
  prev_io_time = io_time_ms;

  bool overhead_increase = false;

  if (gc_diff_ratio < 0 && io_diff_ratio < 0) {
    overhead_increase = false;
  } else {
    overhead_increase = (*cur_state == FHS_WAIT_SHRINK)
      ? (gc_diff_ratio > io_diff_ratio)
      : (io_diff_ratio > gc_diff_ratio);
  }

  return overhead_increase;
}

void LostComputeCyclesPolicy::state_no_action(fh_states *cur_state,
                                              fh_actions *cur_action,
                                              double gc_time_ms,
                                              double io_time_ms,
                                              double last_pause_time) {

  double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  prev_gc_time = gc_time_ms;
  prev_io_time = io_time_ms;

  if (relative_diff <= 0.05) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_NO_ACTION;
    delay_before_action = gc_time_ms + io_time_ms;
    return;
  }

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (is_delay_decreased) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_NO_ACTION;
    return;
  }

  bool under_h1_max_limit = g1h->capacity() < g1h->max_capacity();
  if (last_action == FH_SHRINK_HEAP && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }

  if (last_action == FH_GROW_HEAP) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void LostComputeCyclesPolicy::state_wait_after_grow(fh_states *cur_state,
                                                    fh_actions *cur_action,
                                                    double gc_time_ms,
                                                    double io_time_ms,
                                                    double last_pause_time) {

  // double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;
  // G1CollectedHeap* g1h = G1CollectedHeap::heap();
  // size_t marking_initiating_used_threshold = g1h->policy()->get_ihop_control()->get_conc_mark_start_threshold();
  // size_t cur_used_bytes = g1h->non_young_capacity_bytes();
  // bool is_ihop_low = marking_initiating_used_threshold == 0 ? false : ((double) cur_used_bytes / marking_initiating_used_threshold < 1.0);

  // double gc_diff_ratio = (prev_gc_time == 0) ? 1 : (gc_time_ms - prev_gc_time) / prev_gc_time;
  // double io_diff_ratio = (prev_io_time == 0) ? 1 : (io_time_ms - prev_io_time) / prev_io_time;
  // bool io_overhead_increase = (gc_diff_ratio < 0 && io_diff_ratio < 0) ? false : io_diff_ratio > gc_diff_ratio;

  // prev_gc_time = gc_time_ms;
  // prev_io_time = io_time_ms;
  // static int stagnant_counter = 0;
  bool io_overhead_increase = analyze_gc_io_behavior(gc_time_ms, io_time_ms, cur_state);
  bool no_free_regions = (double)(g1h->num_free_regions() * G1HeapRegion::GrainBytes) / g1h->capacity() <= 0.05;

  if (relative_diff <= 0.05) {
    // stagnant_counter++;
    // bool under_stagnant_limit = stagnant_counter < 10;
    // *cur_state = under_stagnant_limit ? FHS_WAIT_GROW : FHS_WAIT_SHRINK;
    // *cur_action = under_stagnant_limit ? FH_NO_ACTION : FH_SHRINK_HEAP;
    // stagnant_counter = under_stagnant_limit ? stagnant_counter : 0;

    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_NO_ACTION;
    delay_before_action = gc_time_ms + io_time_ms;
    return;
  }
  // reset stagnat counter
  // stagnant_counter = 0;

  bool overall_overhead_increase = (gc_time_ms + io_time_ms) > delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (overall_overhead_increase && io_overhead_increase) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  *cur_state = FHS_WAIT_GROW;
  bool under_max_capacity = g1h->capacity() < g1h->max_capacity();
  *cur_action = (no_free_regions && under_max_capacity) ? FH_GROW_HEAP : FH_WAIT_AFTER_GROW;
  last_action = *cur_action;
}

void LostComputeCyclesPolicy::state_wait_after_shrink(fh_states *cur_state,
                                                      fh_actions *cur_action,
                                                      double gc_time_ms,
                                                      double io_time_ms,
                                                      double last_pause_time) {

  // double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;
  // G1CollectedHeap* g1h = G1CollectedHeap::heap();

  // size_t marking_initiating_used_threshold = g1h->policy()->get_ihop_control()->get_conc_mark_start_threshold();
  // size_t cur_used_bytes = g1h->non_young_capacity_bytes();

  // bool is_ihop_low = marking_initiating_used_threshold == 0 ? false : ((double) cur_used_bytes / marking_initiating_used_threshold < 1.0);

  // double gc_diff_ratio = (prev_gc_time == 0) ? 1 : (gc_time_ms - prev_gc_time) / prev_gc_time;
  // double io_diff_ratio = (prev_io_time == 0) ? 1 : (io_time_ms - prev_io_time) / prev_io_time;
  // bool gc_overhead_increase = (gc_diff_ratio < 0 && io_diff_ratio < 0) ? false : gc_diff_ratio > io_diff_ratio;

  // prev_gc_time = gc_time_ms;
  // prev_io_time = io_time_ms;
  // static int stagnant_counter = 0;
  bool gc_overhead_increase = analyze_gc_io_behavior(gc_time_ms, io_time_ms, cur_state);
  bool no_free_regions = (double)(g1h->num_free_regions() * G1HeapRegion::GrainBytes) / g1h->capacity() <= 0.05;

  if (relative_diff <= 0.05) {
    // stagnant_counter++;
    // bool under_stagnant_limit = stagnant_counter < 10;
    // *cur_state = under_stagnant_limit ? FHS_WAIT_SHRINK : FHS_WAIT_GROW;
    // *cur_action = under_stagnant_limit ? FH_NO_ACTION : FH_GROW_HEAP;
    // stagnant_counter = under_stagnant_limit ? stagnant_counter : 0;
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_NO_ACTION;
    delay_before_action = gc_time_ms + io_time_ms;
    return;
  }

  // reset stagnat counter
  // stagnant_counter = 0;

  bool overall_overhead_increase = (gc_time_ms + io_time_ms) > delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (overall_overhead_increase && gc_overhead_increase) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }

  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));

  *cur_state = FHS_WAIT_SHRINK;
  *cur_action = ioslack ? FH_IOSLACK : FH_SHRINK_HEAP;
  last_action = *cur_action;
}
