#ifndef SHARE_GC_FLEXHEAP_FLEXCPUUSAGE_HPP
#define SHARE_GC_FLEXHEAP_FLEXCPUUSAGE_HPP

#include "memory/allocation.hpp"
#include "utilities/globalDefinitions.hpp"
#include "runtime/os.hpp"
#include <sys/resource.h>

#define STAT_START true
#define STAT_END false

class FlexCPUUsage : public CHeapObj<mtInternal> {
  int active_processor_count;
  int num_mutator_threads;

  double stw_cpu_total_time;
  double stw_start_time_sec;
  double interval;
  double last_pause_time_sec;

  double *conc_gc_thread_cpu_start_time;
  double *conc_gc_thread_cpu_total_time;
  pid_t  *conc_gc_thread_ids;

  double *refine_gc_thread_cpu_start_time;
  double *refine_gc_thread_cpu_total_time;

  double read_thr_cpu_time(pid_t tid);
  // The "virtual time" of a thread is the amount of time a thread has
  // actually run.
  double elapsedVTime();

public:
  // done
  FlexCPUUsage();
  ~FlexCPUUsage();
  void read_gc_conc_refine_thr_cpu_time(int worker_id, bool is_start);
  void reset_gc_conc_refine_thr_timers();

  void read_gc_conc_thr_cpu_time(int tid, bool is_start);

  void set_stw_gc_start_time() {stw_start_time_sec = os::elapsedTime(); }
  void register_stw_gc_ellapsed_time(bool is_pause_cleanup = false);

  double calculate_gc_cpu_time();
  double get_last_pause_time() { return last_pause_time_sec * 1000.0; }

  void incr_mutator_threads() { num_mutator_threads++; }
  void set_inteval(double duration) { interval = duration; }
};

#endif // SHARE_GC_FLEXHEAP_FLEXCPUUSAGE_HPP
