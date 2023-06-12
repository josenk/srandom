#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>             /* For kmalloc */
#include <linux/gfp.h>
#include <linux/vmalloc.h>          /* For vmalloc */
#include <linux/uaccess.h>          /* For copy_to_user */
#include <linux/miscdevice.h>       /* For misc_register (the /dev/srandom) device */
#include <linux/random.h>           /* For inital seed */
#include <linux/proc_fs.h>          /* For /proc filesystem */
#include <linux/seq_file.h>         /* For seq_print */
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include "chacha.h"                 /* For chacha */

#define DRIVER_AUTHOR "Jonathan Senkerik <josenk@jintegrate.co>"
#define DRIVER_DESC   "Improved random number generator."
#define ULTRA_HIGH_SPEED_MODE 1     /* Set to 0 for Chacha8 mode, set to 1 to enable Ultra High Speed Mode (XorShift) */
#define SDEVICE_NAME "srandom"      /* Dev name as it appears in /proc/devices */
#define APP_VERSION "2.0.0"
#define numberOfRndArrays  64       /* Number of 512b Array. do not change */
#define rndArraySize 67             /* Size of Array.  Must be >= 65. */
#define THREAD_SLEEP_VALUE 601      /* Amount of time in seconds, the background thread should sleep between each operation. */
#define PAID 0


//#define DEBUG_CONNECTIONS 0
//#define DEBUG_READ 0
//#define DEBUG_WRITE 0
//#define DEBUG_UPDATE_ARRAYS 0
//#define DEBUG_SHUFFLE 0
//#define DEBUG_THREAD 0
//#define DEBUG_CHACHA 0

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
    #define COPY_TO_USER raw_copy_to_user
    #define COPY_FROM_USER raw_copy_from_user
#else
    #define COPY_TO_USER copy_to_user
    #define COPY_FROM_USER copy_from_user
#endif

/*
 * Copyright (C) 2015 Jonathan Senkerik
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Prototypes
 */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t sdevice_read(struct file *, char *, size_t, loff_t *);
static ssize_t sdevice_write(struct file *, const char *, size_t, loff_t *);
static uint64_t wyhash64(void);
static uint64_t wyhash64_2(void);
static uint64_t xoroshiro256(void);
static inline uint64_t rotl(uint64_t, int);

static void update_sarray(int);
static uint8_t get_next_buffer(void);
static int proc_read(struct seq_file *m, void *v);
static int proc_open(struct inode *inode, struct  file *file);
static void shuffle_sarray(int);
static uint64_t swapInt64(uint64_t);
static uint64_t reverseInt64(uint64_t);
static int work_thread(void *data);


/*
 * Global variables are declared as static, so are global within the file.
 */
static struct file_operations sfops = {
        .owner   = THIS_MODULE,
        .open    = device_open,
        .read    = sdevice_read,
        .write   = sdevice_write,
        .release = device_release
};

static struct miscdevice srandom_dev = {
        MISC_DYNAMIC_MINOR,
        "srandom",
        &sfops
};


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)
static struct proc_ops proc_fops={
      .proc_open = proc_open,
      .proc_release = single_release,
      .proc_read = seq_read,
      .proc_lseek = seq_lseek
};
#else
static const struct file_operations proc_fops = {
        .owner   = THIS_MODULE,
        .read    = seq_read,
        .open    = proc_open,
        .llseek  = seq_lseek,
        .release = single_release,
};
#endif


static struct mutex UpArr_mutex;
static struct mutex Open_mutex;
static struct mutex ArrBusy_mutex;
static struct chacha_context ctx;
static struct task_struct *kthread;


/*
 * Global variables
 */
uint64_t wyhash64_x;                      /* x for wyhash64 */
uint64_t wyhash64_x2;                     /* x for wyhash64 in-module use only */
uint64_t xoroshiro_s[4];                  /* s for xoroshiro256** */
uint8_t chacha_key[32];
uint8_t chacha_nonce[12];
uint64_t chacha_counter =0;
uint64_t (*prngArrays)[rndArraySize];     /* Array of Array of SECURE RND numbers */
int8_t ArraysBusyFlags[rndArraySize];     /* Binary Flags for Busy Arrays */


/*
 * Global counters
 */
