#ifndef RASPBIEC_COMMON_H
#define RASPBIEC_COMMON_H

/* Drive states */
#define DEV_IDLE   0
#define DEV_LISTEN 1
#define DEV_TALK   2

/* Bus status and command codes */
#define IEC_OK                       0

#define IEC_COMMAND_RANGE_START   -0x100

#define IEC_CLEAR_ERROR           -0x100
#define IEC_LAST_BYTE_NEXT        -0x101 /* Next byte last when transmitting */
#define IEC_EOI                   -0x102 /* Last byte when receiving */
#define IEC_BUS_IDLE              -0x111
#define IEC_ASSERT_ATN            -0x1A1
#define IEC_DEASSERT_ATN          -0x1A0
#define IEC_TURNAROUND            -0x1A2

#define IEC_IDENTITY_COMPUTER     -0x164
#define IEC_IDENTITY_DRIVE(dev)    (-(0x1E0|((dev)&0x1f))) /* 1E0..1FF */
#define IEC_DRIVE_DEVICE(byte)     ((-(byte))&0x1f)
#define IEC_IDENTITY_IS_DRIVE(byte) (((-(byte))&~0x1f)==0x1E0)

#define IEC_COMMAND_RANGE_END     -0x1FF

#define IEC_ILLEGAL_DEVICE_NUMBER -0x200
#define IEC_MISSING_FILENAME      -0x201
#define IEC_FILE_NOT_FOUND        -0x202
#define IEC_WRITE_TIMEOUT         -0x203
#define IEC_READ_TIMEOUT          -0x204
#define IEC_DEVICE_NOT_PRESENT    -0x205
#define IEC_ILLEGAL_STATE         -0x206
#define IEC_GENERAL_ERROR         -0x207
#define IEC_PREV_BYTE_HAS_ERROR   -0x208
#define IEC_FILE_EXISTS           -0x209
#define IEC_DRIVER_NOT_PRESENT    -0x210
#define IEC_OUT_OF_MEMORY         -0x211
#define IEC_UNKNOWN_MODE          -0x212
#define IEC_SIGNAL                -0x213
#define IEC_BUS_NOT_IDLE          -0x214
#define IEC_SAVE_ERROR            -0x215
#define IEC_UNKNOWN_DISK_IMAGE    -0x216
#define IEC_ILLEGAL_TRACK_SECTOR  -0x217
#define IEC_DISK_IMAGE_ERROR      -0x218
#define IEC_NO_SPACE_LEFT_ON_DEVICE -0x219
#define IEC_FILE_READ_ERROR       -0x220
#define IEC_FILE_WRITE_ERROR      -0x221

#define CMD_LISTEN(device)  (0x20|(device))
#define CMD_IS_LISTEN(byte) ((0xe0&(byte))==0x20)
#define CMD_UNLISTEN        (0x3f)
#define CMD_TALK(device)    (0x40|(device))
#define CMD_IS_TALK(byte)   ((0xe0&(byte))==0x40)
#define CMD_UNTALK          (0x5f)
#define CMD_DATA(secaddr)   (0x60|(secaddr))
#define CMD_CLOSE(secaddr)  (0xe0|(secaddr))
#define CMD_OPEN(secaddr)   (0xf0|(secaddr))
#define CMD_IS_DATA(byte)   ((0xf0&(byte))==0x60)
#define CMD_IS_CLOSE(byte)  ((0xf0&(byte))==0xe0)
#define CMD_IS_OPEN(byte)   ((0xf0&(byte))==0xf0)
#define CMD_IS_DATA_CLOSE_OPEN(byte) ((0x60&(byte))==0x60)

#define CMD_GET1ST(byte)    ((byte)&0xe0)
#define CMD_GETDEV(byte)    ((byte)&0x1f)
#define CMD_GET2ND(byte)    ((byte)&0xf0)
#define CMD_GETSEC(byte)    ((byte)&0x0f)

/* Some names for character constants used in the code */
enum petscii_codes
{
    PETSCII_CR    = 0x0D,
    PETSCII_SPC   = 0x20,
    PETSCII_ET    = 0x26,
    PETSCII_COMMA = 0x2C,
    PETSCII_MINUS = 0x2D,
    PETSCII_COLON = 0x3A,
    PETSCII_a     = 0x41,
    PETSCII_b     = 0x42,
    PETSCII_c     = 0x43,
    PETSCII_d     = 0x44,
    PETSCII_e     = 0x45,
    PETSCII_f     = 0x46,
    PETSCII_i     = 0x49,
    PETSCII_m     = 0x4D,
    PETSCII_n     = 0x4E,
    PETSCII_p     = 0x50,
    PETSCII_r     = 0x52,
    PETSCII_s     = 0x53,
    PETSCII_u     = 0x55,
    PETSCII_v     = 0x56,
    PETSCII_w     = 0x57,
};

#endif /* RASPBIEC_COMMON_H */
