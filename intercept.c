#include <linux/version.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <asm/unistd.h>

MODULE_LICENSE("GPL");

// IOCTL commands
#define IOCTL_PATCH_TABLE 0x00000001
#define IOCTL_FIX_TABLE 0x00000004

//Global variables
int in_use = 0;	//set to 1 in open handler and reset to zero in release handler
int is_set = 0; // flag to detect system call interception
unsigned long *sys_call_table = (unsigned long*)0xffffffff81801400; //hard coded address of sys_call_table from /boot/System.map

//function pointer to original sys_open
asmlinkage int (*real_open)(const char* __user, int, int);

//Replacement of original call with modified system call
asmlinkage int custom_open(const char* __user file_name, int flags, int mode)
{
	printk("interceptor: open(\"%s\", %X, %X)\n", file_name,flags,mode);
	return real_open(file_name,flags,mode);
}

/*
Make the memory page writable
This is little risky as directly arch level protection bit is changed
*/
int make_rw(unsigned long address)
{
	unsigned int level;
	pte_t *pte = lookup_address(address, &level);
	if(pte->pte &~ _PAGE_RW)
		pte->pte |= _PAGE_RW;
	return 0;
}

/* Make the page write protected */
int make_ro(unsigned long address)
{
	unsigned int level;
	pte_t *pte = lookup_address(address, &level);
	pte->pte = pte->pte &~ _PAGE_RW;
	return 0;
}

/* This function will be invoked each time a user process attempts
       to open my device. You should keep in mind that the prototype
      of this function may change along different kernel versions. */
static int my_open(struct inode *inode, struct file *file)
{
	/*Do not allow multiple processes to open this device*/
	if(in_use)
		return -EBUSY;
	in_use++;
	printk("MyDevice opened\n");
	return 0;
}

/* This function, in turn, will be called when a process closes our device */
static int my_release(struct inode *inode, struct file *file)
{
	in_use--;
	printk("MyDevice closed\n");
	return 0;
}

/*This static function handles ioctl calls performed on MyDevice*/
static int my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	switch(cmd)
   {
      case IOCTL_PATCH_TABLE:
         make_rw((unsigned long)sys_call_table);
         real_open = (void*)*(sys_call_table + __NR_open);
         *(sys_call_table + __NR_open) = (unsigned long)custom_open;
         make_ro((unsigned long)sys_call_table);
         is_set=1;
         break;
      case IOCTL_FIX_TABLE:
         make_rw((unsigned long)sys_call_table);
         *(sys_call_table + __NR_open) = (unsigned long)real_open;
         make_ro((unsigned long)sys_call_table);
         is_set=0;
         break;
      default:
         printk("sys_open not executed\n");
         break;
   }

	return retval;
}

//populate data struct for file operations
static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = &my_open,
	.release = &my_release,
	.unlocked_ioctl = (void*)&my_ioctl,
	.compat_ioctl = (void*)&my_ioctl
};

//populate miscdevice data structure
static struct miscdevice my_device = {
	MISC_DYNAMIC_MINOR,
	"MyDevice",
	&my_fops
};

static int __init init_my_module(void)
{
	int retval;
	printk(KERN_INFO "Inside kernel space\n");
	retval = misc_register(&my_device);
	return retval;
}

static void __exit cleanup_my_module(void)
{
	if (is_set)
	{
		make_rw((unsigned long)sys_call_table);
		*(sys_call_table + __NR_open) = (unsigned long)real_open;
		make_ro((unsigned long)sys_call_table);
	}
	
	misc_deregister(&my_device);
	printk(KERN_INFO "Exiting kernel space\n");
	return;
}

module_init(init_my_module);
module_exit(cleanup_my_module);