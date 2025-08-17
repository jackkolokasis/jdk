#ifndef SHARE_GC_FLEXHEAP_FLEXHEAP_HPP
#define SHARE_GC_FLEXHEAP_FLEXHEAP_HPP

#include "gc/flexHeap/flexEnum.h"
#include "gc/flexHeap/flexCPUUsage.hpp"
#include "gc/flexHeap/flexStateMachine.hpp"
#include "memory/allocation.hpp"
#include <stdlib.h>
#include <string.h>

#define FH_HIST_SIZE 10
#define FH_GC_HIST_SIZE 10
#define FH_NUM_ACTIONS 6
#define FH_NUM_STATES 4
#define FH_NAME_LEN 20

class FlexHeap : public CHeapObj<mtInternal> {
private:

  char state_name[FH_NUM_STATES][FH_NAME_LEN]; //< Define state names
  char action_name[FH_NUM_ACTIONS][FH_NAME_LEN]; //< Define state names

  fh_actions cur_action;              //< Current action

  fh_actions prev_action;             //< Previous action

  fh_states cur_state;                //< Current state

  double hist_gc_time[FH_GC_HIST_SIZE];  //< History of the gc time in
  // previous intervals
  double hist_iowait_time[FH_HIST_SIZE]; //< History of the iowait time in
                                          // previous intervals

  double start_interval_sec;          //< Start time of the interval
  double iowait_time_ms;           //< iowait time

  double window_interval_ms;

  FlexStateMachine *state_machine;    //< FSM
  FlexCPUUsage *cpu_usage;            // Cpu utilization for

  // Intitilize the policy of the state machine.
  FlexStateMachine* init_state_machine_policy();

  // Initialize the array of state names
  void init_state_actions_names();

  // Print states (for debugging and logging purposes)
  void print_state_action(double avg_gc_time_ms, double avg_io_time_ms);

  // Find the average of the array elements
  double calc_avg_time(double *arr, int size);

  void resize_heap(size_t allocation_word_size, bool should_grow, bool is_remark_phase);

  // Set current time since last window
  void record_stw_exit();

public:
  // Constructor
  FlexHeap();

  // Destructor
  ~FlexHeap();

  FlexCPUUsage *get_cpu_usage() { return cpu_usage; }

  void record_stw_entry();

  void record_mutator_thread_id(pid_t tid);

  void dram_repartition(size_t allocation_word_size, bool is_remark_phase = false);
};

#endif // SHARE_GC_FLEXHEAP_FLEXHEAP_HPP