int16_t  sdevOpenCurrent;          /* srandom device current open count */
int32_t  sdevOpenTotal;            /* srandom device total open count */
uint64_t generatedCount;           /* Total generated (512byte) */


/*
 * This function is called when the module is loaded
 */
int mod_init(void)
{
        int16_t C,buffer_id;

        sdevOpenCurrent = 0;
        sdevOpenTotal   = 0;
        generatedCount  = 0;

        mutex_init(&UpArr_mutex);
        mutex_init(&Open_mutex);
        mutex_init(&ArrBusy_mutex);

        /*
         * Register char device
         */
        if (misc_register(&srandom_dev))
                printk(KERN_INFO "[srandom] mod_init /dev/srandom driver registion failed..\n");
        else
                printk(KERN_INFO "[srandom] mod_init /dev/srandom driver registered..\n");

        /*
         * Create /proc/srandom
         */
        // if (! proc_create("srandom", 0, NULL, &proc_fops))
        if (! proc_create("srandom", 0, NULL, &proc_fops))
                printk(KERN_INFO "[srandom] mod_init /proc/srandom registion failed..\n");
        else
                printk(KERN_INFO "[srandom] mod_init /proc/srandom registion regisered..\n");

        printk(KERN_INFO "[srandom] mod_init Module version         : "APP_VERSION"\n");
        if (PAID == 0) {
                printk(KERN_INFO "-----------------------:----------------------\n");
                printk(KERN_INFO "Please support my work and efforts contributing\n");
                printk(KERN_INFO "to the Linux community.  A $25 payment per\n");
                printk(KERN_INFO "server would be highly appreciated.\n");
        }
        printk(KERN_INFO "-----------------------:----------------------\n");
        printk(KERN_INFO "Author                 : Jonathan Senkerik\n");
        printk(KERN_INFO "Website                : https://www.jintegrate.co\n");
        printk(KERN_INFO "github                 : https://github.com/josenk/srandom\n");
        if (PAID == 0) {
                printk(KERN_INFO "Paypal                 : josenk@jintegrate.co\n");
                printk(KERN_INFO "Bitcoin                : 1MTNg7SqcEWs5uwLKwNiAfYqBfnKFJu65p\n");
                printk(KERN_INFO "Commercial Invoice     : Avail on request.\n");
        }


        prngArrays = kmalloc((numberOfRndArrays + 1) * rndArraySize * sizeof(uint64_t), GFP_KERNEL);
        while (!prngArrays) {
                printk(KERN_INFO "[srandom] mod_init kmalloc failed to allocate initial memory.  retrying...\n");
                prngArrays = kmalloc((numberOfRndArrays + 1) * rndArraySize * sizeof(uint64_t), GFP_KERNEL);
        }

        //  Seed everything
        get_random_bytes(&wyhash64_x, sizeof(uint64_t));
        get_random_bytes(&wyhash64_x2, sizeof(uint64_t));
        get_random_bytes(&xoroshiro_s[0], sizeof(uint64_t));
        get_random_bytes(&xoroshiro_s[1], sizeof(uint64_t));
        get_random_bytes(&xoroshiro_s[2], sizeof(uint64_t));
        get_random_bytes(&xoroshiro_s[3], sizeof(uint64_t));

        chacha_init_context(&ctx, chacha_key, chacha_nonce, chacha_counter);

        /*
         * Init the sarray
         */
        for (buffer_id = 0;buffer_id <= numberOfRndArrays ;buffer_id++) {
                for (C = 0;C <= rndArraySize;C++) {
                        prngArrays[buffer_id][C] = wyhash64() ^ xoroshiro256();
                }
                update_sarray(buffer_id);
        }

        kthread = kthread_create(work_thread, NULL, "srandom-kthread");
        wake_up_process(kthread);

        return 0;
}

/*
 * This function is called when the module is unloaded
 */
void mod_exit(void)
{
        kthread_stop(kthread);

        misc_deregister(&srandom_dev);

        remove_proc_entry("srandom", NULL);

        printk(KERN_INFO "[srandom] mod_exit srandom deregisered..\n");
}


/*
 * This function is called when a process tries to open the device file. "dd if=/dev/srandom"
 */
