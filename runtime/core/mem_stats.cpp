#include "ar_mem_stats.h"
#if defined(__APPLE__)
#include <mach/mach.h>
double ar_phys_footprint_mb(void) {
    task_vm_info_data_t info{};
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) != KERN_SUCCESS) return 0.0;
    return double(info.phys_footprint) / (1024.0 * 1024.0);
}
#else
double ar_phys_footprint_mb(void) { return 0.0; }
#endif
