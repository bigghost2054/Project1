#define _GNU_SOURCE
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>


#include <sched.h>
#include <time.h>
#include <sys/wait.h>
#define PARENT_CPU	0
#define CHILD_CPU	1


#define DEBUG

#define POLICY_FIFO	0
#define POLICY_RR	1
#define POLICY_SJF	2
#define POLICY_PSJF	3

#define PSTATE_WAITING	0
#define PSTATE_READY	1
#define PSTATE_RUNNING	2
#define PSTATE_PAUSED	3
#define PSTATE_FINISHED	4

#define RR_TIME		500

typedef struct process{
	int state;
	char name[33];
	int pid;
	int ready_time; //X
	int execution_time; //Y
	int time_to_ready; //max(X - current_time, 0)
	int time_to_finish_execution; //max(Y - time_executed, 0)
	int request_cpu_time; //For RR Only
}process;

int do_scheduling(int policy_code, int num_processes, process* processes);
int find_next_event_time(int policy_code, int* finish_flag, int num_processes, process* processes, int time_to_next_round);
int find_next_ready_time(int num_processes, process* processes);
int find_next_finish_time(int num_processes, process* processes);

void update_clock_for_all_processes(int time_to_next_event, int num_processes, process* processes, int* running_process_idx, int* last_finished_process_idx, int cur_time);
void check_and_run_processes(int policy_code, int* running_process_idx, int last_finished_process_idx, int num_processes, process* processes, int cur_time); 


int wait_process(int unit);

int run_process(process* process_to_start, int cur_time, int unit);
int preempt_process(process* process_to_preempt, int cur_time);
int finish_process(process* process_to_finish, int cur_time);


int main(int argc, char** argv){
	const char STR_FIFO[] = "FIFO";
	const char STR_RR[] = "RR";
	const char STR_SJF[] = "SJF";
	const char STR_PSJF[] = "PSJF";

	char str_policy[5];
	
	int policy_code;
	int num_processes;

	process* processes;

	int i;

	//Read the scheduling policy
	scanf("%4s", str_policy);
	//Convert the str into policy code (for convinient)
	if (strcmp(str_policy, STR_FIFO) == 0){
		policy_code = POLICY_FIFO;
	}else if(strcmp(str_policy, STR_RR) == 0){
		policy_code = POLICY_RR;
	}else if (strcmp(str_policy, STR_SJF) == 0){
		policy_code = POLICY_SJF;
	}else if (strcmp(str_policy, STR_PSJF) == 0){
		policy_code = POLICY_PSJF;
	}

	//Read #Processes
	scanf("%d",&num_processes);

	//Allocate the memory for processes structure
	processes = (process*)malloc(sizeof(process)*num_processes);

	//Read each process information
	for (i = 0; i < num_processes; i++){
		scanf("%32s",processes[i].name);			//Read name of the process
		scanf("%d", &(processes[i].ready_time));		//Read ready time
		scanf("%d", &(processes[i].execution_time));		//Read execution_time
		//Initialize the state of the process
		processes[i].state = PSTATE_WAITING;

		//Initialize time to ready and time to finish execution
		processes[i].time_to_ready = processes[i].ready_time;
		processes[i].time_to_finish_execution = processes[i].execution_time;

		//
		processes[i].request_cpu_time = processes[i].ready_time;
		
		//printf("%s\n",processes[i].name);
	}

	//Start Scheduling
	do_scheduling(policy_code, num_processes, processes);


	//Free allocated memory
	free(processes);

	//Clean dmesg buffer
	system("echo  | sudo tee /dev/kmsg");
	return 0;
}

