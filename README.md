Introduction
------------
srandom is a Linux kernel module that can be used like the built-in /dev/urandom & /dev/random device files.  In the standard mode (ChaCha8) it is about 20%-40% faster than built-in urandom and should be secure enough for crypto.  In ultra high speed mode it is about 500% faster (5x faster) than the built-in urandom generator.  YMWV depending on hardware and software configuration.  It should compile and install on any Linux 3.17+ kernel.  Both ChaCha8 and (UHS)XorShift pass all the randomness tests using dieharder.

Version 2.1.0 introduces some performance improvements including the upgraded Xoshiro256++ PRNG algorithm, per-buffer locking for better multi-core performance, atomic operations for reduced mutex overhead, and enhanced shuffle algorithms with 6 different mixing types.

srandom was created as a performance improvement to the built-in PRNG /dev/urandom number generator.  I wanted a much faster random number generator to wipe ssd disks.  The UHS algorithm is many times faster than urandom, but still produces excellent random numbers that dieharder finds indistinguishable from true random numbers.  You can easily wipe multiple SSDs at the same time.

The built-in __PRNG__ generators (/dev/random and /dev/urandom) use Chacha20 and are technically not flawed.  They are just very slow when compared to srandom.  __/dev/random and /dev/urandom are BOTH PRNG generators.__  
  > https://www.2uo.de/myths-about-urandom/
  ```
  Truth is, when state-of-the-art hash algorithms are broken, or when state-of-the-art block ciphers are broken, it doesn't matter that you get “philosophically insecure” random numbers because of them. You've got nothing left to securely use them for anyway.
  ```

What makes srandom a great PRNG generator?
  * Higher speed vs the built-in random devices.
  * It's configurable to use ChaCha8 or UHS(XorShift) depending on your use-case.
  * ChaCha8 and UHS both pass Dieharder tests.
  * Plug and play.


Why do I need this?
-------------------
 * Use standard mode (ChaCha8) for any security type applications that rely heavily on random numbers.  For example, Apache SSL (Secure Socket Level), PGP (Pretty Good Privacy), VPN (Virtual Private Networks).  All types of Encryption, Password seeds, Tokens would rely on a source of random numbers.  There are many examples at https://www.random.org/testimonials/.

 * Use UHS for disk wiping or any application that does not involve crypto, or if you're not overly concerned about random numbers affecting security.


Compile and installation
------------------------
To build & compile the kernel module.  A pre-req is "kernel-devel".  Use yum or apt to install.

    make

To load the kernel module into the running kernel (temporary).

    make load

To unload the kernel module from the running kernel.

    make unload

To install the kernel module on your system (persistent on reboot).

    make install ; make load

To uninstall the kernel module from your system.

    make uninstall


Kernel Module Signing (Required for Secure Boot)
------------------------------------------------
If your system has Secure Boot enabled or kernel module signature verification enforced, you'll need to sign the module before loading it. The build process automatically detects if signing is required and attempts to sign the module.

### Automatic Signing
The Makefile will automatically attempt to sign the module during `make` if it detects that your kernel requires signed modules. It looks for existing signing keys in common locations.

### Generate New Signing Keys
If no signing keys are found, generate new ones:

    make generate-keys

This creates signing keys in `/usr/src/kernel-keys/` and generates both PEM and DER certificate formats.

### Loading Signed Modules
After building and signing, load the module normally:

    make load

If the load fails due to signature issues, the Makefile will provide specific solutions based on your system configuration.

### Secure Boot Systems
For systems with Secure Boot enabled, you have several options:

1. **Import to Machine Owner Key (MOK) database** (recommended):
   ```
   make mok-import
   ```
   Then reboot and enroll the key when prompted by the MOK manager.

2. **Import to kernel keyring**:
   ```
   make import-key
   ```

3. **Temporarily disable signature enforcement**:
   ```
   sudo sysctl kernel.module_sig_enforce=0
   make load
   ```

### Troubleshooting Signature Issues
- Check if signature verification failed: `dmesg | tail -10`
- Force load (bypasses signature check): `make force-load`
- Verify keys exist: `ls -la /usr/src/kernel-keys/`

Note: All signing operations require root privileges. Use `sudo make` when building on systems with signing enforcement.


Technical Details
-----------------

### PRNG Algorithms

**srandom v2.1.0** uses a combination of high-quality PRNG algorithms:

