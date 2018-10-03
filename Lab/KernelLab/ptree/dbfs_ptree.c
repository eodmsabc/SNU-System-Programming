#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define MYSIZE 1024

typedef struct debugfs_blob_wrapper sdbw;

MODULE_LICENSE("GPL");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;

//struct debugfs_blob_wrapper *dbw;
sdbw *dbw;
char *fstr;

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
	int i;
	char temp[MYSIZE];
	pid_t input_pid;
	sscanf(user_buffer, "%u", &input_pid);
	curr = pid_task(find_vpid(input_pid), PIDTYPE_PID);
	if (curr == NULL) {
		printk("PID Error\n");
		return length;
	}
	for(i = 0; i < MYSIZE; i++) fstr[i] = 0;

	while(curr) {
		sprintf(temp, "%s (%d)\n%s", curr->comm, curr->pid, fstr);
		strcpy(fstr, temp);
		if (curr->pid == 1) {
			break;
		}
		else {
			curr = curr -> real_parent;
		}
	}

	return length;
}

static const struct file_operations dbfs_fops = {
	.write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{
	dir = debugfs_create_dir("ptree", NULL);
	
	if (!dir) {
		printk("Cannot create ptree dir\n");
		return -1;
	}
	fstr = kmalloc(MYSIZE * sizeof(char), GFP_KERNEL);
	dbw = (sdbw *)kmalloc(sizeof(sdbw), GFP_KERNEL);
	dbw->data = (void *) fstr;
	dbw->size = (unsigned long) MYSIZE * sizeof(char);
	inputdir = debugfs_create_file("input", 0644, dir, NULL, &dbfs_fops);
	ptreedir = debugfs_create_blob("ptree", 0444, dir, dbw);
	// Find suitable debugfs API

	return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);
	kfree(dbw);
	kfree(fstr);
	// Implement exit module code
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