static int device_open(struct inode *inode, struct file *file)
{
        while (mutex_lock_interruptible(&Open_mutex));

        sdevOpenCurrent++;
        sdevOpenTotal++;
        mutex_unlock(&Open_mutex);

        #ifdef DEBUG_CONNECTIONS
        printk(KERN_INFO "[srandom] device_open (current open) :%d\n",sdevOpenCurrent);
        printk(KERN_INFO "[srandom] device_open (total open)   :%d\n",sdevOpenTotal);
        #endif

        return 0;
}


/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
        while (mutex_lock_interruptible(&Open_mutex));

        sdevOpenCurrent--;
        mutex_unlock(&Open_mutex);

        #ifdef DEBUG_CONNECTIONS
        printk(KERN_INFO "[srandom] device_release (current open) :%d\n", sdevOpenCurrent);
        #endif

        return 0;
}

/*
 * Called when a process reads from the device.
 */
static ssize_t sdevice_read(struct file * file, char * buf, size_t requestedCount, loff_t *ppos)
{
        int Block, ret;
        uint8_t buffer_id;
        char *new_buf;                 /* Buffer to hold numbers to send */
        bool isVMalloc = 0;


        #ifdef DEBUG_READ
        printk(KERN_INFO "[srandom] sdevice_read requestedCount:%zu\n", requestedCount);
        #endif


        new_buf = kmalloc((requestedCount + 512) * sizeof(uint8_t), GFP_KERNEL|__GFP_NOWARN);
        while (!new_buf) {
                #ifdef DEBUG_READ
                printk(KERN_INFO "[srandom] using vmalloc to allocate large blocksize.\n");
                #endif

                isVMalloc = 1;
                new_buf = vmalloc((requestedCount + 512) * sizeof(uint8_t));
        }

        for (Block = 0; Block <= (requestedCount / 512); Block++) {
                buffer_id = get_next_buffer();
                generatedCount++;

                /*
                 * Fill new_buf from a prngArrays block until requestedCount is met.
                 */
                #ifdef DEBUG_READ
                printk(KERN_INFO "[srandom] Block:%u buffer_id:%d\n", Block, buffer_id);
                #endif

                memcpy(new_buf + (Block * 512), prngArrays[buffer_id], 512);
                
                #if ULTRA_HIGH_SPEED_MODE
                // UHS mode will update the prngArrays block with new values for next request.
                update_sarray(buffer_id);
                #endif

                /*
                 * Clear ArraysBusyFlags
                 */
                if (mutex_lock_interruptible(&ArrBusy_mutex))
                        return -ERESTARTSYS;
                ArraysBusyFlags[buffer_id] = 0;
                mutex_unlock(&ArrBusy_mutex);
        }

        //  Use Chacha to cipher new_buf
        #if ! ULTRA_HIGH_SPEED_MODE
        //printk(KERN_INFO "[srandom] preChaCha 0:%d last:%d\n", (uint8_t)new_buf[0], (uint8_t)new_buf[sizeof(new_buf) -1]);

        chacha_xor(&ctx, new_buf, requestedCount);
        chacha_counter += requestedCount;

        //printk(KERN_INFO "[srandom] postChaCha 0:%d last:%d\n", (uint8_t)new_buf[0], (uint8_t)new_buf[sizeof(new_buf) -1]);
        #endif

        /*
         * Send new_buf to device
         */
        ret = COPY_TO_USER(buf, new_buf, requestedCount);

        /*
         * Free allocated memory
         */
        if (isVMalloc) {
                vfree(new_buf);
        } else {
                kfree(new_buf);
        }


        /*
         * return how many chars we sent
         */
        return requestedCount;
}


/*
 * Called when someone tries to write to /dev/srandom device
 */
static ssize_t sdevice_write(struct file *file, const char __user *buf, size_t receivedCount, loff_t *ppos)
{

        char *newdata;
        int result;

        #ifdef DEBUG_CONNECTIONS
        printk(KERN_INFO "[srandom] sdevice_write receivedCount:%zu\n", receivedCount);
        #endif

        /*
         * Allocate memory to read from device
         */
        newdata = kmalloc(receivedCount, GFP_KERNEL);
        while (!newdata) {
                newdata = kmalloc(receivedCount, GFP_KERNEL);
        }

        result = COPY_FROM_USER(newdata, buf, receivedCount);

        /*
         * Free memory
         */
        kfree(newdata);

        #ifdef DEBUG_WRITE
        printk(KERN_INFO "[srandom] sdevice_write COPY_FROM_USER receivedCount:%zu \n", receivedCount);
        #endif

        return receivedCount;
}