- **wyhash64**: Fast 64-bit PRNG using 128-bit multiplication, passes BigCrush tests
- **Xoshiro256++**: State-of-the-art 256-bit state PRNG from prng.di.unimi.it, successor to xoroshiro with improved performance
- **LCG Fast**: Linear congruential generator for internal high-speed operations
- **ChaCha8**: Stream cipher used for additional entropy mixing in standard mode

### Enhanced Shuffle Algorithm

The array shuffling system now includes **6 different mixing types** for maximum entropy:

0. **Element swapping** with optional byte swapping and bit reversal
1. **32-bit word swapping** within 64-bit values  
2. **16-bit word swapping** for finer granularity
3. **8-bit byte swapping** for maximum bit diffusion
4. **Rotation-based mixing** using left/right bit rotations (NEW)
5. **XOR with rotated values** creating non-linear dependencies (NEW)

Each shuffle operation randomly selects one of these 6 methods, ensuring unpredictable mixing patterns.

### Performance Optimizations (v2.1.0)

**Per-Buffer Locking**: Replaced global mutex with per-buffer mutexes, allowing parallel updates of different random number buffers on multi-core systems.

**Atomic Operations**: Eliminated mutex overhead for simple counters (open counts) by using atomic operations, reducing lock contention.

**Optimized Algorithms**: Upgraded from xoroshiro256** to Xoshiro256++ for better performance while maintaining excellent statistical quality.

These optimizations improve performance on multi-core systems and reduce lock contention bottlenecks.


Ultra High Speed Mode
---------------------
This mode uses the optimized Xoshiro256++ and wyhash64 PRNGs with enhanced shuffle algorithms.  This mode performs much faster than ChaCha8, but still passes dieharder tests.  To enable this mode, set the following line in the source code before running make.
```
#define ULTRA_HIGH_SPEED_MODE 1
```


Usage
-----
You can load the kernel module temporarily, or you can install the kernel module to be persistent on reboot.

  * If you want to just test the kernel module, you should run "make load".  This will load the kernel module into the running kernel and create a /dev/srandom accessible to root only.   It can be removed with "make unload".   You can monitor the load process in /var/log/messages.
  * When you run "make install", the srandom kernel module is moved to /usr/lib/modules/.../kernel/drivers/.  If you run "make load" or reboot, the kernel module will be loaded into the running kernel, but now will replace the /dev/urandom device file.  The old /dev/urandom device is renamed (keeping its inode number).  This allows any running process that had /dev/urandom to continue running without issues. All new requests for /dev/urandom will use the srandom kernel module.
  * Once the kernel module is loaded, you can access the module information through the /proc filesystem. For example:
```
# cat /proc/srandom
-----------------------:----------------------
Device                 : /dev/srandom
Module version         : 2.1.0 UHS (XorShift)
Current open count     : 3
Total open count       : 42
Total K bytes          : 38030518
-----------------------:----------------------
Author                 : Jonathan Senkerik
Website                : https://www.jintegrate.co
github                 : https://github.com/josenk/srandom

```
  * Use the /usr/bin/srandom tool to set srandom as the system PRNG, set the system back to default PRNG, or get the status.
```
# /usr/bin/srandom help

# /usr/bin/srandom status
Module loaded
srandom is functioning correctly
/dev/urandom is LINKED to /dev/srandom (system is using srandom)

```
  * To completely remove the srandom module, use "make uninstall".   Depending if there are processes accessing /dev/srandom, you may not be able to remove the module from the running kernel.   Try "make unload", if the module is busy, then a reboot is required.


Testing & performance
---------------------
A simple dd command to read from the /dev/srandom device will show performance of the generator.  The results below are typical from my system.  Of course, your performance will vary.


The srandom number generator

```
time dd if=/dev/srandom of=/dev/null count=64k bs=64k
65536+0 records in
65536+0 records out
4294967296 bytes (4.3 GB, 4.0 GiB) copied, 1.88435 s, 2.3 GB/s

real    0m1.886s
user    0m0.004s
sys     0m1.866s
```


The built-in urandom number generator (use /dev/urandom.orig if you did make install)

```
time dd if=/dev/urandom of=/dev/null count=64k bs=64k
65536+0 records in
65536+0 records out
4294967296 bytes (4.3 GB, 4.0 GiB) copied, 9.75787 s, 440 MB/s

real    0m9.760s
user    0m0.015s
sys     0m9.680s
```


