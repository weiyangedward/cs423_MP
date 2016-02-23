#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h> // workqueue
#include "mp1_given.h"

#define DEBUG 1
#define PROCFS_MAX_SIZE 1024
#define FILENAME "status"
#define DIRECTORY "mp1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("G14");
MODULE_DESCRIPTION("CS-423 MP1");

/*===== global variable declaration ======*/
static struct proc_dir_entry *mp1, *status;
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];
static struct timer_list update_timer;
static struct workqueue_struct *update_workqueue; // workqueue

/*======= method declaration ===========*/
static ssize_t cpu_write_callback(struct file *file, const char __user *buffer, size_t count, loff_t *data);
static int cpu_proc_show(struct seq_file *sf, void *v);
static int cpu_proc_open(struct inode *inode, struct file *file);
static void update_work(struct work_struct *work);
void _update_timer_handler(unsigned long data);


// linked list
struct proc_item
{
    int pid;
    int cputime;
    
    struct list_head list;
};

LIST_HEAD(proc_list);

static const struct file_operations cpu_proc_fops = {
    .owner = THIS_MODULE,
    .open = cpu_proc_open,
    .read = seq_read,
    .write = cpu_write_callback,
};

// proc fs
// read
static int cpu_proc_show(struct seq_file *sf, void *v)
{
    struct proc_item *cur;
    
    list_for_each_entry(cur, &proc_list, list)
    {
        seq_printf(sf, "%d: %d\n", cur->pid, cur->cputime);
    }
    
    return 0;
}

static int cpu_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cpu_proc_show, NULL);
}


// write
static ssize_t cpu_write_callback(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
    int pid;
    // Allocate a new node
    struct proc_item *new = (struct proc_item *)kmalloc(sizeof(struct proc_item), GFP_KERNEL);

//    /* get buffer size */
//    procfs_buffer_size = count;
//    if (procfs_buffer_size > PROCFS_MAX_SIZE)
//    {
//        procfs_buffer_size = PROCFS_MAX_SIZE;
//    }

    // Get input (pid)
    if (copy_from_user(procfs_buffer, buffer, count))
    {
        return -EFAULT;
    }

    // Initialize the item for the new PID
    kstrtoint(procfs_buffer, 0, &pid);
    new->pid = pid;
    INIT_LIST_HEAD(&new->list);

    // Register the new item to the list
    list_add(&new->list, &proc_list);

    return count;
}

/*======
TOP-half
use workqueue to schedule the run of timer
*/
void _update_timer_handler(unsigned long data)
{
    // Create a work to do update_work
    struct work_struct *work = (struct work_struct *)kmalloc(sizeof(struct work_struct), GFP_KERNEL); // use heap mem since there might be more than one workqueue, one for each process
    INIT_WORK(work, update_work);
    
    // Enqueue the work immediately for later execution
    queue_work(update_workqueue, work);
    
    // Restart the timer, increment run timer every 5s
    mod_timer(&update_timer, jiffies + 5 * HZ);
}

/*
BOTTOM-half
run timers
*/
static void update_work(struct work_struct *work)
{
    struct proc_item *cur, *temp;
    
    printk(KERN_INFO "This is from work function\n");
    /*======= critical section ==========*/
    // The works in workqueue are asynchronous,
    //  so locking for the proc_list is necessary
    //  in case multiple works are updating the list items simultaneously
    spin_lock(&list_lock);    
    
    // Update each item
    list_for_each_entry_safe(cur, temp, &proc_list, list)
    {
        if (get_cpu_use(cur->pid, &cur->cpu_use) == -1)
            // If the process is terminated,
            //  delete it from the proc_list
        {
            list_del(&cur->list);
            kfree(cur);
        }
    }

    spin_unlock(&list_lock);
    
    // This work is done
    kfree(work);
}

static int __init cpu_proc_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE LOADING\n");
    #endif

    // Create 'mp1' dir
    mp1 = proc_mkdir(DIRECTORY, NULL);

    // Create 'status' file, FILENAME is the 'file' used by cpu_proc_fops
    status = proc_create(FILENAME, 0666, mp1, &cpu_proc_fops);
    
    init_timer(&update_timer); // initialize a timer
    update_timer.expires = jiffies + 5 * HZ; // expire is time when timer will run, run every 5s, each second = HZ
    update_timer.function = _update_timer_handler; // at the expires time, function will be called with data as arg
    
    printk(KERN_ALERT "MP1 MODULE LOADED\n");
    return 0;    
}

static void __exit cpu_proc_exit(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
    #endif
    
    struct proc_item *cur, *temp;
    
    // Free the nodes
    list_for_each_entry_safe(cur, temp, &proc_list, list)
    {
        list_del(&cur->list);
        kfree(cur);
    }
    
    // Remove 'status' file
    remove_proc_entry("status", mp1);

    // Remove 'mp1' dir
    remove_proc_entry("mp1", NULL);


    printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(cpu_proc_init);
module_exit(cpu_proc_exit);