/*
 *  Get the next available buffer
 */
uint8_t get_next_buffer(void) {
        uint8_t next;

        next = (uint8_t)wyhash64_2() >> 2;

        while (mutex_lock_interruptible(&ArrBusy_mutex));
        while (ArraysBusyFlags[next] != 0) {
                next += 1;
                if (next >= numberOfRndArrays) {
                        next = 0;
                }
        }

        ArraysBusyFlags[next] = 1;
        mutex_unlock(&ArrBusy_mutex);

        return next;
}


void update_sarray(int buffer_id) {
        int16_t C;
        int64_t X[2], Z[2], temp;
        int8_t mixer;

        mixer = (uint8_t)wyhash64_2();
        if ((mixer & 1) == 1) {
                Z[0] = wyhash64();
        } else {
                Z[0] = xoroshiro256();
        }

        if ((mixer & 2) == 2) {
                Z[1] = wyhash64();
        } else {
                Z[1] = xoroshiro256();
        }

        /*
         * This must run exclusivly
         */
        while (mutex_lock_interruptible(&UpArr_mutex));

        for (C = 0; C < (rndArraySize -4); C = C + 4) {
                mixer = (uint8_t)wyhash64_2();
                X[0]  = wyhash64();
                X[1]  = wyhash64();
                temp                         = prngArrays[buffer_id][C];
                prngArrays[buffer_id][C]     = prngArrays[buffer_id][C + 1] ^ X[(mixer & 1) == 1] ^ Z[(mixer & 16) == 16];
                prngArrays[buffer_id][C + 1] = prngArrays[buffer_id][C + 2] ^ X[(mixer & 2) == 2] ^ Z[(mixer & 32) == 32];
                prngArrays[buffer_id][C + 2] = prngArrays[buffer_id][C + 3] ^ X[(mixer & 4) == 4] ^ Z[(mixer & 64) == 64];
                prngArrays[buffer_id][C + 3] = temp                         ^ X[(mixer & 8) == 8] ^ Z[(mixer & 128) == 128];
        }

        shuffle_sarray(buffer_id);

        mutex_unlock(&UpArr_mutex);

        #ifdef DEBUG_UPDATE_ARRAYS
        printk(KERN_INFO "[srandom] update_sarray buffer_id:%d, X:%llu, Y:%llu, Z1:%llu, Z2:%llu, Z3:%llu,\n", buffer_id, X, Y, Z1, Z2, Z3);
        #endif
}


/*
 * Shuffle the sarray
 */
