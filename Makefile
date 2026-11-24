.RECIPEPREFIX = >
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -fcf-protection=none -nostdlib -O2 -Wall

all: kernel.bin

boot.o: boot.asm
> nasm -f elf32 boot.asm -o boot.o

kernel.o: kernel.c
> gcc $(CFLAGS) -c kernel.c -o kernel.o

kernel.bin: boot.o kernel.o linker.ld
> ld -m elf_i386 -T linker.ld -o kernel.bin boot.o kernel.o

run: kernel.bin
> qemu-system-i386 -kernel kernel.bin

run-term: kernel.bin
> qemu-system-i386 -kernel kernel.bin -display curses

clean:
> rm -f *.o kernel.bin