int do_scheduling(int policy_code, int num_processes, process* processes){
	int cur_time = 0;				//in unit defined in project 1
	int time_to_next_ready;
	int time_to_next_finish;
	int time_to_next_round = RR_TIME;		//For RR MODE ONLY

	int time_to_next_event;

	int running_process_idx = -1;
	int last_finished_process_idx = -1;
	int finish_flag = 0;


	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(PARENT_CPU, &mask);
	sched_setaffinity(getpid(), sizeof(mask), &mask);


	while(1){
		
		time_to_next_event = find_next_event_time(policy_code, &finish_flag, num_processes, processes, time_to_next_round);
		//printf("next time, finish_flag = %d, %d\n", time_to_next_event, finish_flag);
		if (finish_flag == 1)break; //END SIMULATION
		//Wait for that time
		wait_process(time_to_next_event);
		//Update cur_time and all processes
		cur_time += time_to_next_event;
		if (policy_code == POLICY_RR){
			time_to_next_round -= time_to_next_event;
			if (time_to_next_round <= 0){
				time_to_next_round = RR_TIME;
			}
		}
		//Update the clock for each proceses
		update_clock_for_all_processes(time_to_next_event, num_processes, processes, &running_process_idx, &last_finished_process_idx, cur_time);
		

		//Find the good-to-run task (paused or ready) with highest priority
		//Run it, and preempt the task if necessary
		//NOTE: There may be no good-to-run task (e.g. Someone finished, but no one has been ready yet)
		check_and_run_processes(policy_code, &running_process_idx, last_finished_process_idx, num_processes, processes, cur_time); 
		



	}
	return 0;
}


int find_next_event_time(int policy_code, int* finish_flag, int num_processes, process* processes, int time_to_next_round){
	//Find next event time
	int time_to_next_ready = find_next_ready_time(num_processes, processes);
	int time_to_next_finish = find_next_finish_time(num_processes, processes);
	int time_to_next_event;
	//Find the next event time
	switch(policy_code){
		case POLICY_FIFO:
			time_to_next_event = -1;
			for (int i = 0; i < num_processes; i++){
				if (processes[i].state == PSTATE_RUNNING){
					time_to_next_event = processes[i].time_to_finish_execution;
					break;
				}else if(processes[i].state == PSTATE_WAITING){
					time_to_next_event = processes[i].time_to_ready;
					break;
				}else if(processes[i].state == PSTATE_READY){
					time_to_next_event = 0;
					break;
				}
			}
			if (time_to_next_event == -1) *finish_flag = 1;
			break;

		case POLICY_RR:
			if (time_to_next_finish >= 0){
				//There is a running processes
				if (time_to_next_finish < time_to_next_round){
					time_to_next_event = time_to_next_finish;
				}else{
					time_to_next_event = time_to_next_round;
				}
			}else{
				//No Running processes
				if (time_to_next_ready >= 0){
					//There is a potential ready program
					time_to_next_event = time_to_next_ready;
				}else{
					//END RUNNING
					*finish_flag = 1;
				}
			}
			break;

		case POLICY_SJF:
			if (time_to_next_finish >= 0){
				//There is running process ==> No Preemptive so you have to wait
				time_to_next_event = time_to_next_finish;
			}else{
				//No Running Processes
				if (time_to_next_ready >= 0){
					time_to_next_event = time_to_next_ready;
				}else{
					*finish_flag = 1;
				}
			}
			break;

		case POLICY_PSJF:
			if (time_to_next_finish >= 0){
				//There is a running process
				if (time_to_next_ready >= 0){
					if (time_to_next_ready < time_to_next_finish){
						//Allow the scheduler to detect the new ready process
						time_to_next_event = time_to_next_ready;
					}else{
						time_to_next_event = time_to_next_finish;
					}
				}else{
					time_to_next_event = time_to_next_finish;
				}
			}else if (time_to_next_ready >= 0){
				time_to_next_event = time_to_next_ready;
			}else{
				*finish_flag = 1;
			}
			break;

	}
	//printf("time_to_next_event = %d\n", time_to_next_event);
	return time_to_next_event;
}