Testing randomness
------------------
The most important part of the random number device file is that it produces random/unpredictable numbers.  The golden standard of testing randomness is the dieharder test suite (http://www.phy.duke.edu/~rgb/General/dieharder.php).  The dieharder tool will easily detect flawed random number generators.   After you install dieharder, use the following command to put /dev/srandom through the battery of tests.

NOTE: Being random includes the possibility of a test showing as "WEAK" or "FAILED"...  ie. If there's 0 percent chance a test could fail, that means it's not completely random.  With that in mind, if a test is repeatedly "FAILED" or "WEAK", then that is a problem, if it happened only once its OK/GOOD.


```
dd if=/dev/srandom |dieharder -g 200 -f - -a

#=============================================================================#
#            dieharder version 3.31.1 Copyright 2003 Robert G. Brown          #
#=============================================================================#
   rng_name    |           filename             |rands/second|
stdin_input_raw|                               -|  5.85e+07  |
#=============================================================================#
        test_name   |ntup| tsamples |psamples|  p-value |Assessment
#=============================================================================#
   diehard_birthdays|   0|       100|     100|0.81047982|  PASSED
      diehard_operm5|   0|   1000000|     100|0.05511543|  PASSED
  diehard_rank_32x32|   0|     40000|     100|0.01261989|  PASSED
    diehard_rank_6x8|   0|    100000|     100|0.47316309|  PASSED
   diehard_bitstream|   0|   2097152|     100|0.74346434|  PASSED
        diehard_opso|   0|   2097152|     100|0.98820557|  PASSED
        diehard_oqso|   0|   2097152|     100|0.38452940|  PASSED
         diehard_dna|   0|   2097152|     100|0.23079735|  PASSED
diehard_count_1s_str|   0|    256000|     100|0.35819763|  PASSED
diehard_count_1s_byt|   0|    256000|     100|0.18753110|  PASSED
 diehard_parking_lot|   0|     12000|     100|0.29327315|  PASSED
    diehard_2dsphere|   2|      8000|     100|0.35160725|  PASSED
    diehard_3dsphere|   3|      4000|     100|0.49118105|  PASSED
     diehard_squeeze|   0|    100000|     100|0.33775434|  PASSED
        diehard_sums|   0|       100|     100|0.37711818|  PASSED
        diehard_runs|   0|    100000|     100|0.54179533|  PASSED
        diehard_runs|   0|    100000|     100|0.73976903|  PASSED
       diehard_craps|   0|    200000|     100|0.66525469|  PASSED
       diehard_craps|   0|    200000|     100|0.84537370|  PASSED
 marsaglia_tsang_gcd|   0|  10000000|     100|0.10708190|  PASSED
 marsaglia_tsang_gcd|   0|  10000000|     100|0.87071126|  PASSED
         sts_monobit|   1|    100000|     100|0.30011393|  PASSED
            sts_runs|   2|    100000|     100|0.91175959|  PASSED
              <<<   etc...   >>>
```



How to manually configure your apps
-----------------------------------
  If you installed the kernel module to load on reboot, then you do not need to modify any applications to use the srandom kernel module.   It will be linked to /dev/urandom, so all applications will use it automatically.   However, if you do not want to link /dev/srandom to /dev/urandom, then you can configure your applications to use whichever device you want.   Here are a few examples....

  Java:  Use the following command line argument to tell Java to use the new random device

    -Djava.security.egd=file:///dev/srandom switch

       or

    -Djava.security.egd=file:/dev/./srandom

  Java: To make the setting as default, add the following line to the configuration file. ($JAVA_HOME/jre/lib/security/java.security)

    securerandom.source=file:/dev/./srandom


  https: (Apache SSL), Configure /etc/httpd/conf.d/ssl.conf

    SSLRandomSeed startup file:/dev/srandom 512
    SSLRandomSeed connect file:/dev/srandom 512


  Postfix: Change the following line in /etc/postfix/main.cf

    tls_random_source = dev:/dev/srandom


  PHP: Change the following line in PHP config file.

    session.entropy_file = /dev/srandom


  OpenLDAP:  Change the following line in /etc/openldap/slapd.conf

    TLSRandFile /dev/srandom



Using /dev/srandom to wipe all data from your SSD disks.
--------------------------------------------------------
*** This will DESTROY DATA!   Use with caution! ***

*** Replace /dev/sdXX with your disk device you want to wipe.


    dd if=/dev/srandom of=/dev/sdXX bs=64k


License
-------
Copyright (C) 2019 Jonathan Senkerik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
