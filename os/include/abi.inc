; abi.inc — everything a program, the kernel and the BIOS must agree on.

; ---- one error space (Y1.15, LB1) -------------------------------------------
E_OK            = 0
E_IO            = $10000 - 1
E_NOENT         = $10000 - 2
E_EXIST         = $10000 - 3
E_NOTDIR        = $10000 - 4
E_ISDIR         = $10000 - 5
E_NOTEMPTY      = $10000 - 6
E_INVAL         = $10000 - 7
E_NOSPC         = $10000 - 8
E_NOFILE        = $10000 - 9
E_ROFS          = $10000 - 10
E_BUSY          = $10000 - 11
E_LOOP          = $10000 - 12
E_NAMETOOLONG   = $10000 - 13
E_MFILE         = $10000 - 14
E_BADF          = $10000 - 15
E_XDEV          = $10000 - 16
E_CORRUPT       = $10000 - 17
E_FEATURE       = $10000 - 18
E_NOTSUP        = $10000 - 19
E_FBIG          = $10000 - 20
E_NOTMOUNTED    = $10000 - 21
E_NOMEM         = $10000 - 22
E_FAULT         = $10000 - 23
E_NOEXEC        = $10000 - 24
E_CHILD         = $10000 - 25
E_SRCH          = $10000 - 26
E_NOTTY         = $10000 - 27
E_PIPE          = $10000 - 28
E_NOSYS         = $10000 - 29
E_2BIG          = $10000 - 30
E_LAST          = 30

; ---- open flags (LB1.19) ----------------------------------------------------
NV_O_RDONLY     = $0000
NV_O_WRONLY     = $0001
NV_O_RDWR       = $0002
NV_O_ACCMODE    = $0003
NV_O_CREAT      = $0100
NV_O_EXCL       = $0200
NV_O_TRUNC      = $0400
NV_O_APPEND     = $0800
NV_SEEK_SET     = 0
NV_SEEK_CUR     = 1
NV_SEEK_END     = 2

; ---- struct nv_stat = vattr_t (DN-FS-VFS-001 5.3), 32 bytes -----------------
ST_MODE         = 0
ST_NLINK        = 2
ST_SIZE         = 4
ST_BLOCKS       = 8             ; 512-byte units
ST_ATIME        = 12
ST_MTIME        = 16
ST_CTIME        = 20
ST_FILE_ID      = 24
ST_UID          = 28
ST_GID          = 30
ST_SIZEOF       = 32
S_IFMT          = $F000
S_IFDIR         = $4000
S_IFREG         = $8000
S_IFLNK         = $A000
S_IFCHR         = $2000

; ---- struct nv_dirent = dirent_out_t, 262 bytes -----------------------------
DE_FILE_ID      = 0
DE_TYPE         = 4             ; vtype_t
DE_NAMELEN      = 5
DE_NAME         = 6             ; NUL-terminated
DE_SIZEOF       = 262
VREG            = 1
VDIR            = 2
VLNK            = 3
VCHR            = 4

; ---- tty ioctls (LB1, CN1.13) -----------------------------------------------
TTY_GETMODE     = $5401         ; arg -> u16 mode
TTY_SETMODE     = $5402         ; arg -> u16 mode
TTY_GETSIZE     = $5403         ; arg -> u16 columns, u16 rows
TTY_CLEAR       = $5404
TTY_SETFG       = $5405
TTY_MODE_CANON  = 0
TTY_MODE_RAW    = 1

PATH_MAX        = 256           ; including the terminator
NAME_MAX        = 56            ; Y2.4

; ---- the system information block (PROVISIONAL encoding: Q168) --------------
SIB_MAGIC       = $00           ; 'N' 'S' 'I' 'B'
SIB_VERSION     = $04           ; u16, this layout is 1
SIB_SIZE        = $06           ; u16
SIB_CPU         = $08           ; u16: 0 W65C816S, otherwise the Argon signature
SIB_PLATFORM    = $0A           ; u16
SIB_REGMAP      = $0C           ; u16 register-map version
SIB_CAPS        = $0E           ; u16 Helium capabilities
SIB_RAM_FRAMES  = $10           ; u32, 2 KB frames
SIB_COLS        = $14           ; u16 text columns
SIB_ROWS        = $16           ; u16 text rows
SIB_VM_GATE     = $18           ; u32, 0 = no NVM32 gate
SIB_BOOT_SRC    = $1C           ; u16: 1 staged by the Debug Agent, 2 the card
SIB_DEVICES     = $1E           ; u16: b0 Neon · b1 HID queue · b2 SD block
SIB_BOOT_LBA    = $20           ; u32 first sector of the boot partition
SIB_BOOT_PART   = $24           ; u16 its partition number, 1-4
SIB_BATTERY     = $26           ; u16, $FFFF unknown
SIB_RTC         = $28           ; u32 seconds since 1970, 0: no clock (Q88)
SIB_BIOS_VER    = $2C           ; u16
SIB_SIZEOF      = $30

; ---- the native executable (PROVISIONAL encoding: N.5, Q20) -----------------
; Fixed virtual addresses, no relocation, one entry per segment. The kernel
; image in the boot partition is one of these (CN1.5).
NX_MAGIC        = $00           ; $7F 'N' '6' '4'
NX_VERSION      = $04           ; u16 = 1
NX_NSEG         = $06           ; u16
NX_ENTRY        = $08           ; u32
NX_STACK        = $0C           ; u32 stack size
NX_FILESIZE     = $10           ; u32 whole file
NX_CRC          = $14           ; u32 CRC-32 of the whole file with this field zero
NX_FLAGS        = $18           ; u16: b0 kernel image
NX_SEGS         = $20           ; then nseg entries of NXS_SIZEOF
NXS_VADDR       = 0             ; u32
NXS_FOFF        = 4             ; u32 offset of its bytes in the file
NXS_FILESZ      = 8             ; u32
NXS_MEMSZ       = 12            ; u32, >= filesz; the rest is zero-filled
NXS_FLAGS       = 16            ; u16: 1 R · 2 W · 4 X
NXS_SIZEOF      = 20