inline void shuffle_sarray(int buffer_id)
{
        uint64_t temp;
        uint16_t mixer = (uint16_t)wyhash64_2();
        uint8_t mixtype = (mixer & 448) >> 7;
        uint8_t istart = (mixer & 56) >> 4;
        uint8_t increment = (mixer & 3) + 1;
        int i;
        

        #ifdef DEBUG_SHUFFLE
        printk(KERN_INFO "[srandom] shuffle_sarray istart: %d, increment: %d, buffer_id:%d, first:%llu, last:%llu\n", istart, increment, buffer_id, prngArrays[buffer_id][0], prngArrays[buffer_id][rndArraySize-1]);
        #endif



        for(i = istart; i<rndArraySize/2; i = i + increment){
            if (mixtype == 0) {
                                temp = prngArrays[buffer_id][i];
                if ((mixer & 64) == 64) {
                        prngArrays[buffer_id][i] = swapInt64(prngArrays[buffer_id][rndArraySize-i-1]);
                } else {
                        prngArrays[buffer_id][i] = prngArrays[buffer_id][rndArraySize-i-1];
                }
                if ((mixer & 128) == 128) {
                        prngArrays[buffer_id][rndArraySize-i-1] = temp;
                } else {
                        prngArrays[buffer_id][rndArraySize-i-1] = reverseInt64(temp);
                }

            } else if (mixtype == 1) {
                prngArrays[buffer_id][i] = ((prngArrays[buffer_id][i] & 0xFFFFFFFF00000000ULL) >> 32) | ((prngArrays[buffer_id][i] & 0x00000000FFFFFFFFULL) << 32);
                prngArrays[buffer_id][rndArraySize-i-1] = ((prngArrays[buffer_id][rndArraySize-i-1] & 0xFFFFFFFF00000000ULL) >> 32) | ((prngArrays[buffer_id][rndArraySize-i-1] & 0x00000000FFFFFFFFULL) << 32);

            } else if (mixtype == 2) {
                prngArrays[buffer_id][i] = ((prngArrays[buffer_id][i] & 0xFFFF0000FFFF0000ULL) >> 16) | ((prngArrays[buffer_id][i] & 0x0000FFFF0000FFFFULL) << 16);
                prngArrays[buffer_id][rndArraySize-i-1] = ((prngArrays[buffer_id][rndArraySize-i-1] & 0xFFFF0000FFFF0000ULL) >> 16) | ((prngArrays[buffer_id][rndArraySize-i-1] & 0x0000FFFF0000FFFFULL) << 16);

            } else if (mixtype == 3) {
                prngArrays[buffer_id][i] = ((prngArrays[buffer_id][i] & 0xFF00FF00FF00FF00ULL) >> 8) | ((prngArrays[buffer_id][i] & 0x00FF00FF00FF00FFULL) << 8);
                prngArrays[buffer_id][rndArraySize-i-1] = ((prngArrays[buffer_id][rndArraySize-i-1] & 0xFF00FF00FF00FF00ULL) >> 8) | ((prngArrays[buffer_id][rndArraySize-i-1] & 0x00FF00FF00FF00FFULL) << 8);;
                
            }

        }
}


/*
 * PRNG functions
 */
//https://lemire.me/blog/2019/03/19/the-fastest-conventional-random-number-generator-that-can-pass-big-crush/
uint64_t wyhash64(void) {
        __uint128_t tmp;
        uint64_t m1;
        uint64_t m2;

        wyhash64_x += 0x60bee2bee120fc15;

        tmp = (__uint128_t) wyhash64_x * 0xa3b195354a39b70d;
        m1 = (tmp >> 64) ^ tmp;
        tmp = (__uint128_t)m1 * 0x1b03738712fad5c9;
        m2 = (tmp >> 64) ^ tmp;
        return m2;
}

// wyhash64 for in-module instance
uint64_t wyhash64_2(void) {
        __uint128_t tmp;
        uint64_t m1;
        uint64_t m2;

        wyhash64_x2 += 0x60bee2bee120fc15;

        tmp = (__uint128_t) wyhash64_x2 * 0xa3b195354a39b70d;
        m1 = (tmp >> 64) ^ tmp;
        tmp = (__uint128_t)m1 * 0x1b03738712fad5c9;
        m2 = (tmp >> 64) ^ tmp;
        return m2;
}

// https://prng.di.unimi.it/
uint64_t xoroshiro256(void) {
        const uint64_t result = rotl(xoroshiro_s[1] * 5, 7) * 9;

        const uint64_t t = xoroshiro_s[1] << 17;

        xoroshiro_s[2] ^= xoroshiro_s[0];
        xoroshiro_s[3] ^= xoroshiro_s[1];
        xoroshiro_s[1] ^= xoroshiro_s[2];
        xoroshiro_s[0] ^= xoroshiro_s[3];

        xoroshiro_s[2] ^= t;

        xoroshiro_s[3] = rotl(xoroshiro_s[3], 45);

        return result;
}
inline uint64_t rotl(const uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
}


//Swap a 64-bit integer
#define SWAPINT64(x) ( \
   (((uint64_t)(x) & 0x00000000000000FFULL) << 56) | \
   (((uint64_t)(x) & 0x000000000000FF00ULL) << 40) | \
   (((uint64_t)(x) & 0x0000000000FF0000ULL) << 24) | \
   (((uint64_t)(x) & 0x00000000FF000000ULL) << 8) | \
   (((uint64_t)(x) & 0x000000FF00000000ULL) >> 8) | \
   (((uint64_t)(x) & 0x0000FF0000000000ULL) >> 24) | \
   (((uint64_t)(x) & 0x00FF000000000000ULL) >> 40) | \
   (((uint64_t)(x) & 0xFF00000000000000ULL) >> 56))
