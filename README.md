intercept
=========

Intercept a system call using a loadable kernel module.

Consider this readme file as a tutorial to intercepting a system call using loadable kernel modules

STEPS TO FOLLOW:
Loadable Kernel Modules (LKM) are simply extensions to the basic kernel in your operating system that may be loaded/unloaded on the fly, without a need to recompile the kernel or reboot the system.
	Open up a text editor of your choice and start with writing a simple loadable kernel module by 
 	adding two header files:
   #include <linux/version.h> //this adds required headers for versioning
   #include <linux/module.h>  //macros for module development

For writing any kernel module we need to implement initialization and cleanup routines. Those modules can be implemented as follows:
	static int __init init_my_module(void)	  //initialise the kernel module
{
	printk(KERN_INFO "Inside kernel space\n");
	return 0;
}

static void __exit cleanup_my_module(void)	//cleanup the module on unloading
{
		printk(KERN_INFO "Exiting kernel space\n");
	return;
}

module_init(init_my_module);		//macro to identify initialiser
module_exit(cleanup_my_module);	//macro to identify cleanup routine
MODULE_LICENSE("GPL");	//crucial macro for availability of some kernel functions

As a final step towards making a kernel module we need to write a Makefile with the following contents before we compile and load the kernel module.
obj-m := intercept.o	     //assume we have named our module as intercept here (intercept.c)
KDIR := /lib/modules/`uname -r`/build	//location of current kernel tree
PWD := `pwd`	// present working directory
default:
	make -C $(KDIR) M=$(PWD) modules

	i) Save the Makefile and execute make command. Among the many files created there is one  
               named intercept.ko which is our loadable kernel module.
ii) Load the module by executing sudo insmod intercept.ko and check the output of lsmod 
    command to check the details of all the modules loaded in kernel (intercept should be 
    present in it if we have loaded the module successfully)
iii) Now unload the module by executing sudo rmmod intercept and then execute dmesg | 
     tail to check the output of printk() statements in kernel ring.

Now that we are done with a simple kernel module development let us move ahead with writing a simple miscellaneous character device. Add the following header to you source file (intercept.c):
			#include <linux/miscdevice.h>  //important struct miscdevice is declared here
			#include <linux/fs.h>  //struct file_operations declared here

Now add global variables and device handler functions to the code.
	i) int in_use = 0; //set to 1 in open handler and reset to 0 in release handler
	ii)  /*Device open handler*/
static int my_open(struct inode *inode, struct file *file)
{
			/*Do not allow multiple processes to open this device*/
	if(in_use)
				return -EBUSY;
	in_use++;
		printk("MyDevice opened\n");
	return 0;
}
iii)  /* This function, in turn, will be called when a process closes our device */
static int my_release(struct inode *inode, struct file *file)
{
			in_use--;
	printk("MyDevice closed\n");
			return 0;
}
	iv)  /*This static function handles ioctl calls performed on MyDevice*/
static int my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	// This part needs to filled later as shown in the same tutorial
	return retval;
}
Now we are ready to populate the two most important data structures, file_operations and miscdevice, which determine the working of MyDevice.

	i) //populate data struct for file operations
static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = &my_open,
	.release = &my_release,
	.unlocked_ioctl = (void*)&my_ioctl,
	.compat_ioctl = (void*)&my_ioctl
};

	ii) //populate miscdevice data structure
static struct miscdevice my_device = {
	MISC_DYNAMIC_MINOR,  //assigns random minor number to MyDevice
	"MyDevice",
	&my_fops
};

As a final step for device driver writing:
	i) Add int retval = misc_register(&my_device); to init_my_module() and change return 0; 
   to return retval;
ii) Add misc_deregister(&my_device); to the cleanup subroutine.

Finally execute make command again and load the module using insmod. Now to check whether our device has been successfully registered execute the command as follows:
ls -l /dev/MyDevice
If the output is like crw------- 1 root root 10, 56 Dec  9 23:43 /dev/MyDevice , it means our 
device has been successfully registered with read and write permission granted only for the 
root user which we can change by executing chmod a+r+w /dev/MyDevice. Now you may 
open the device for  I/O via open() system call. Here numbers 10 and 56 denote major and 
minor device numbers respectively.

For the third and final part we again start by adding two header files:
		#include <linux/highmem.h>
#include <asm/unistd.h>
The first one is needed due to the fact that system call table is located in read only memory area in modern kernels and we will have to modify the protection attributes of the memory page containing the address of the system call that we want to intercept. The second one is needed for implementing modified system calls.

In kernel 2.6 and above sys_call_table is not an exported symbol so we need to hard code the sys_call_table address. Get that by copying the address from the output of:
sudo cat /boot/System.map-3.13.0-40-generic | grep sys_call_table
	Store in a variable unsigned long *sys_call_table = (unsigned long*)0xffffffff81801400; 
which should be declared as global.


Now we define two global values, which would be used as argument to our_ioctl function. One will tell us to patch the table, another one will tell us to fix it by restoring the original value.
/* IOCTL commands */
#define IOCTL_PATCH_TABLE 0x00000001
#define IOCTL_FIX_table   0x00000004
Add variable int is_set=0; to use as a flag to distinguish between real & modified system call.
Add following lines to implement the modified open() system call.

//function pointer to original sys_open system call
asmlinkage int (*real_open)(const char* __user, int, int);

//Replacement of original call with modified system call
asmlinkage int custom_open(const char* __user file_name, int flags, int mode)
{
			printk("interceptor: open(\"%s\", %X, %X)\n", file_name,flags,mode);
	return real_open(file_name,flags,mode);
}

Another couple of crucial functions is the set that will allow us to modify the memory page protection attributes directly.
		/*Make the page write protected*/
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

Now in the final stage we need to implement the my_ioctl() function, so add the following lines to it: 
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

Add the following lines to cleanup module to restore pointers in sys_call_table.
		if (is_set)
	{
			make_rw((unsigned long)sys_call_table);
			*(sys_call_table + __NR_open) = (unsigned long)real_open;
		make_ro((unsigned long)sys_call_table);
		}

After adding all the above given snippets save intercept.c, execute make and load the kernel module. Now test the driver using the following code and checking the output of dmesg | tail in terminal.
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Define ioctl commands */
#define IOCTL_PATCH_TABLE 0x00000001
#define IOCTL_FIX_TABLE   0x00000004

int main(void)
{
   int device = open("/dev/MyDevice", O_RDWR);
   printf("%d\n",device);
   ioctl(device, IOCTL_PATCH_TABLE);
   sleep(2);
   ioctl(device, IOCTL_FIX_TABLE);
   close(device);
   return 0;
}
This brings us to the end of intercepting as system call via a kernel module