int find_next_ready_time(int num_processes, process* processes){
	//For all scheduling policy
	int next_time_to_ready = -1; //Set to None in Python
	for (int i = 0; i < num_processes; i++){
		if (processes[i].state == PSTATE_WAITING){
			if ((next_time_to_ready == -1) || (processes[i].time_to_ready < next_time_to_ready)){
				next_time_to_ready = processes[i].time_to_ready;
			}
		}
	}
	return next_time_to_ready;
}

int find_next_finish_time(int num_processes, process* processes){
	//For all scheduling policy
	int next_finish_time = -1;
	for (int i = 0; i < num_processes; i++){
		if (processes[i].state == PSTATE_RUNNING){
			if ((next_finish_time == -1) || (processes[i].time_to_finish_execution < next_finish_time)){
				next_finish_time = processes[i].time_to_finish_execution;
			}
		}
	}
	return next_finish_time;
}

int wait_process(int unit){
	for (int j = 0; j < unit; j++){
		{volatile unsigned long i; for (i = 0; i < 1000000UL; i++);}
	}
	return 0;
}


void update_clock_for_all_processes(int time_to_next_event, int num_processes, process* processes, int* running_process_idx, int* last_finished_process_idx, int cur_time){
	for (int i = 0; i < num_processes; i++){
		switch (processes[i].state){
			case PSTATE_WAITING:
				processes[i].time_to_ready -= time_to_next_event;
				if (processes[i].time_to_ready <= 0){//May Less than 0 (if FIFO, RR or SJF)
					processes[i].state = PSTATE_READY;
					processes[i].time_to_ready = 0;
					//printf("TIME TO READY RESET!!\n");
				}
				break;
			case PSTATE_READY:
				break;
			case PSTATE_RUNNING:
				processes[i].time_to_finish_execution -= time_to_next_event;
				if (processes[i].time_to_finish_execution == 0){
					//TODO: WAIT CHILD PROCESS to End
					finish_process(&(processes[i]), cur_time);
					processes[i].state = PSTATE_FINISHED;
					*running_process_idx = -1;
					*last_finished_process_idx = i;
				}
				break;
			case PSTATE_PAUSED:
				break;
			case PSTATE_FINISHED:
				break;

		}
	}
}

