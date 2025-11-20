/*
 * Copyright (c) 2016, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1HEAPSIZINGPOLICY_HPP
#define SHARE_GC_G1_G1HEAPSIZINGPOLICY_HPP

#include "memory/allocation.hpp"
#include "utilities/numberSeq.hpp"

class G1Analytics;
class G1CollectedHeap;

class G1HeapSizingPolicy: public CHeapObj<mtGC> {
  // MinOverThresholdForGrowth must be less than the number of recorded
  // pause times in G1Analytics, representing the minimum number of pause
  // time ratios that exceed GCTimeRatio before a heap expansion will be triggered.
  const static uint MinOverThresholdForGrowth = 4;

  const G1CollectedHeap* _g1h;
  const G1Analytics* _analytics;

  TruncatedSeq _recent_cpu_usage_deltas;
  const uint _num_prev_pauses_for_heuristics;
  // Ratio check data for determining if heap growth is necessary.
  uint _ratio_over_threshold_count;
  double _ratio_over_threshold_sum;
  uint _pauses_since_start;
  uint _long_term_count;
  int _gc_cpu_usage_deviation_counter;
  // Recent GC CPU usage deviations relative to the gc_cpu_usage_target



  // Clear GC CPU usage tracking data used by young_collection_resize_amount().
  void reset_cpu_usage_tracking_data();
  // Decay (move towards "no changes") GC CPU usage tracking data.
  void decay_cpu_usage_tracking_data();
  // Scale "full" gc pause time threshold with heap size as we want to resize more
  // eagerly at small heap sizes.
  double scale_with_heap(double pause_time_threshold);
  // Scale the cpu usage delta depending on the relative difference from the target gc_cpu_usage.
  double scale_cpu_usage_delta(double cpu_usage_delta, double min_scale_factor, double max_scale_factor) const;
  
  size_t young_collection_expand_amount(double cpu_usage_delta) const;
  size_t young_collection_shrink_amount(double cpu_usage_delta, size_t allocation_word_size) const;

  G1HeapSizingPolicy(const G1CollectedHeap* g1h, const G1Analytics* analytics);
public:

  // If an expansion would be appropriate, because recent GC overhead had
  // exceeded the desired limit, return an amount to expand by.
  size_t young_collection_expansion_amount();
  // Return by how many bytes the heap should be changed based on recent GC CPU
  // usage after young collection. If expand is set, the heap should be expanded,
  // otherwise shrunk.
  size_t young_collection_resize_amount(bool& expand, size_t allocation_word_size);

  // Returns the amount of bytes to resize the heap; if expand is set, the heap
  // should by expanded by that amount, shrunk otherwise.
  size_t full_collection_resize_amount(bool& expand);
   // Return by how many bytes the heap should be expand based on recent GC CPU
  // usage and I/O wait time after each STW GC..
  size_t flexheap_resize_amount(size_t allocation_word_size, bool should_expand);
  // Clear ratio tracking data used by expansion_amount().
  void clear_ratio_check_data();

  static G1HeapSizingPolicy* create(const G1CollectedHeap* g1h, const G1Analytics* analytics);
};

#endif // SHARE_GC_G1_G1HEAPSIZINGPOLICY_HPP
