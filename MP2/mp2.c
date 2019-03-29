#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include "mp2_given.h"
#include <linux/uaccess.h>	/* for copy_*_user */
#include <linux/slab.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP2");

#define PROCFS_MAX_SIZE 2048
#define DEBUG 1

/**
 * @brief the mp2_task_state contains all possible states of tasks
 */
typedef enum mp2_task_state_t
{
	READY,
	SLEEPING,
	RUNNING
} mp2_task_state;

/**
 * @brief the mp2_task_struct contains all relevant information about tasks
 */
typedef struct mp2_task_struct_t {
		struct task_struct* linux_task;
		struct list_head task_node;
		struct timer_list wakeup_timer;

		pid_t pid;

		mp2_task_state task_state;
		unsigned long deadline_jiffies;
		unsigned int period_ms;
		unsigned int computation_ms;
} mp2_task_struct;


//procfs variables
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

//mp2_task_struct cache
struct kmem_cache * mp2_cache;

//dispatch task task_struct
struct task_struct * mp2_dispatch_task;

//linked list variables
unsigned num_entries;

mp2_task_struct head;
mp2_task_struct * currently_running_task;

//parameter for scheduling
struct sched_param sparam_fifo;
struct sched_param sparam_normal;

//mutexes
struct mutex task_struct_mutex;
struct mutex currently_running_task_mutex;

/**
 * @brief find_mp2_task_struct_by_pid Finds the mp2_task_struct in the linked list
 * based on the provided pid.
 * @param pid - the pid of the mp2_task_struct to find
 * @return pointer to the mp2_task_struct
 */
mp2_task_struct * find_mp2_task_struct_by_pid(pid_t pid){
	struct list_head * pos;
	mutex_lock(&task_struct_mutex);
	list_for_each(pos, &head.task_node){
		mp2_task_struct * tmp = list_entry(pos, mp2_task_struct, task_node);
		if(tmp->pid == pid){
			mutex_unlock(&task_struct_mutex);
			return tmp;
		}
	}
	mutex_unlock(&task_struct_mutex);
	return NULL;
}

/**
 * @brief timer_callback - handler function whenever a task's release timer
 * expires. This function sets the task's run state to READY.
 * @param data - contains the pid of the caller
 */
void timer_callback(unsigned long data){
	pid_t pid = (pid_t) data;
	mp2_task_struct * task_struct = find_mp2_task_struct_by_pid(pid);

	int result = 0;

	if(task_struct){
		task_struct->task_state = READY;
		task_struct->deadline_jiffies += msecs_to_jiffies(task_struct->period_ms);
		mod_timer(&task_struct->wakeup_timer, task_struct->deadline_jiffies);

		result = wake_up_process(mp2_dispatch_task);
//		printk(KERN_ALERT "Set ready process with pid %u and set it's next deadline to %lu. Wakeup result: %d\n", pid, task_struct->deadline_jiffies, result);

	} else {
		printk(KERN_ALERT "Timer interrupt for unknown PID: %u\n", pid);
	}
}

/**
 * @brief mp2_read - handler function for read. Called whenever a read is made
 * to the procfs file.
 * @param file - unused
 * @param buffer - userspace buffer to copy written data to
 * @param count - size of buffer
 * @param data - used to delineate if it was the first time this function was called
 * @return number of bytes read
 */
static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t * data){
	char* kernelbuffer;
	ssize_t len;
	struct list_head *pos;
	unsigned offset = 0;

	//Copy to buffer if no previous data
	if(!(num_entries && *data)){
		kernelbuffer = (char*) kcalloc(100, sizeof(char), GFP_ATOMIC);

		mutex_lock(&task_struct_mutex);
		list_for_each(pos, &head.task_node){
			mp2_task_struct * tmp = list_entry(pos, mp2_task_struct, task_node);
			offset += sprintf(kernelbuffer + offset, "PID:\t%u\tComputation:\t%u\tPeriod:\t%u\n", tmp->pid, tmp->computation_ms, tmp->period_ms);
		}
		mutex_unlock(&task_struct_mutex);

		len = min(strlen(kernelbuffer), count);
		*data += len;

		if(len == count){
			printk(KERN_ALERT "User buffer length exceeded, truncating\n");
			kernelbuffer[len-1] = '\0';
		}

		if (copy_to_user(buffer, kernelbuffer, len)) {
			return -EFAULT;
		}

		kfree(kernelbuffer);
		return len;
	}
	return 0;
}

/**
 * @brief isAdmissible - Checks to see if the function is admissible and passes
 * utilization criteria
 * @param c - computation time of new task
 * @param p - period of new task
 * @return 1 if schedulable, 0 if not
 */
