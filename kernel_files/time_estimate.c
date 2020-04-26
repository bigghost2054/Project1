#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

asmlinkage void sys_time_estimate(int start_flag, int pid, struct timespec* start_time){
	struct timespec cur_time;
	if (start_flag == 1){
		//Only get current time (for program start)
		getnstimeofday(start_time);
		return;
	}else{
		//Use start_time as start time and then get current time and then print them
		getnstimeofday(&cur_time);
		printk("[Project1] %d %lu.%09lu %lu.%09lu", pid, start_time->tv_sec, start_time->tv_nsec, cur_time.tv_sec, cur_time.tv_nsec);
		return;
	}
}
