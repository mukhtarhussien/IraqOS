CC = clang
LD = ld.lld

CFLAGS = -ffreestanding -nostdlib -Wall -I.. -I../Kernel -I../Kernel/Process
LDFLAGS = -T linker.ld

all: boot.elf

boot.o: boot.c
	$(CC) $(CFLAGS) -c boot.c -o boot.o

start.o: start.S
	$(CC) $(CFLAGS) -c start.S -o start.o

kernel.o: ../Kernel/kernel.c
	$(CC) $(CFLAGS) -c ../Kernel/kernel.c -o kernel.o

printk.o: ../Kernel/printk.c
	$(CC) $(CFLAGS) -c ../Kernel/printk.c -o printk.o

uart.o: ../Drivers/uart.c
	$(CC) $(CFLAGS) -c ../Drivers/uart.c -o uart.o

shell.o: ../Shell/shell.c
	$(CC) $(CFLAGS) -c ../Shell/shell.c -o shell.o

gui.o: ../Shell/gui.c ../Drivers/display_size.h
	$(CC) $(CFLAGS) -c ../Shell/gui.c -o gui.o

memory.o: ../Memory/memory.c
	$(CC) $(CFLAGS) -c ../Memory/memory.c -o memory.o

process.o: ../Kernel/Process/process.c
	$(CC) $(CFLAGS) -c ../Kernel/Process/process.c -o process.o

context.o: ../Kernel/Process/context.S
	$(CC) $(CFLAGS) -c ../Kernel/Process/context.S -o context.o

console.o: ../Drivers/console.c ../Drivers/display_size.h
	$(CC) $(CFLAGS) -c ../Drivers/console.c -o console.o

virtio_gpu.o: ../Drivers/virtio_gpu.c ../Drivers/display_size.h
	$(CC) $(CFLAGS) -c ../Drivers/virtio_gpu.c -o virtio_gpu.o

virtio_input.o: ../Drivers/virtio_input.c
	$(CC) $(CFLAGS) -c ../Drivers/virtio_input.c -o virtio_input.o

scheduler.o:
	$(CC) $(CFLAGS) -c ../Drivers/scheduler.c -o scheduler.o

timer.o: ../Kernel/timer.c
	$(CC) $(CFLAGS) -c ../Kernel/timer.c -o timer.o

irq.o: ../Kernel/irq.c
	$(CC) $(CFLAGS) -c ../Kernel/irq.c -o irq.o

gic.o: ../Kernel/gic.c
	$(CC) $(CFLAGS) -c ../Kernel/gic.c -o gic.o

exceptions.o: ../Kernel/exceptions.S
	$(CC) $(CFLAGS) -c ../Kernel/exceptions.S -o exceptions.o

boot.elf: start.o boot.o kernel.o printk.o uart.o shell.o gui.o memory.o process.o context.o console.o virtio_gpu.o virtio_input.o scheduler.o timer.o irq.o exceptions.o gic.o
	$(LD) $(LDFLAGS) start.o boot.o kernel.o printk.o uart.o shell.o gui.o memory.o process.o context.o console.o virtio_gpu.o virtio_input.o scheduler.o timer.o irq.o exceptions.o gic.o -o boot.elf
clean:
	rm -f *.o boot.elf boot.bin