inline uint64_t swapInt64(uint64_t x)
{
    return SWAPINT64(x);
}

inline uint64_t reverseInt64(uint64_t value) {
    value = ((value & 0xFFFFFFFF00000000ULL) >> 32) | ((value & 0x00000000FFFFFFFFULL) << 32);
    value = ((value & 0xFFFF0000FFFF0000ULL) >> 16) | ((value & 0x0000FFFF0000FFFFULL) << 16);
    value = ((value & 0xFF00FF00FF00FF00ULL) >> 8) | ((value & 0x00FF00FF00FF00FFULL) << 8);
    value = ((value & 0xF0F0F0F0F0F0F0F0ULL) >> 4) | ((value & 0x0F0F0F0F0F0F0F0FULL) << 4);
    value = ((value & 0xCCCCCCCCCCCCCCCCULL) >> 2) | ((value & 0x3333333333333333ULL) << 2);
    value = ((value & 0xAAAAAAAAAAAAAAAAULL) >> 1) | ((value & 0x5555555555555555ULL) << 1);

    return value;
}


/*
 *  The Kernel thread refreshing the arrays.
 */
int work_thread(void *data)
{
        int buffer_id = 0;

        while (!kthread_should_stop()) {

                msleep_interruptible(THREAD_SLEEP_VALUE * 1000);
                
                buffer_id ++;
                if (buffer_id == numberOfRndArrays) {
                        buffer_id = 0;
                }

                update_sarray(buffer_id);

                #ifdef DEBUG_THREAD
                printk(KERN_INFO "[srandom] work_thread buffer_id:%d\n", buffer_id);
                #endif

        }

        return 0;
 }



/*
 * This function is called when reading /proc filesystem
 */
int proc_read(struct seq_file *m, void *v)
{
        seq_printf(m, "-----------------------:----------------------\n");
        seq_printf(m, "Device                 : /dev/"SDEVICE_NAME"\n");
        #if ULTRA_HIGH_SPEED_MODE
                seq_printf(m, "Module version         : "APP_VERSION" UHS (XorShift)\n");
        #else
                seq_printf(m, "Module version         : "APP_VERSION" ChaCha\n");
        #endif
        seq_printf(m, "Current open count     : %d\n",sdevOpenCurrent);
        seq_printf(m, "Total open count       : %d\n",sdevOpenTotal);
        seq_printf(m, "Total K bytes          : %llu\n",generatedCount / 2);
        if (PAID == 0) {
                seq_printf(m, "-----------------------:----------------------\n");
                seq_printf(m, "Please support my work and efforts contributing\n");
                seq_printf(m, "to the Linux community.  A $25 payment per\n");
                seq_printf(m, "server would be highly appreciated.\n");
        }
        seq_printf(m, "-----------------------:----------------------\n");
        seq_printf(m, "Author                 : Jonathan Senkerik\n");
        seq_printf(m, "Website                : https://www.jintegrate.co\n");
        seq_printf(m, "github                 : https://github.com/josenk/srandom\n");
        if (PAID == 0) {
                seq_printf(m, "Paypal                 : josenk@jintegrate.co\n");
                seq_printf(m, "Bitcoin                : 1GEtkAm97DphwJbJTPyywv6NbqJKLMtDzA\n");
                seq_printf(m, "Commercial Invoice     : Avail on request.\n");
        }
        return 0;
}


int proc_open(struct inode *inode, struct  file *file)
{
        return single_open(file, proc_read, NULL);
}


/*
 *  ChaCha
 *  Adapted from: https://github.com/Ginurx/chacha20-c
 */
static uint32_t rotl32(uint32_t x, int n) 
{
        return (x << n) | (x >> (32 - n));
}

static uint32_t pack4(const uint8_t *a)
{
        uint32_t res = 0;
        res |= (uint32_t)a[0] << 0 * 8;
        res |= (uint32_t)a[1] << 1 * 8;
        res |= (uint32_t)a[2] << 2 * 8;
        res |= (uint32_t)a[3] << 3 * 8;
        return res;
}

