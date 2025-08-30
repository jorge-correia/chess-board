#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#define CHESS_MAGIC 'c'
#define CHESS_IOCTL_READ_REG _IOR (CHESS_MAGIC, 0, int*)
#define CHESS_IOCTL_WRITE_REG _IOW (CHESS_MAGIC, 1, int*)
int main ()
{
        int reg = 123;
        int ret;
        int chess_fd = open ("/dev/chess-chrdev", O_RDWR);

        reg = 7;
        if (ret = ioctl (chess_fd, CHESS_IOCTL_WRITE_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }
        

        if (ret = ioctl (chess_fd, CHESS_IOCTL_READ_REG, &reg))
        {
                printf ("ioctl returned error %d\n", ret);
                exit (1);
        }

      //  printf ("reg after CHESS_IOCTL_REG_REG: %d\n", reg);

       




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
