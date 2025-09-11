#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>

#define CHESS_MAGIC 'c'
#define CHESS_IOCTL_READ_REG _IOR (CHESS_MAGIC, 0, int*)
#define CHESS_IOCTL_WRITE_REG _IOW (CHESS_MAGIC, 1, int*)
int main ()
{
        int reg = 123;
        int ret;
        int chess_fd = open ("/dev/chess-chrdev", O_RDWR);

        uint8_t *ptr = mmap (0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
                        chess_fd, 0);

        printf ("fd %d, ptr: %llx\n",chess_fd, ptr);


        reg = 7;
        if (ret = ioctl (chess_fd, CHESS_IOCTL_WRITE_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }

        sleep (2);
        printf ("first char %c\n", *ptr);
        
        *(ptr + 7) = 'H';

        
        sleep (2);
        

        if (ret = ioctl (chess_fd, CHESS_IOCTL_READ_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }

      //  printf ("reg after CHESS_IOCTL_REG_REG: %d\n", reg);


        sleep (2);

        reg = 7;
        if (ret = ioctl (chess_fd, CHESS_IOCTL_WRITE_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }

        /*
        reg = 123;
        if (ret = ioctl (chess_fd, CHESS_IOCTL_READ_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }

        printf ("reg after CHESS_IOCTL_REG_REG: %d\n", reg);
        */
        return 0;
}
