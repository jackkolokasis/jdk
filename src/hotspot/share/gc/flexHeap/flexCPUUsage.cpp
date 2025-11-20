#include "gc/flexHeap/flexCPUUsage.hpp"
#include "runtime/os.hpp"
#include "gc/shared/gc_globals.hpp"

#define BUFFER_SIZE 1024

double FlexCPUUsage::elapsedVTime() {
  struct rusage usage;
  int retval = getrusage(RUSAGE_THREAD, &usage);
  if (retval == 0) {
    return (double) (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + (double) (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / (1000 * 1000);
  } else {
    // better than nothing, but not much
    return os::elapsedTime();
  }
}

double FlexCPUUsage::read_thr_cpu_time(pid_t tid) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);

  FILE* file = fopen(path, "r");
  if (!file) return 0;

  char line[BUFFER_SIZE];
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return 0;
  }
  fclose(file);

  // Skip to the part after the thread name (in parentheses)
  char* stats = strrchr(line, ')');
  if (!stats || *(stats + 1) != ' ')
    return 0;
  stats += 2;  // Skip ") "

  // Parse fields starting after the comm field
  unsigned long long utime = 0, stime = 0;
  char* saveptr = nullptr;  // For strtok_r state
  for (int i = 3; i <= 15; ++i) {
    char* token = strtok_r(i == 3 ? stats : nullptr, " ", &saveptr);
    if (!token)
      return 0;

    if (i == 14)
      utime = strtoull(token, nullptr, 10);

    if (i == 15)
      stime = strtoull(token, nullptr, 10);
  }

  // Transform clock ticks into seconds
  return (utime + stime) / (double) sysconf(_SC_CLK_TCK);
}

FlexCPUUsage::FlexCPUUsage() {
  active_processor_count = os::active_processor_count();
  stw_cpu_total_time = 0;

  // Allocate memory for these data structues
  conc_gc_thread_cpu_start_time = NEW_C_HEAP_ARRAY(double, ConcGCThreads + 1, mtGC);
  conc_gc_thread_cpu_total_time = NEW_C_HEAP_ARRAY(double, ConcGCThreads + 1, mtGC);
  conc_gc_thread_ids = NEW_C_HEAP_ARRAY(pid_t, ConcGCThreads + 1, mtGC);

  refine_gc_thread_cpu_start_time = NEW_C_HEAP_ARRAY(double, G1ConcRefinementThreads, mtGC);
  refine_gc_thread_cpu_total_time = NEW_C_HEAP_ARRAY(double, G1ConcRefinementThreads, mtGC);

  // Initialize
  memset(conc_gc_thread_cpu_start_time, 0, (ConcGCThreads + 1) * sizeof(double));
  memset(conc_gc_thread_cpu_total_time, 0, (ConcGCThreads + 1) * sizeof(double));
  memset(conc_gc_thread_ids, 0, (ConcGCThreads + 1) * sizeof(pid_t));

  memset(refine_gc_thread_cpu_start_time, 0, G1ConcRefinementThreads * sizeof(double));
  memset(refine_gc_thread_cpu_total_time, 0, G1ConcRefinementThreads  * sizeof(double));

  num_mutator_threads = 0;
  interval = 0;

  last_pause_time_sec = 0;
}

FlexCPUUsage::~FlexCPUUsage() {
  FREE_C_HEAP_ARRAY(double, conc_gc_thread_cpu_start_time);
  FREE_C_HEAP_ARRAY(double, conc_gc_thread_cpu_total_time);
  FREE_C_HEAP_ARRAY(pid_t, conc_gc_thread_ids);

  FREE_C_HEAP_ARRAY(double, refine_gc_thread_cpu_start_time);
  FREE_C_HEAP_ARRAY(double, refine_gc_thread_cpu_total_time);
}

