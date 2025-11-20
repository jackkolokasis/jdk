#ifndef SHARE_GC_FLEXHEAP_FLEXSTATEMACHINE_HPP
#define SHARE_GC_FLEXHEAP_FLEXSTATEMACHINE_HPP

#include "gc/flexHeap/flexEnum.h"
#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/allocation.hpp"

class FlexStateMachine : public CHeapObj<mtInternal> {

public:
  virtual void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
                   double io_time_ms, double last_pause_time = 0) = 0;

  virtual void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                                     double gc_time_ms, double io_time_ms, double last_pause_time = 0) = 0;

  virtual void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                                       double gc_time_ms, double io_time_ms, double last_pause_time = 0) = 0;

  virtual void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms, double last_pause_time = 0) = 0;

  virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
                            double gc_time_ms, double io_time_ms, double last_pause_time = 0) = 0;

  virtual double get_cpu_usage_delta() = 0;

  // Read the memory statistics for the cgroup
  size_t read_cgroup_mem_stats(bool read_page_cache);
};

class LostComputeCyclesPolicy : public FlexStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  double prev_gc_time = 1;
  double prev_io_time = 1;
  double gc_diff_ratio;
  double io_diff_ratio; 
  double relative_diff;
  bool is_ihop_low;
  fh_actions last_action = FH_GROW_HEAP;
  G1CollectedHeap* g1h;

  bool analyze_gc_io_behavior(double gc_time_ms, double io_time_ms, fh_states *cur_state);

public:
  LostComputeCyclesPolicy() {
    tty->print_cr("Resizing Policy = LostComputeCyclesPolicy()\n");
    tty->flush();
    g1h = G1CollectedHeap::heap();
  }

  void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
           double io_time_ms, double last_pause_time = 0);

  void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                       double gc_time_ms, double io_time_ms, double last_pause_time = 0);

  void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                             double gc_time_ms, double io_time_ms, double last_pause_time = 0);

  void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms, double last_pause_time = 0);

  void state_stable(fh_states *cur_state, fh_actions *cur_action,
                    double gc_time_ms, double io_time_ms, double last_pause_time = 0) {
    return;
  }
  
  double get_cpu_usage_delta();
};

#endif