static void chacha_init_block(struct chacha_context *ctx, uint8_t key[], uint8_t nonce[])
{
        const uint8_t *magic_constant = (uint8_t*)"expand 32-byte k";

        memcpy(ctx->key, key, sizeof(ctx->key));
        memcpy(ctx->nonce, nonce, sizeof(ctx->nonce));

        ctx->state[0] = pack4(magic_constant + 0 * 4);
        ctx->state[1] = pack4(magic_constant + 1 * 4);
        ctx->state[2] = pack4(magic_constant + 2 * 4);
        ctx->state[3] = pack4(magic_constant + 3 * 4);
        ctx->state[4] = pack4(key + 0 * 4);
        ctx->state[5] = pack4(key + 1 * 4);
        ctx->state[6] = pack4(key + 2 * 4);
        ctx->state[7] = pack4(key + 3 * 4);
        ctx->state[8] = pack4(key + 4 * 4);
        ctx->state[9] = pack4(key + 5 * 4);
        ctx->state[10] = pack4(key + 6 * 4);
        ctx->state[11] = pack4(key + 7 * 4);
        // 64 bit counter initialized to zero by default.
        ctx->state[12] = 0;
        ctx->state[13] = pack4(nonce + 0 * 4);
        ctx->state[14] = pack4(nonce + 1 * 4);
        ctx->state[15] = pack4(nonce + 2 * 4);

        memcpy(ctx->nonce, nonce, sizeof(ctx->nonce));
}

static void chacha_block_set_counter(struct chacha_context *ctx, uint64_t counter)
{
        ctx->state[12] = (uint32_t)counter;
        ctx->state[13] = pack4(ctx->nonce + 0 * 4) + (uint32_t)(counter >> 32);
}

static void chacha_block_next(struct chacha_context *ctx) {
        uint32_t *counter = ctx->state + 12;
        int i;

        // This is where the crazy voodoo magic happens.
        // Mix the bytes a lot and hope that nobody finds out how to undo it.
        for (i = 0; i < 16; i++) ctx->keystream32[i] = ctx->state[i];

#define CHACHA_QUARTERROUND(x, a, b, c, d) \
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16); \
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12); \
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8); \
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);

        for (i = 0; i < 4; i++) 
        {
                CHACHA_QUARTERROUND(ctx->keystream32, 0, 4, 8, 12)
                CHACHA_QUARTERROUND(ctx->keystream32, 1, 5, 9, 13)
                CHACHA_QUARTERROUND(ctx->keystream32, 2, 6, 10, 14)
                CHACHA_QUARTERROUND(ctx->keystream32, 3, 7, 11, 15)
                CHACHA_QUARTERROUND(ctx->keystream32, 0, 5, 10, 15)
                CHACHA_QUARTERROUND(ctx->keystream32, 1, 6, 11, 12)
                CHACHA_QUARTERROUND(ctx->keystream32, 2, 7, 8, 13)
                CHACHA_QUARTERROUND(ctx->keystream32, 3, 4, 9, 14)
        }

        for (i = 0; i < 16; i++) ctx->keystream32[i] += ctx->state[i];

        
        // increment counter
        counter[0]++;
        if (0 == counter[0]) 
        {
                // wrap around occured, increment higher 32 bits of counter
                counter[1]++;
                // Limited to 2^64 blocks of 64 bytes each.
                // If you want to process more than 1180591620717411303424 bytes (1.6 PB)
                // you have other problems.
                // We could keep counting with counter[2] and counter[3] (nonce),
                // but then we risk reusing the nonce which is very bad.
                //assert(0 != counter[1]);
        }
}

void chacha_init_context(struct chacha_context *ctx, uint8_t key[], uint8_t nonce[], uint64_t counter)
{
        memset(ctx, 0, sizeof(struct chacha_context));

        chacha_init_block(ctx, key, nonce);
        chacha_block_set_counter(ctx, counter);

        ctx->counter = counter;
        ctx->position = 64;
}

void chacha_xor(struct chacha_context *ctx, uint8_t *bytes, size_t n_bytes)
{
        uint8_t *keystream8 = (uint8_t*)ctx->keystream32;
        size_t i;
        for (i = 0; i < n_bytes; i++) 
        {
                if (ctx->position >= 64) 
                {
                        chacha_block_next(ctx);
                        ctx->position = 0;
                }
                bytes[i] ^= keystream8[ctx->position];
                ctx->position++;
        }
}


module_init(mod_init);
module_exit(mod_exit);


/*
 *  Module license information
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