// Records the CPU time used by a concurrent refinement thread (GC worker).
// This function is called at both the start and end of a GC refinement phase.
//
// Parameters:
// - worker_id: The ID of the concurrent refinement GC worker thread.
// - is_start: If true, records the current CPU time as the thread's start time;
//             otherwise, updates the end time.
//
// The start time is reset at the beginning of the refinement phase,
// and the end time is updated on every invocation. If called with is_start=true,
// the end time is reset to zero. This time tracking enables FlexHeap to estimate
// GC CPU overhead per worker and adjust heap/page cache sizes accordingly.
void FlexCPUUsage::read_gc_conc_refine_thr_cpu_time(int worker_id, bool is_start) {
  if (is_start) {
    refine_gc_thread_cpu_start_time[worker_id] = elapsedVTime();
  }

  refine_gc_thread_cpu_total_time[worker_id] += elapsedVTime() - refine_gc_thread_cpu_start_time[worker_id];
}

void FlexCPUUsage::reset_gc_conc_refine_thr_timers() {
  memset(refine_gc_thread_cpu_start_time, 0, ParallelGCThreads * sizeof(double));
  memset(refine_gc_thread_cpu_total_time, 0, ParallelGCThreads * sizeof(double));
}

void FlexCPUUsage::read_gc_conc_thr_cpu_time(int worker_id, bool is_start) {
  if (is_start) {
    conc_gc_thread_cpu_start_time[worker_id] = elapsedVTime();
    conc_gc_thread_ids[worker_id] = os::current_thread_id();
    return;
  }

  conc_gc_thread_cpu_total_time[worker_id] += elapsedVTime() - conc_gc_thread_cpu_start_time[worker_id];
  conc_gc_thread_cpu_start_time[worker_id] = 0;
  conc_gc_thread_ids[worker_id] = 0;
}

double FlexCPUUsage::calculate_gc_cpu_time() {
  double total_gc_cpu_time = 0;
  double conc_gc_cpu_time = 0;

  for (unsigned int i = 0; i < G1ConcRefinementThreads; i++) {
    total_gc_cpu_time += refine_gc_thread_cpu_total_time[i];
    refine_gc_thread_cpu_total_time[i] = 0;
  }

  for (unsigned int i = 0; i < ConcGCThreads + 1; i++) {
    // no in progress work for the current concurrent thread
    if (conc_gc_thread_cpu_start_time[i] == 0) {
      conc_gc_cpu_time += conc_gc_thread_cpu_total_time[i];
      conc_gc_thread_cpu_total_time[i] = 0;
      continue;
    }

    // Take the total cpu time up until now and calculate the cpu time from the
    // current work that has been interrupted by STW GC
    double cur_cpu_time = read_thr_cpu_time(conc_gc_thread_ids[i]);
    conc_gc_cpu_time += conc_gc_thread_cpu_total_time[i] + (cur_cpu_time - conc_gc_thread_cpu_start_time[i]);
    conc_gc_thread_cpu_total_time[i] = 0;
    conc_gc_thread_cpu_start_time[i] = cur_cpu_time;
  }

  conc_gc_cpu_time = (num_mutator_threads + (int) ConcGCThreads > os::active_processor_count())
    ? conc_gc_cpu_time - MAX2(0.0, (os::active_processor_count() - num_mutator_threads) * interval)
    : 0;

  total_gc_cpu_time += (stw_cpu_total_time + conc_gc_cpu_time);
  stw_cpu_total_time = 0;

  return total_gc_cpu_time * 1000.0;
}

void FlexCPUUsage::register_stw_gc_ellapsed_time(bool is_pause_cleanup) {
  double elapsed_time_sec = os::elapsedTime() - stw_start_time_sec;
  last_pause_time_sec = elapsed_time_sec;

  if (is_pause_cleanup) {
    stw_cpu_total_time += (elapsed_time_sec * MIN2(num_mutator_threads, os::active_processor_count()));
    return;
  }

  stw_cpu_total_time += (elapsed_time_sec * MIN2(num_mutator_threads, os::active_processor_count()));
}
