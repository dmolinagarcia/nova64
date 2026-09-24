# HOWTO — usar la librería FAT32

## La idea en pocas palabras

Una tarjeta SD, vista desde dentro, es una lista enorme de **bloques** de
512 bytes numerados desde 0. No sabe nada de ficheros ni de carpetas.

**FAT32** es un acuerdo sobre cómo organizar esos bloques para que haya
ficheros y carpetas. Unos bloques dicen cómo es la tarjeta. Una tabla (la
*FAT*) dice qué bloques van detrás de cuáles. Otros bloques guardan los
nombres de cada carpeta y dónde empieza cada fichero.

**Esta librería hace de traductora.** Tú le pides «dame `/docs/leeme.txt`»
y ella averigua qué bloques hay que leer y en qué orden, y te devuelve el
contenido. Lo único que no sabe hacer es hablar con el hardware, y eso lo
pones tú: una función que lea bloques.

```
   tu shell            ls, cat, cd...
      │
      ▼
   librería FAT32      nombres → números de bloque        (fat32/)
      │
      ▼
   bdev_t              "lee N bloques desde el bloque X"  (common/bdev.h)
      │
      ▼
   tu driver de SD     el que ya tienes
```

La librería solo **lee**. Nunca escribe en la tarjeta.

## Paso 1 — enchufar tu lectura de la SD

La librería pide los bloques siempre de la misma forma: «lee `nsec` bloques
a partir del bloque `lba` y déjalos en `buf`». Escribes una función con esa
forma que llame a tu driver, y la guardas en un `bdev_t`.

Supongamos que tu driver tiene una función que lee **un** bloque:

```c
int mi_sd_leer_bloque(unsigned long bloque, unsigned char *destino);  /* 0 = bien */
```

El adaptador queda así:

```c
#include "bdev.h"
#include "fat32_fs.h"

static int sd_read(bdev_t *bd, uint32_t lba, void *buf, uint32_t nsec)
{
    uint8_t *p = (uint8_t *)buf;

    (void)bd;
    while (nsec > 0) {                  /* a veces pide varios bloques seguidos */
        if (mi_sd_leer_bloque(lba, p) != 0)
            return BDEV_EIO;            /* la tarjeta falló */
        lba++;
        p += 512;
        nsec--;
    }
    return BDEV_OK;
}

static bdev_t   sd;     /* static: empieza a ceros, que es lo que hace falta */
static fat_fs_t fs;     /* el volumen montado: no lo pongas en la pila */

fs_err_t montar_sd(uint32_t bloques_de_la_tarjeta)
{
    sd.read = sd_read;
    sd.sector_size = 512;
    sd.sector_count = bloques_de_la_tarjeta;   /* tamaño de la tarjeta en bloques */
    return fat_mount_auto(&fs, &sd);           /* FS_OK si todo fue bien */
}
```

Tres detalles:

- **`sector_count`** es el tamaño de la tarjeta en bloques. Tu driver lo
  sabe (lo da el registro CSD de la SD). La librería lo usa para no pedir
  nunca un bloque que no existe.
- **`fat_mount_auto`** encuentra el volumen solo: la tarjeta entera, o la
  primera partición FAT32 si tiene tabla de particiones. Es lo normal en
  una SD formateada en un PC.
- Si tu driver sabe leer varios bloques de golpe, úsalo dentro de
  `sd_read` en vez del bucle y irá más rápido.

## Paso 2 — compilar

Compila estos ficheros junto con tu programa:

```
common/bdev.c  common/fs_err.c  common/mbr.c
fat32/fat32_ondisk.c  fat32/fat32_fat.c  fat32/fat32_lfn.c
fat32/fat32_dir.c  fat32/fat32_file.c  fat32/fat32_fs.c  fat32/fat32_attr.c
```

Usa estas opciones para una máquina pequeña:

```
-Icommon -Ifat32
-DNOVAFS_FREESTANDING       no usa stdio ni ficheros del PC
-DFAT_CFG_MAX_SECTOR=512u   buffers de 512 bytes, lo que usa una SD
-DFAT_CFG_FAT_CACHE=2u      caché pequeña para la tabla FAT
-DFAT_CFG_NAME_MAX=255u     nombres de hasta 255 bytes
```

Así, `fat_fs_t` ocupa unos 1,6 KB. Hacen falta un compilador de C99 (por
`stdint.h` y `static inline`) y algo menos de 1 KB de pila en las funciones
que buscan por nombre.

## Paso 3 — tu shell

Tu shell llama a la librería en cada orden. Al arrancar, monta la tarjeta
**una vez** con `montar_sd(...)`. Después, cada orden es una llamada o un
bucle corto. Las rutas son texto normal, como `/docs/leeme.txt`, y la
librería las recorre sola. Entiende `.`, `..` y nombres largos, y no
distingue mayúsculas en letras normales.

El shell guarda la carpeta actual como texto, y las rutas relativas se
pegan a ella:

```c
static char cwd[128] = "/";

static const char *completa(const char *ruta)       /* "docs" → "/docs" */
{
    static char buf[256];

    if (ruta[0] == '/')
        return ruta;
    snprintf(buf, sizeof buf, "%s%s%s", cwd,
             cwd[strlen(cwd) - 1] == '/' ? "" : "/", ruta);
    return buf;
}
```

**`ls`**: abrir la carpeta e ir pidiendo entradas hasta que no haya más.

```c
void cmd_ls(const char *ruta)
{
    static fat_dir_t    d;
    static fat_dirent_t e;
    fs_err_t err = fat_opendir(&fs, completa(ruta), &d);

    while (err == FS_OK && (err = fat_dir_read(&d, &e)) == FS_OK)
        printf("%10lu  %s%s\n", (unsigned long)e.node.size, e.name,
               fat_node_is_dir(&e.node) ? "/" : "");
    if (err != FS_ENOENT)                   /* FS_ENOENT = "ya no hay más" */
        printf("ls: %s\n", fs_strerror(err));
}
```

**`cat`**: abrir el fichero y leerlo a trozos hasta que devuelva 0 bytes.

```c
void cmd_cat(const char *ruta)
{
    static fat_file_t f;
    static uint8_t    buf[512];
    uint32_t n;
    fs_err_t err = fat_open(&fs, completa(ruta), &f);

    while (err == FS_OK && (err = fat_file_read(&f, buf, sizeof buf, &n)) == FS_OK && n > 0)
        fwrite(buf, 1, n, stdout);          /* o tu forma de imprimir */
    if (err != FS_OK)
        printf("cat: %s\n", fs_strerror(err));
}
```

**`cd`**: comprobar que la ruta existe y es una carpeta, y guardarla.

```c
void cmd_cd(const char *ruta)
{
    fat_node_t n;
    const char *nueva = completa(ruta);
    fs_err_t err = fat_stat(&fs, nueva, &n);

    if (err == FS_OK && !fat_node_is_dir(&n))
        err = FS_ENOTDIR;
    if (err != FS_OK)
        printf("cd: %s\n", fs_strerror(err));
    else if (strlen(nueva) < sizeof cwd)
        strcpy(cwd, nueva);
}
```

Con `cd ..` el texto crece (`/docs/..`), pero la librería lo resuelve bien.
Si quieres que se vea limpio, quita el último trozo del texto tú mismo.

Todas las funciones de este paso se han compilado y probado contra una
tarjeta de prueba, con las opciones del paso 2.

## Si algo va mal

- Cada función devuelve un **código de error** (`FS_OK`, `FS_ENOENT`,
  `FS_ENOTDIR`...), y `fs_strerror(err)` te lo da en texto.
- Si la tarjeta está **dañada**, la librería no se cuelga ni se inventa
  datos: devuelve `FS_ECORRUPT` y avisa de qué ha visto. Para ver ese aviso
  en tu consola, dale una función al arrancar:

  ```c
  static void avisa(const char *que) { printf("SD dañada: %s\n", que); }
  ...
  fs_set_corrupt_logger(avisa);
  ```

- Una tarjeta **exFAT** se rechaza con `FS_ENOTSUP`. Las de más de 32 GB
  (SDXC) vienen así de fábrica: hay que formatearlas en FAT32.

## En el PC: `fat32tool`

Para mirar una tarjeta desde el PC no hace falta escribir nada. Copia la
tarjeta a un fichero de imagen y usa la herramienta:

```sh
sudo dd if=/dev/sdX of=tarjeta.img bs=1M     # sdX = tu lector de tarjetas
make                                         # dentro de novafs/
build/fat32tool tarjeta.img ls /
build/fat32tool tarjeta.img cat /docs/leeme.txt
build/fat32tool tarjeta.img tree             # todo, con un CRC de cada fichero
```

## La otra puerta: la tabla `fsops`

Lo anterior usa la librería **por rutas**, que es lo más sencillo para un
shell. Existe también una puerta **por números de inodo**, la tabla `fsops`
(`common/fsops.h`, `fat32/fat32_fsops.h`). Es la forma en que la VFS de la
hoja Y1 hablará con cada sistema de ficheros cuando haya varios a la vez
(NVFS, ext2...). Por esa puerta, quien llama recorre la ruta trozo a trozo
con `lookup`. `build/fat32tool` la usa así; mira su función `resolve()`.
Para un shell que solo lee una SD FAT32, la puerta por rutas es suficiente.

## Ten en cuenta

- Según la decisión D98, FAT32 vive en el PC y no en la máquina: el sistema
  de ficheros propio es NVFS y el de intercambio es ext2. Esto sirve para
  prototipos y pruebas, no como sistema definitivo.
- La librería se ha comprobado con un compilador de `int` de 16 bits
  (`make check16`), pero todavía no se ha compilado con Calypsi ni se ha
  ejecutado en un 65816.
