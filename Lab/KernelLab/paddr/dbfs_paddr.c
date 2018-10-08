#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

struct packet {
	pid_t pid;
	unsigned long vaddr;
	unsigned long paddr;
};

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
	struct packet *pbuf = (struct packet *)user_buffer;
	pid_t pid = pbuf->pid;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long vad = pbuf->vaddr;
	unsigned long ppn = 0, ppo = 0;

	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	
	pgd = pgd_offset(task->mm, vad);
	pud = pud_offset(pgd, vad);
	pmd = pmd_offset(pud, vad);
	pte = pte_offset_kernel(pmd,vad);

	ppn = pte_pfn(*pte) << PAGE_SHIFT;
	ppo = vad & (~PAGE_MASK);
//	printk(KERN_EMERG"p : %lx\n", pte_val(*pte));
	pbuf->paddr = ppn | ppo;

	return 0;
}

static const struct file_operations dbfs_fops = {
	.read = read_output,
};

static int __init dbfs_module_init(void)
{

	dir = debugfs_create_dir("paddr", NULL);

	if (!dir) {
		printk("Cannot create paddr dir\n");
		return -1;
	}

	output = debugfs_create_file("output", 0444, dir, NULL, &dbfs_fops);

	return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