bool isAdmissible(unsigned c, unsigned p){
	struct list_head *pos;
	unsigned int sum = 0;
	sum += c * 10000 / p;
	mutex_lock(&task_struct_mutex);
	list_for_each(pos, &head.task_node){
		mp2_task_struct * tmp = list_entry(pos, mp2_task_struct, task_node);
		sum += tmp->computation_ms * 10000 / tmp->period_ms;
	}
	mutex_unlock(&task_struct_mutex);
	return sum <= 6930;
}

/**
 * @brief mp2_write - handler function for write. Called whenever a write is made
 * @param file - unused
 * @param buffer - buffer of write data from userspace
 * @param count - length of buffer
 * @param data - unused
 * @return number of bytes written
 */
static ssize_t mp2_write(struct file *file, const char *buffer, size_t count, loff_t * data){
	pid_t pid;
	unsigned int period, computation;

	struct list_head *pos, *q;

	mp2_task_struct* tmp;

	//calculate buffer size
	if ( count > PROCFS_MAX_SIZE )	{
		procfs_buffer_size = PROCFS_MAX_SIZE;
	}
	else	{
		procfs_buffer_size = count;
	}

	//copy to user
	if ( copy_from_user(procfs_buffer, buffer, procfs_buffer_size) ) {
		return -EFAULT;
	}

	switch (procfs_buffer[0]) {
		case 'R': //Registration
			sscanf(&procfs_buffer[2], "%u, %u, %u", &pid, &period, &computation);
			if(isAdmissible(computation, period)){
				tmp = (mp2_task_struct*)kmem_cache_alloc(mp2_cache, GFP_KERNEL);
				tmp->pid = pid;
				tmp->linux_task = find_task_by_pid(pid);
				tmp->period_ms = period;
				tmp->computation_ms = computation;
				tmp->task_state = SLEEPING;
				tmp->deadline_jiffies = 0;
				setup_timer(&tmp->wakeup_timer, timer_callback, tmp->pid);
				INIT_LIST_HEAD(&tmp->task_node);

				mutex_lock(&task_struct_mutex);
				num_entries++;
				list_add(&(tmp->task_node), &head.task_node);
				mutex_unlock(&task_struct_mutex);

//				printk(KERN_ALERT "Registering task with PID: %u Period: %u  Compute Time: %u\n", pid, period, computation);
			} else {
				printk(KERN_ALERT "Job with PID: %u, computation time %u, period %u is not admissible.\n", pid, computation, period);
			}
			break;
		case 'Y': //Yield
			sscanf(&procfs_buffer[2], "%d", &pid);

			tmp = find_mp2_task_struct_by_pid(pid);
			if(!tmp) break;
			mutex_lock_interruptible(&task_struct_mutex);
			tmp->task_state = SLEEPING;
			if(!tmp->deadline_jiffies){
				tmp->deadline_jiffies = jiffies + msecs_to_jiffies(tmp->period_ms);
				mod_timer(&tmp->wakeup_timer, tmp->deadline_jiffies);
			}
//			printk(KERN_ALERT "Yielding: %u (%u %u)\n", tmp->pid, tmp->computation_ms, tmp->period_ms);
			mutex_unlock(&task_struct_mutex);

			mutex_lock_interruptible(&currently_running_task_mutex);
			if(tmp == currently_running_task){
				currently_running_task = NULL;
			}
			mutex_unlock(&currently_running_task_mutex);

			wake_up_process(mp2_dispatch_task);

			set_task_state(tmp->linux_task, TASK_UNINTERRUPTIBLE);
			schedule();
			break;
		case 'D': //Deregistration
			sscanf(&procfs_buffer[2], "%d", &pid);

			mutex_lock(&task_struct_mutex);
			list_for_each_safe(pos, q, &head.task_node){
				mp2_task_struct * tmp = list_entry(pos, mp2_task_struct, task_node);
				//Remove task from list.
				if(tmp->pid == pid){
					del_timer(&tmp->wakeup_timer);

//					printk(KERN_ALERT "Removing task %u (%u %u)\n", tmp->pid, tmp->computation_ms, tmp->period_ms);
					list_del(pos);
					kmem_cache_free(mp2_cache, tmp);
					num_entries--;
					mutex_lock(&currently_running_task_mutex);
					if(tmp == currently_running_task){
						currently_running_task = NULL;
					}
					mutex_unlock(&currently_running_task_mutex);
					break;
				}
			}
			mutex_unlock(&task_struct_mutex);
			break;
		default:
			printk(KERN_ALERT "Received unknown prefix in mp2_write\n");
			break;
	}

	memset(procfs_buffer, 0, procfs_buffer_size);
	return procfs_buffer_size;
}

/**
 * @brief mp2_schedule - handler function for dispatcher thread. Schedules the
 * highest priority ready task
 * @param data - unused
 * @return 0 when finished
 */
