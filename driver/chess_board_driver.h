#ifndef CHESS_BOARD_DRIVER_H
#define CHESS_BOARD_DRIVER_H

#include <linux/ioctl.h>

#define CHESS_BOARD_DEV_ID 0xdead
#define CHESS_BOARD_CHRDEV_NAME "chess-chrdev"
#define CHESS_BOARD_DEVREGION_NAME "chess-devregion"
#define CHESS_BOARD_DEVCLASS_NAME "chess-devclass"
#define CHESS_BAR 0

#define CHESS_MAGIC 'c'
#define CHESS_IOCTL_READ_REG _IOR (CHESS_MAGIC, 0, int*)
#define CHESS_IOCTL_WRITE_REG _IOW (CHESS_MAGIC, 1, int*)
#define QEMU_VENDOR_ID 0x1234


#endif
