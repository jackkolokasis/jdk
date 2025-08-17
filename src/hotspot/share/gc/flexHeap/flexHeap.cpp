#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/flexHeap/flexHeap.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/universe.hpp"
#include <ioWait.hpp>

#define BUFFER_SIZE 1024
#define REGULAR_INTERVAL ((2LL * 1000))
#define GROWTH_FACTOR 1.2
#define SHRINK_FACTOR 0.8

FlexHeap::FlexHeap() {
  cpu_usage = new FlexCPUUsage();

  cur_action = FH_NO_ACTION;
  prev_action = FH_NO_ACTION;
  cur_state = FHS_NO_ACTION;

  memset(hist_gc_time, 0, FH_GC_HIST_SIZE * sizeof(double));
  memset(hist_iowait_time, 0, FH_HIST_SIZE * sizeof(double));

  init_state_actions_names();
  state_machine = init_state_machine_policy();

  iowait_time_ms = 0;
  start_interval_sec = os::elapsedTime();

  ebpf_start();
}

FlexHeap::~FlexHeap() {
  ebpf_stop();
  delete(cpu_usage);
  delete(state_machine);
}

// Initialize the policy of the state machine
FlexStateMachine* FlexHeap::init_state_machine_policy() {
  // if (strcmp(FlexResizingPolicy, "optimal_state") == 0) {
  //   // Experimental policy used for the paper:
  //   // Evaluates the effect of getting trapped in a local minimum
  //   // versus continuously searching for better configurations,
  //   // as occurs in the FlexSimpleWaitStateMachineOnlyDelay() policy.
  //   frpintf
  //   return new FlexSimpleWaitStateMachineOnlyDelayWithOptimalState();
  // }

  return new LostComputeCyclesPolicy();
}

// Initialize the array of state names
void FlexHeap::init_state_actions_names() {
  strncpy(action_name[0], "NO_ACTION",       10);
  strncpy(action_name[1], "SHRINK_HEAP",     12);
  strncpy(action_name[2], "GROW_HEAP",       10);
  strncpy(action_name[3], "CONTINUE",         9);
  strncpy(action_name[4], "IOSLACK",          8);
  strncpy(action_name[5], "WAIT_AFTER_GROW", 16);

  strncpy(state_name[0], "S_NO_ACTION",      12);
  strncpy(state_name[1], "S_WAIT_SHRINK",    14);
  strncpy(state_name[2], "S_WAIT_GROW",      12);
  strncpy(state_name[3], "S_STABLE",         9);
}

// Print states (for debugging and logging purposes)
void FlexHeap::print_state_action(double avg_gc_time_ms, double avg_io_time_ms) {

  tty->stamp(true);
  tty->print(",%lf", avg_gc_time_ms);
  tty->print(",%lf", avg_io_time_ms);
  tty->print(",%s", state_name[cur_state]);
  tty->print(",%s", action_name[cur_action]);
}

// Find the average of the array elements
double FlexHeap::calc_avg_time(double *arr, int size) {
  double sum = 0;

  for (int i = 0; i < size; i++) {
    sum += arr[i];
  }

  return (double) sum / size;
}

void FlexHeap::resize_heap(size_t allocation_word_size, bool should_grow, bool is_remark_phase) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  g1h->flexheap_resize_heap(allocation_word_size, cur_action);

  if (should_grow && is_remark_phase) {
    g1h->uncommit_regions_if_necessary();
  }
}

void FlexHeap::dram_repartition(size_t allocation_word_size, bool is_remark_phase) {
  double avg_gc_time_ms, avg_io_time_ms;
  static int index = 0;

  hist_gc_time[index % FH_GC_HIST_SIZE] = cpu_usage->calculate_gc_cpu_time();
  hist_iowait_time[index % FH_HIST_SIZE] = iowait_time_ms;
  index++;

  avg_io_time_ms = calc_avg_time(hist_iowait_time, FH_HIST_SIZE);
  avg_gc_time_ms = calc_avg_time(hist_gc_time, FH_GC_HIST_SIZE);

  state_machine->fsm(&cur_state, &cur_action, avg_gc_time_ms, avg_io_time_ms, cpu_usage->get_last_pause_time());
// #ifdef DEBUG_PRINTS_FLEXHEAP
  // print_state_action(avg_gc_time_ms, avg_io_time_ms);
// #endif

  switch (cur_action) {
    case FH_SHRINK_HEAP:
      resize_heap(allocation_word_size, false /* shrink heap */, is_remark_phase);
      break;
    case FH_GROW_HEAP:
      resize_heap(allocation_word_size, true /* grow heap */, is_remark_phase);
      break;
    case FH_NO_ACTION:
    case FH_IOSLACK:
    case FH_WAIT_AFTER_GROW:
    case FH_CONTINUE:
// #ifdef DEBUG_PRINTS_FLEXHEAP
    // tty->print("\n");
    // tty->flush();
// #endif
      break;
  }
  prev_action = cur_action;
  record_stw_exit();
}

void FlexHeap::record_stw_exit() {
  start_interval_sec = os::elapsedTime();
  ebpf_enable_tracking();
}

void FlexHeap::record_stw_entry() {
  double interval = (os::elapsedTime() - start_interval_sec);
  window_interval_ms = interval * 1000.0;
  cpu_usage->set_inteval(interval);
  cpu_usage->set_stw_gc_start_time();
  iowait_time_ms = MIN2(ebpf_disable_tracking(), interval * 1000.0 * os::active_processor_count());
}

void FlexHeap::record_mutator_thread_id(pid_t tid) {
  cpu_usage->incr_mutator_threads();
  ebpf_add_tid(tid);
}
