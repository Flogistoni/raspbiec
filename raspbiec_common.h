#ifndef RASPBIEC_COMMON_H
#define RASPBIEC_COMMON_H

/* Drive states */
#define DEV_IDLE   0
#define DEV_LISTEN 1
#define DEV_TALK   2

/* Bus status and command codes */
#define IEC_OK                       0

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

#endif /* RASPBIEC_COMMON_H */
