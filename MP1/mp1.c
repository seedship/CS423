#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include "mp1_given.h"
#include <asm/uaccess.h>	/* for copy_*_user */
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP1");

#define PROCFS_MAX_SIZE 	2048
#define DEBUG 1


//procfs variables
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

//timer and queue variables
static struct timer_list mp1_timer;
static struct workqueue_struct *queue;
struct mutex list_mutex;

typedef struct mp1_entry_t {
		pid_t pid;
		unsigned long use;
		struct list_head mp1_list;
} mp1_entry;

//linkedlist variables
unsigned num_entries;
mp1_entry head;

static void mp1_work_func(struct work_struct *work){
	struct list_head *pos, *q;

	mutex_lock(&list_mutex);
	list_for_each_safe(pos, q, &head.mp1_list){
		mp1_entry * tmp = list_entry(pos, mp1_entry, mp1_list);
		//If task valid, update use. Otherwise, remove from list.
		if(get_cpu_use(tmp->pid, &tmp->use)){
			list_del(pos);
			kfree(tmp);
			num_entries--;
		}
	}
	mutex_unlock(&list_mutex);
}

DECLARE_WORK(work, mp1_work_func);

void timer_callback(unsigned long data){
	queue_work(queue, &work);
	mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));
}

static ssize_t mp1_read(struct file *file, char __user *buffer, size_t count, loff_t * data){
	char* kernelbuffer;
	ssize_t len;
	struct list_head *pos;
	unsigned offset = 0;

	//Copy to buffer if no previous data
	if(num_entries > 0 && !(*data)){
		kernelbuffer = (char*) kcalloc(num_entries * 100, sizeof(char), GFP_ATOMIC);

		mutex_lock(&list_mutex);
		list_for_each(pos, &head.mp1_list){
			mp1_entry * tmp = list_entry(pos, mp1_entry, mp1_list);
			offset += sprintf(kernelbuffer + offset, "PID: %d\t%lu\n", tmp->pid, tmp->use);
		}
		mutex_unlock(&list_mutex);

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

static ssize_t mp1_write(struct file *file, const char *buffer, size_t count, loff_t * data){
	pid_t val;
	mp1_entry* tmp;

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

	//parse string into int
	kstrtoint(procfs_buffer, 10, &val);

	//add entries
	tmp = (mp1_entry*) kmalloc(sizeof(mp1_entry), GFP_ATOMIC);
	INIT_LIST_HEAD(&tmp->mp1_list);
	tmp->pid = val;
	tmp->use = 0;

	mutex_lock(&list_mutex);
	list_add(&(tmp->mp1_list), &head.mp1_list);
	num_entries++;
	mutex_unlock(&list_mutex);



	return procfs_buffer_size;
}

static const struct file_operations mp1_file = {
		.owner = THIS_MODULE,
		.read = mp1_read,
		.write = mp1_write
};

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
#ifdef DEBUG
	printk(KERN_ALERT "MP1 MODULE LOADING\n");
#endif
	//create proc files
	proc_dir = proc_mkdir("mp1", NULL);
	proc_entry = proc_create("status", 0666, proc_dir, &mp1_file);

	//create a mutex
	mutex_init(&list_mutex);

	//init list
	num_entries = 0;
	INIT_LIST_HEAD(&head.mp1_list);

	//create workqueue and timer
	queue = create_singlethread_workqueue("mp1_workqueue");
	setup_timer(&mp1_timer, timer_callback, 0);
	mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));

	printk(KERN_ALERT "MP1 MODULE LOADED\n");
	return 0;
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
	struct list_head *pos, *q;
	mp1_entry * tmp;
#ifdef DEBUG
	printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
#endif

	//cleanup
	del_timer(&mp1_timer);

	cancel_work_sync(&work);
	destroy_workqueue(queue);

	list_for_each_safe(pos, q, &head.mp1_list){
		tmp = list_entry(pos, mp1_entry, mp1_list);
		list_del(pos);
		kfree(tmp);
	}

	proc_remove(proc_entry);
	proc_remove(proc_dir);


	printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
