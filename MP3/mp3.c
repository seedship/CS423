#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include "mp3_given.h"
#include <linux/uaccess.h>	/* for copy_*_user */
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/page-flags.h>
#include <linux/kthread.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>

#include <asm/page_types.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP3");

#define PROCFS_MAX_SIZE 2048
#define DEBUG 1

#define KB (1<<10)

#define VBUFFER_SIZE_B (4*128*KB)

/**
 * @brief the mp3_task_struct contains all relevant information about tasks
 */
typedef struct mp3_task_struct_t {
		struct task_struct* linux_task;
		unsigned long utime;
		unsigned long stime;
		unsigned long major_fault;
		unsigned long minor_fault;
		pid_t pid;
		struct list_head task_node;
} mp3_task_struct;


//procfs variables
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

//vbuffer
static uint8_t *vbuffer;

//workqueue things
static struct workqueue_struct *queue;

//character device driver
static dev_t mp3_dev;
struct cdev mp3_cdev;

//task list variables
unsigned num_entries;
mp3_task_struct head;

unsigned vbuffer_idx;

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
		if(tmp->pid == pid){
			mutex_unlock(&task_struct_mutex);
			return tmp;
		}
	}
	mutex_unlock(&task_struct_mutex);
	return NULL;
}

static void mp3_work_func(struct work_struct *work);

DECLARE_DELAYED_WORK(mp3_deplayed_work, mp3_work_func);