int mp2_schedule(void * data) {
	mp2_task_struct *tmp, *next;
	struct list_head *pos;
	unsigned int shortest_relative_period;

	while(1){
		//sleep
//		printk(KERN_ALERT "dispatch kthread going to sleep\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
//		printk(KERN_ALERT "dispatch kthread waking up\n");

		//check to see if thread was shutdown
		if(kthread_should_stop()){
			printk(KERN_ALERT "Stopping dispatch kthread\n");
			mutex_lock(&currently_running_task_mutex);
			if(currently_running_task){ //deschedule current task
				printk(KERN_ALERT "Descheduling current running task\n");
				currently_running_task->task_state = SLEEPING;
				sched_setscheduler(currently_running_task->linux_task, SCHED_NORMAL, &sparam_normal);
			}
			mutex_unlock(&currently_running_task_mutex);
			return 0;
		}

		//setup variables
		next = NULL;
		shortest_relative_period  = 0;

		//find next task to schedule
		mutex_lock(&task_struct_mutex);
		list_for_each(pos, &head.task_node){
			tmp = list_entry(pos, mp2_task_struct, task_node);
			if((tmp->task_state == READY) &&(tmp->period_ms < shortest_relative_period || !next)){
				shortest_relative_period = tmp->period_ms;
				next = tmp;
			}
		}
		mutex_unlock(&task_struct_mutex);

		//preempt/deschedule current task
		mutex_lock(&currently_running_task_mutex);
		if(currently_running_task && next){
			if(next->computation_ms < currently_running_task->computation_ms){ //preemption
//				printk(KERN_ALERT "Task %u (%u %u) is preempting task %u (%u %u)\n", next->pid, next->computation_ms, next->period_ms, currently_running_task->pid, currently_running_task->computation_ms, currently_running_task->period_ms);
				currently_running_task->task_state = READY;
				sched_setscheduler(currently_running_task->linux_task, SCHED_NORMAL, &sparam_normal);
				set_task_state(currently_running_task->linux_task, TASK_UNINTERRUPTIBLE);

				next->task_state = RUNNING;
				wake_up_process(next->linux_task);
				sched_setscheduler(next->linux_task, SCHED_FIFO, &sparam_fifo);
				currently_running_task = next;
			}
		} else if (next){
//			printk(KERN_ALERT "Scheduling %u (%u %u)\n", next->pid, next->computation_ms, next->period_ms);
			next->task_state = RUNNING;
			wake_up_process(next->linux_task);
			sched_setscheduler(next->linux_task, SCHED_FIFO, &sparam_fifo);
			currently_running_task = next;
		}
		mutex_unlock(&currently_running_task_mutex);
	}

	return 0;
}

/**
 * @brief associates file actions to their respective handlers
 */
static const struct file_operations mp2_file = {
		.owner = THIS_MODULE,
		.read = mp2_read,
		.write = mp2_write
};

/**
 * @brief mp2_init called when the module is loaded. Sets up all necessary variables
 * @return 0 when done
 */
int __init mp2_init(void)
{
#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADING\n");
#endif
	//create proc files
	proc_dir = proc_mkdir("mp2", NULL);
	proc_entry = proc_create("status", 0666, proc_dir, &mp2_file);

	//create a mutex
	mutex_init(&task_struct_mutex);
	mutex_init(&currently_running_task_mutex);

	//init list
	num_entries = 0;
	INIT_LIST_HEAD(&head.task_node);

	//slab cache
	mp2_cache = kmem_cache_create("mp2_cache", sizeof(mp2_task_struct), 0, SLAB_HWCACHE_ALIGN, NULL);

	//initialize currently running task to NULL
	mutex_lock(&currently_running_task_mutex);
	currently_running_task = NULL;
	mutex_unlock(&currently_running_task_mutex);

	//scheduling params
	sparam_fifo.sched_priority = 99;
	sparam_normal.sched_priority = 0;

	//dispatch thread
	mp2_dispatch_task = kthread_run(mp2_schedule, NULL, "mp2_dispatch_thread");

	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	return 0;
}

/**
 * @brief mp2_exit called when the module is unloaded. Cleans up all variables
 */
void __exit mp2_exit(void)
{
	struct list_head *pos, *q;
	mp2_task_struct* tmp;
#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
#endif

	//cleanup procfs
	proc_remove(proc_entry);
	proc_remove(proc_dir);

	//stop kernel thread
	kthread_stop(mp2_dispatch_task);

	//cleanup task list
	mutex_lock(&task_struct_mutex);
	list_for_each_safe(pos, q, &head.task_node){
		tmp = list_entry(pos, mp2_task_struct, task_node);
		del_timer(&tmp->wakeup_timer);
		list_del(pos);
		kmem_cache_free(mp2_cache, tmp);
	}
	mutex_unlock(&task_struct_mutex);
	kmem_cache_destroy(mp2_cache);

	mutex_destroy(&task_struct_mutex);
	mutex_destroy(&currently_running_task_mutex);

	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