void check_and_run_processes(int policy_code, int* running_process_idx, int last_finished_process_idx, int num_processes, process* processes, int cur_time){
	//FOR RR
	int idx_first_process_ready_to_run;
	int idx_last_process_ready_to_run;
	//FOR PSJF or SJF
	int idx_shortest_process;
	int shortest_time;

	//FOR RR Only
	int found_next_process_to_run; //FOR RR ONLY
	int cur_shortest_request_time;
	int cur_process_to_run;

	switch(policy_code){
		case POLICY_FIFO:
			for (int i = 0; i < num_processes; i++){
				if (processes[i].state == PSTATE_WAITING){
					//WAIT FOR PROCESS i
					break;
				}else if (processes[i].state == PSTATE_READY){
					//RUN PROCESS i
					run_process(&(processes[i]), cur_time, processes[i].execution_time);
					
					processes[i].state = PSTATE_RUNNING;
					*running_process_idx = i;
					break;
				}else if (processes[i].state == PSTATE_FINISHED){
					//SKIP IT
					continue;
				}else{
					//SHOULD NOT HAPPEN
				}
			}
			break;

		case POLICY_RR:
			found_next_process_to_run = 0;
			idx_first_process_ready_to_run = -1;
			idx_last_process_ready_to_run = -1;

			//Find the next process to run
			cur_shortest_request_time = -1;
			cur_process_to_run = -1;
			for (int i = 0; i < num_processes; i++){
				if ((processes[i].state == PSTATE_READY) || (processes[i].state == PSTATE_PAUSED)){
					if ((cur_shortest_request_time == -1) || (cur_shortest_request_time > processes[i].request_cpu_time)){
						found_next_process_to_run = 1;
						cur_shortest_request_time = processes[i].request_cpu_time;
						cur_process_to_run = i;
					}
				}
			}

			if (*running_process_idx == -1){
				if (cur_process_to_run != -1){
					//Run process [cur_process_to_run] (the first process ready to run)
					run_process(&(processes[cur_process_to_run]), cur_time, processes[cur_process_to_run].execution_time);
								
					processes[cur_process_to_run].state = PSTATE_RUNNING;
					*running_process_idx = cur_process_to_run;
					break;
				}
			}else{
				if (cur_process_to_run != -1){
					//You found something else process to run
					//PAUSE process[*running_process_idx]
					preempt_process(&(processes[*running_process_idx]), cur_time);
					processes[*running_process_idx].state = PSTATE_PAUSED;
					//Request CPU next round
					processes[*running_process_idx].request_cpu_time = cur_time;
					
					//Run process [cur_process_to_run]
					run_process(&(processes[cur_process_to_run]), cur_time, processes[cur_process_to_run].execution_time);
					processes[cur_process_to_run].state = PSTATE_RUNNING;
					*running_process_idx = cur_process_to_run;
					break;
				}else{
					//You cannot find something else process to run ==> keep this process running
				}
			}

			/*
			for (int i = 0; i < num_processes; i++){
				if ((processes[i].state == PSTATE_READY) || (processes[i].state == PSTATE_PAUSED)){
					if (idx_first_process_ready_to_run == -1){
						idx_first_process_ready_to_run = i;
					}
					idx_last_process_ready_to_run = i;

					if (*running_process_idx == -1){
						if ((last_finished_process_idx == -1) || (i > last_finished_process_idx)){
							//Run process i (the first process ready to run)
							run_process(&(processes[i]), cur_time, processes[i].execution_time);
							
							processes[i].state = PSTATE_RUNNING;
							*running_process_idx = i;
							found_next_process_to_run = 1;
							break;
						}else{
						}
						
					}else if (i > *running_process_idx){
						//PAUSE process[*running_process_idx]
						preempt_process(&(processes[*running_process_idx]), cur_time);
						processes[*running_process_idx].state = PSTATE_PAUSED;
						//Run process i
						run_process(&(processes[i]), cur_time, processes[i].execution_time);
						processes[i].state = PSTATE_RUNNING;
						*running_process_idx = i;
						found_next_process_to_run = 1;
						break;
					}
				}
			}

			if ((found_next_process_to_run == 0) && ((idx_last_process_ready_to_run <= *running_process_idx) || (idx_last_process_ready_to_run <= last_finished_process_idx)) && (idx_first_process_ready_to_run >= 0)){
				if (idx_first_process_ready_to_run != *running_process_idx){
					if (*running_process_idx >= 0){
						//PAUSE process[*running_process_idx]
						preempt_process(&(processes[*running_process_idx]), cur_time);
						processes[*running_process_idx].state = PSTATE_PAUSED;
					}
					//RUN process[idx_first_process_ready_to_run]
					run_process(&(processes[idx_first_process_ready_to_run]), cur_time, processes[idx_first_process_ready_to_run].execution_time);
					processes[idx_first_process_ready_to_run].state = PSTATE_RUNNING;
					*running_process_idx = idx_first_process_ready_to_run;
				}else{
					//If new process is the same as the process for preempting (If only one process need to execute) --> SKIP, and let it go
				}
				
			}*/
			break;

		case POLICY_SJF:
			idx_shortest_process = -1;
			shortest_time = -1;
			for (int i = 0; i < num_processes; i++){
				if (processes[i].state == PSTATE_READY){
					//SHOULD NOT HAVE PAUSED STATE (THIS IS NOT PREEMTIVE MODE)
					if ((idx_shortest_process == -1) || (shortest_time > processes[i].execution_time)){
						idx_shortest_process = i;
						shortest_time = processes[i].execution_time;
					}
				}
			}
			//RUN process[idx_shortest_process]
			if (idx_shortest_process >= 0){
				run_process(&(processes[idx_shortest_process]), cur_time, processes[idx_shortest_process].execution_time);
				processes[idx_shortest_process].state = PSTATE_RUNNING;
				*running_process_idx = idx_shortest_process;
			}
			break;

		case POLICY_PSJF:
			idx_shortest_process = -1;
			shortest_time = -1;
			for (int i = 0; i < num_processes; i++){
				if ((processes[i].state == PSTATE_READY) || (processes[i].state == PSTATE_PAUSED) || (processes[i].state == PSTATE_RUNNING)){
					if ((idx_shortest_process == -1) || (shortest_time > processes[i].execution_time)){
						idx_shortest_process = i;
						shortest_time = processes[i].execution_time;
					}
				}
			}
			if (*running_process_idx != idx_shortest_process){
				//PAUSED process[*running_process_idx] if necessary
				if (*running_process_idx >= 0){
					preempt_process(&(processes[*running_process_idx]), cur_time);
					processes[*running_process_idx].state = PSTATE_PAUSED;
					*running_process_idx = -1;
				}
				//RUN process[idx_shortest_process]
				if (idx_shortest_process >= 0){
					run_process(&(processes[idx_shortest_process]), cur_time, processes[idx_shortest_process].execution_time);
					processes[idx_shortest_process].state = PSTATE_RUNNING;
					*running_process_idx = idx_shortest_process;
				}
			}
			break;

	}
}

