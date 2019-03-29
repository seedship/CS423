#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include "mp3_given.h"
#include <linux/uaccess.h>	/* for copy_*_user */
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP3");

#define PROCFS_MAX_SIZE 2048
#define DEBUG 1

/**
 * @brief the mp3_task_struct contains all relevant information about tasks
 */
typedef struct mp3_task_struct_t {
		struct task_struct* linux_task;
		unsigned utilization;
		unsigned major_fault;
		unsigned minor_fault;
		struct list_head task_node;
} mp3_task_struct;


//procfs variables
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

unsigned num_entries;
mp3_task_struct head;

//mutexes
struct mutex task_struct_mutex;

/**
 * @brief find_mp3_task_struct_by_pid Finds the mp3_task_struct in the linked list
 * based on the provided pid.
 * @param pid - the pid of the mp3_task_struct to find
 * @return pointer to the mp3_task_struct
 */
mp3_task_struct * find_mp3_task_struct_by_pid(pid_t pid){
	struct list_head * pos;
	mutex_lock(&task_struct_mutex);
	list_for_each(pos, &head.task_node){
		mp3_task_struct * tmp = list_entry(pos, mp3_task_struct, task_node);
		if(tmp->linux_task->pid == pid){
			mutex_unlock(&task_struct_mutex);
			return tmp;
		}
	}
	mutex_unlock(&task_struct_mutex);
	return NULL;
}

/**
 * @brief mp3_read - handler function for read. Called whenever a read is made
 * to the procfs file.
 * @param file - unused
 * @param buffer - userspace buffer to copy written data to
 * @param count - size of buffer
 * @param data - used to delineate if it was the first time this function was called
 * @return number of bytes read
 */
static ssize_t mp3_read(struct file *file, char __user *buffer, size_t count, loff_t * data){
	char* kernelbuffer;
	ssize_t len;
	struct list_head *pos;
	unsigned offset = 0;

	//Copy to buffer if no previous data
	if(!(num_entries && *data)){
		kernelbuffer = (char*) kcalloc(100, sizeof(char), GFP_ATOMIC);

		mutex_lock(&task_struct_mutex);
		list_for_each(pos, &head.task_node){
			mp3_task_struct * tmp = list_entry(pos, mp3_task_struct, task_node);
			offset += sprintf(kernelbuffer + offset, "PID:\t%u\n", tmp->linux_task->pid);
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
 * @brief mp3_write - handler function for write. Called whenever a write is made
 * @param file - unused
 * @param buffer - buffer of write data from userspace
 * @param count - length of buffer
 * @param data - unused
 * @return number of bytes written
 */
static ssize_t mp3_write(struct file *file, const char *buffer, size_t count, loff_t * data){
	pid_t pid;
	mp3_task_struct* tmp;

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
			sscanf(&procfs_buffer[2], "%u", &pid);
			tmp = (mp3_task_struct*)vmalloc(sizeof(mp3_task_struct));
			tmp->linux_task = find_task_by_pid(pid);
			tmp->major_fault = 0;
			tmp->minor_fault = 0;
			tmp->utilization = 0;
			INIT_LIST_HEAD(&tmp->task_node);

			mutex_lock(&task_struct_mutex);
			num_entries++;
			list_add(&(tmp->task_node), &head.task_node);
			mutex_unlock(&task_struct_mutex);
			break;
		case 'U': //Unregistration
			sscanf(&procfs_buffer[2], "%d", &pid);

			mutex_lock(&task_struct_mutex);
			tmp = find_mp3_task_struct_by_pid(pid);
			list_del(&tmp->task_node);
			vfree(tmp);
			mutex_unlock(&task_struct_mutex);
			break;
		default:
			printk(KERN_ALERT "Received unknown prefix in m3_write\n");
			break;
	}

	memset(procfs_buffer, 0, procfs_buffer_size);
	return procfs_buffer_size;
}

/**
 * @brief associates file actions to their respective handlers
 */
static const struct file_operations mp3_file = {
		.owner = THIS_MODULE,
		.read = mp3_read,
		.write = mp3_write
};

/**
 * @brief mp3_init called when the module is loaded. Sets up all necessary variables
 * @return 0 when done
 */
int __init mp3_init(void)
{
#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE LOADING\n");
#endif
	//create proc files
	proc_dir = proc_mkdir("mp3", NULL);
	proc_entry = proc_create("status", 0666, proc_dir, &mp3_file);

	//create a mutex
	mutex_init(&task_struct_mutex);

	//init list
	num_entries = 0;
	INIT_LIST_HEAD(&head.task_node);

	printk(KERN_ALERT "MP3 MODULE LOADED\n");
	return 0;
}

/**
 * @brief mp3_exit called when the module is unloaded. Cleans up all variables
 */
void __exit mp3_exit(void)
{
	struct list_head *pos, *q;
	mp3_task_struct* tmp;
#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
#endif

	//cleanup procfs
	proc_remove(proc_entry);
	proc_remove(proc_dir);

	//cleanup task list
	mutex_lock(&task_struct_mutex);
	list_for_each_safe(pos, q, &head.task_node){
		tmp = list_entry(pos, mp3_task_struct, task_node);
		list_del(pos);
		vfree(tmp);
	}
	mutex_unlock(&task_struct_mutex);

	mutex_destroy(&task_struct_mutex);

	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