static void mp3_work_func(struct work_struct *work){
	struct list_head *pos;
	struct list_head *q;

	unsigned long total_major_fault = 0;
	unsigned long total_minor_fault = 0;
	unsigned long total_utilization = 0;

	if(!num_entries){
		vbuffer_idx = 0;
		return;
	}

	list_for_each_safe(pos, q, &head.task_node){
		mp3_task_struct * tmp = list_entry(pos, mp3_task_struct, task_node);
		//If task valid, update use. Otherwise, remove from list.
		if(!get_cpu_use(tmp->pid, &tmp->minor_fault, &tmp->major_fault, &tmp->utime, &tmp->stime)){
			total_major_fault += tmp->major_fault;
			total_minor_fault += tmp->minor_fault;
			total_utilization += (tmp->utime + tmp->stime);
		} else {
			printk(KERN_ALERT "Can not find CPU use for unknown PID: %u, deleting it\n", tmp->pid);
			list_del(pos);
			kfree(tmp);
			num_entries--;
		}
	}
	mutex_unlock(&task_struct_mutex);

	((unsigned long*)vbuffer)[vbuffer_idx++] = jiffies;
	((unsigned long*)vbuffer)[vbuffer_idx++] = total_minor_fault;
	((unsigned long*)vbuffer)[vbuffer_idx++] = total_major_fault;
	((unsigned long*)vbuffer)[vbuffer_idx++] = total_utilization;
	vbuffer_idx %= (VBUFFER_SIZE_B / sizeof(unsigned long));

	printk(KERN_ALERT "vbuffer idx: %u max idx: %lu\n", vbuffer_idx, VBUFFER_SIZE_B / sizeof(unsigned long));

	queue_delayed_work(queue, &mp3_deplayed_work, msecs_to_jiffies(50));
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
	if(num_entries && !*data){
		kernelbuffer = (char*) kcalloc(100 * num_entries, sizeof(char), GFP_ATOMIC);

		mutex_lock(&task_struct_mutex);
		list_for_each(pos, &head.task_node){
			mp3_task_struct * tmp = list_entry(pos, mp3_task_struct, task_node);
			offset += sprintf(kernelbuffer + offset, "PID:\t%u\n", tmp->pid);
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

	struct task_struct *toremove;

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
			printk(KERN_ALERT "Registering task with PID:%u\n", pid);
			tmp = (mp3_task_struct*)kmalloc(sizeof(mp3_task_struct), GFP_KERNEL);
			tmp->linux_task = find_task_by_pid(pid);
			tmp->major_fault = 0;
			tmp->minor_fault = 0;
			tmp->pid = pid;
			tmp->utime = 0;
			tmp->stime = 0;
			INIT_LIST_HEAD(&tmp->task_node);

			mutex_lock(&task_struct_mutex);
			if(!num_entries) queue_delayed_work(queue, &mp3_deplayed_work, msecs_to_jiffies(50));
			num_entries++;
			list_add(&(tmp->task_node), &head.task_node);
			mutex_unlock(&task_struct_mutex);
			printk(KERN_ALERT "Registered task with PID:%u\n", tmp->pid);
			break;
		case 'U': //Unregistration
			sscanf(&procfs_buffer[2], "%u", &pid);
			printk(KERN_ALERT "Begin unregistering task with PID:%u\n", pid);
			tmp = find_mp3_task_struct_by_pid(pid);
			if(tmp){
				toremove = tmp->linux_task;
				mutex_lock(&task_struct_mutex);
				num_entries--;
				list_del(&(tmp->task_node));
				kfree(tmp);
				mutex_unlock(&task_struct_mutex);
				printk(KERN_ALERT "Unregistered task with PID:%u\n", pid);
			} else {
				printk(KERN_ALERT "Received unknown PID in unregister: %u\n", pid);
			}
			break;
		default:
			printk(KERN_ALERT "Received unknown prefix in mp3_write\n");
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

static int chardev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long x;
	unsigned long pfn;

	if(vma->vm_end - vma->vm_start > VBUFFER_SIZE_B) return -EINVAL;

	for(x = 0; x < VBUFFER_SIZE_B; x+=PAGE_SIZE){
		pfn = vmalloc_to_pfn((void*)(vbuffer+x));
		if(remap_pfn_range(vma, vma->vm_start+x, pfn, PAGE_SIZE, vma->vm_page_prot)){
			printk(KERN_ALERT "MP3 module could not perform mmap!\n");
			return -EAGAIN;
		}
	}
//	printk(KERN_ALERT "worked!\n");
	return 0;
}

/**
 * @brief character device driver file operations
 */
static const struct file_operations mp3_chardev_fops = {
		.owner = THIS_MODULE,
		.open = NULL,
		.release = NULL,
		.mmap = chardev_mmap
};

/**
 * @brief mp3_init called when the module is loaded. Sets up all necessary variables
 * @return 0 when done
 */
int __init mp3_init(void)
{
	unsigned long x;
#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE LOADING\n");
#endif

	//create proc files
	proc_dir = proc_mkdir("mp3", NULL);
	proc_entry = proc_create("status", 0666, proc_dir, &mp3_file);

	//create vbuffer
	vbuffer = vmalloc(VBUFFER_SIZE_B);
	memset(vbuffer, -1, VBUFFER_SIZE_B);
	vbuffer_idx = 0;

	for(x = 0; x < VBUFFER_SIZE_B; x+=PAGE_SIZE){
		SetPageReserved(vmalloc_to_page((void*)(vbuffer+x)));
	}

	//create a mutex
	mutex_init(&task_struct_mutex);

	//init character device driver
	alloc_chrdev_region(&mp3_dev, 0, 1, "mp3_cdev");
	cdev_init(&mp3_cdev, &mp3_chardev_fops);
	cdev_add(&mp3_cdev, mp3_dev, 1);

	//init list
	num_entries = 0;
	INIT_LIST_HEAD(&head.task_node);

//	printk(KERN_ALERT "Page size is %lu\n", PAGE_SIZE);

	//create workqueue and timer
	queue = create_singlethread_workqueue("mp3_workqueue");

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
	unsigned long x = 0;
#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
#endif

	//cleanup procfs
	proc_remove(proc_entry);
	proc_remove(proc_dir);

	//cleanup
	cancel_delayed_work_sync(&mp3_deplayed_work);
	flush_delayed_work(&mp3_deplayed_work);
	destroy_workqueue(queue);

	//cleanup task list
	mutex_lock(&task_struct_mutex);
	list_for_each_safe(pos, q, &head.task_node){
		tmp = list_entry(pos, mp3_task_struct, task_node);
		list_del(pos);
		kfree(tmp);
	}
	mutex_unlock(&task_struct_mutex);

	mutex_destroy(&task_struct_mutex);

	//remove character device driver
	cdev_del(&mp3_cdev);
	unregister_chrdev_region(mp3_dev, 1);

	//remove memory buffer
	for(x = 0; x < VBUFFER_SIZE_B; x+=PAGE_SIZE){
		ClearPageReserved(vmalloc_to_page((void*)(vbuffer+x)));
	}
	vfree(vbuffer);

	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