int run_process(process* process_to_start, int cur_time, int unit){
	if (process_to_start->state == PSTATE_PAUSED){
		struct sched_param param;

		//RESUME
		//printf("Resumed process %s at time %d\n", process_to_start->name, cur_time);
		
		param.sched_priority = 0;
		sched_setscheduler(process_to_start->pid, SCHED_OTHER, &param);


	}else if (process_to_start->state == PSTATE_READY){
		//START
		int pid;
		//printf("Start process %s at time %d\n", process_to_start->name, cur_time);
	
		pid = fork();
		if (pid < 0){
			//ERROR
		}else if (pid == 0){
			struct timespec start_time;
			//CHILD PROCESS
			//PRINT YOURSELF :)
			printf("%s %d\n", process_to_start->name, getpid());
			syscall(334, 1, getpid(), &start_time);
			//Do Something
			//
			for (int j = 0; j < unit; j++){
				{volatile unsigned long i; for (i = 0; i < 1000000UL; i++);}
			}
			//Get end time
			syscall(334, 0, getpid(), &start_time);
			//printf("%s %d %d(END)\n", process_to_start->name, getpid(), unit);
			exit(0);

		}else{
			//PARENT PROCESS
			//ASSIGN CPU TO CHILD
			cpu_set_t mask;
			CPU_ZERO(&mask);
			CPU_SET(CHILD_CPU, &mask);
			sched_setaffinity(pid, sizeof(mask), &mask);

			process_to_start->pid = pid;

		}



	}else{
		//ERROR ==> NOT MAKING SENSE
		printf("ERROR: run a process which is not paused or ready\n");
	}
	return 0;
}
int preempt_process(process* process_to_preempt, int cur_time){
	if (process_to_preempt->state == PSTATE_RUNNING){
		struct sched_param param;

		//PREEMPT
		//printf("Preempt process %s at time %d\n", process_to_preempt->name, cur_time);
		
		param.sched_priority = 0;
		sched_setscheduler(process_to_preempt->pid, SCHED_IDLE, &param);
	}else{
		//ERROR ==> NOT MAKING SENSE
		printf("ERROR: preempt a process which is not running\n");
	}
	return 0;
}
int finish_process(process* process_to_finish, int cur_time){
	if (process_to_finish->state == PSTATE_RUNNING){
		//FINISH the process
		//printf("Finish process %s at time %d\n", process_to_finish->name, cur_time);
		waitpid(process_to_finish->pid, NULL, 0);
	}else{
		//ERROR ==> NOT MAKING SENSE
		printf("ERROR: finish a process which is not running\n");
	}
	return 0;
}
