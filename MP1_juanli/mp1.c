#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h> // seq_file
#include <linux/slab.h> // kmalloc
#include <linux/list.h> // list_head
#include <linux/string.h>
#include <linux/timer.h> // timer
#include <linux/spinlock.h> // spinlock
#include <linux/workqueue.h> // workqueue
#include <asm/uaccess.h>
#include "mp1_given.h"

#define DEBUG 1
#define PROCFS_MAX_SIZE 1024 // size of proc file
#define FILENAME "status"
#define DIRECTORY "mp1"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("G14");
MODULE_DESCRIPTION("CS-423 MP1");

/* Variable declaration */

static struct proc_dir_entry *mp1, *status;
static unsigned long procfs_buffer_size = 0;
static char procfs_buffer[PROCFS_MAX_SIZE];
static struct timer_list update_timer; // timer
static struct workqueue_struct *update_workqueue; // workqueue
static spinlock_t list_lock; // spinlock

/* Function forward declaration */

static int _proc_show_callback(struct seq_file *sf, void *v);
static int _proc_open_callback(struct inode *inode, struct file *file);
static ssize_t _proc_write_callback(struct file *file, const char __user *buffer, size_t count, loff_t *data);

void _update_timer_handler(unsigned long data);
static void update_work(struct work_struct *work);

/* Linked list item for each registered process */

struct proc_item
{
    // Data fields
    int pid;
    unsigned long cpu_use;
    
    // Linked list member
    struct list_head list;
};

// Init a list
LIST_HEAD(proc_list);

/* I/O of the ProcFS */

static const struct file_operations cpu_proc_fops = {
    .owner = THIS_MODULE,
    .open = _proc_open_callback, // first operation perform on a file
    .read = seq_read,
    .write = _proc_write_callback,
    // .llseek = seq_lseek,
    // .release = single_release,
};

// Read the 'status' entry, function called when /proc file is cat or opened
static int _proc_open_callback(struct inode *inode, struct file *file)
{
    return single_open(file, _proc_show_callback, NULL); // perform a function call on a file
}
/* print all process pid and cpu_time */
static int _proc_show_callback(struct seq_file *sf, void *v)
{
    // Iterate the proc_list,
    //  print to the console "[PID]: [cpu_use]" for each registered process
    struct proc_item *cur; // linkedlist
    list_for_each_entry(cur, &proc_list, list)
    {
        seq_printf(sf, "%d: %lu\n", cur->pid, cur->cpu_use); // same as seq_file, used in show function
    }
    
    return 0;
}

// Write the 'status' entry, this happens when a process writes to /proc file, then this function creates a new node and add to linkedlist
static ssize_t _proc_write_callback(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
    // Allocate a new node
    struct proc_item *new = (struct proc_item *)kmalloc(sizeof(struct proc_item), GFP_KERNEL);
    
    // Restrict procfs_buffer_size
    procfs_buffer_size = count;
    if (procfs_buffer_size > PROCFS_MAX_SIZE)
    {
        procfs_buffer_size = PROCFS_MAX_SIZE;
    }
    
    // Get input (PID) from user space
    if (copy_from_user(procfs_buffer, buffer, procfs_buffer_size)) // return 0 on success
    {
        return -EFAULT;
    }
    
    // Initialize the node item for the input PID
    kstrtoint(procfs_buffer, 0, &new->pid); // str(procfs_buffer) to int(pid)
    new->cpu_use = 0;
    INIT_LIST_HEAD(&new->list);
    
    // Register the new item to the proc_list
    list_add(&new->list, &proc_list);
    
    return procfs_buffer_size;
}

/* Periodic timer per 5s */

// Step 1:
// Timer handler in TOP HALF, when the timer expires (interrupt), recursion: add a new timer when a timer expires
void _update_timer_handler(unsigned long data)
{
    // Create a work to do update_work, in heap since one workqueue for a process
    struct work_struct *work = (struct work_struct *)kmalloc(sizeof(struct work_struct), GFP_KERNEL);
    INIT_WORK(work, update_work);
    
    // Enqueue the work immediately for later execution
    queue_work(update_workqueue, work);
    
    // Restart the timer
    mod_timer(&update_timer, jiffies + 5 * HZ);
}

/* Workqueue for timer handler to defer the cpu_use updates */

// Step 2:
// Work function in BOTTOM HALF, when the work is dequeued to execute by executor
static void update_work(struct work_struct *work)
{
    struct proc_item *cur, *temp;
    
    printk(KERN_INFO "This is from work function\n");
    
    /*======= critical section =========*/
    // The works in workqueue are asynchronous,
    //  so locking for the proc_list is necessary
    //  in case multiple works are updating the list items simultaneously
    spin_lock(&list_lock);    

    // Update each item
    list_for_each_entry_safe(cur, temp, &proc_list, list)
    {
        /* get_cpu_use updates cpu_use */
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

// Init module
static int __init _cpu_proc_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE LOADING\n");
    #endif

    // Create 'mp1' dir
    mp1 = proc_mkdir(DIRECTORY, NULL);

    // Create 'status' file
    status = proc_create(FILENAME, 0666, mp1, &cpu_proc_fops);
    
    // Create timer of 5s
    init_timer(&update_timer);
    update_timer.expires = jiffies + 5 * HZ; // time to run
    update_timer.function = _update_timer_handler; // function to run
    
    // Start the timer with arg above
    add_timer(&update_timer);
    
    // Init work queue
    update_workqueue = create_workqueue("update_workqueue");
    
    // Init spin lock
    spin_lock_init(&list_lock);
    
    printk(KERN_ALERT "MP1 MODULE LOADED\n");
    return 0;    
}

// Exit module
static void __exit _cpu_proc_exit(void)
{
    struct proc_item *cur, *temp;
    
    #ifdef DEBUG
    printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
    #endif
    
    // Compelete works and delete the queue
    flush_workqueue(update_workqueue);
    destroy_workqueue(update_workqueue);
    
    // Delete the timer
    del_timer_sync(&update_timer);
    
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
module_init(_cpu_proc_init);
module_exit(_cpu_proc_exit);
