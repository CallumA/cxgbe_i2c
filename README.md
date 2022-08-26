# cxgbe_i2c
FreeBSD driver for I2C on Chelsio NIC SFP ports

## Usage
The simplest way to read/write an SFP module is with `dd`.  
For example, to read the SFP data from `t4nex0`, port 1, page 0xA0: `dd if=/dev/t4i2c0.1 bs=1 iseek=0xa000 count=256`.  
Larger block sizes may also be used, but remember to change the `iseek` address: `dd if=/dev/t4i2c0.1 bs=4 iseek=0x2800 count=64`.

I2C addresses are really 7-bits long with a final write bit, but the SFP spec tends to refer to them as 8-bit addresses (0xA0 is really 0x50 where the LSB is 0).
This driver ignores the least significant bit in addresses, so reading/writing byte `0xa0000` is equivalent to reading/writing `0xa100`.

Writing works similarly, for example, to write a new part number (untested): `echo "Hello World!    " | dd of=/dev/t4i2c0.1 bs=1 oseek=0xa028 count=16`.  
The SFP will need to be writeable for this to succeed. Failure will be silent until read back.

Some SFPs need to be unlocked before they become writeable.

## Installation
```
# make
# make install
# kldload cxgbe_i2c
```
