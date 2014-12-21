obj-m := intercept.o
KDIR := /lib/modules/`uname -r`/build
PWD := `pwd`

default:
	make -C $(KDIR) M=$(PWD) modules
	gcc test.c -o test.o
clean: 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean