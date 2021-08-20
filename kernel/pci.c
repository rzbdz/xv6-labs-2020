//
// simple PCI-Express initialization, only
// works for qemu and its e1000 card.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void
pci_init()
{
  // we'll place the e1000 registers at this address.
  // vm.c maps this range.
  uint64 e1000_regs = 0x40000000L; 

  // qemu -machine virt puts PCIe config space here.
  // vm.c maps this range.
  uint32  *ecam = (uint32 *) 0x30000000L;
  
  // look at each possible PCI device on bus 0.
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    // PCI address: 
    //   |31 enable bit|30:24 Reserved|-
    //  -|23:16 Bus num|15:11 Dev num|10:8 func num|7:2 off|1:0 0|.
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
    volatile uint32 *base = ecam + off;
    // PCI address space header:
    // Byte Off   |   3   |   2   |   1   |   0   |
    //          0h|   Device ID   |   Vendor ID   |
    uint32 id = base[0]; // read the first line.
    
    // 10 0e (device id):80 86(vendor id)  is an e1000
    if(id == 0x100e8086){
      // PCI address space header:
      // Byte Off   |   3   |   2    |   1     |   0    |
      //         4h |Status register | command register |
      // command and status register.
      // bit 0 : I/O access enable
      // bit 1 : memory access enable
      // bit 2 : enable mastering
      base[1] = 7;
      __sync_synchronize();

      for(int i = 0; i < 6; i++){
        // Byte Off              |   3   |   2    |   1     |   0    |
        // 16b/4b = 4        10h |           Base Address 0          |
        //          5        14h |           Base Address 1          |
        //          6        18h |           Base Address 2          |
        //          7    1ch~24h |          .... 3, 4, 5             |
        uint32 old = base[4+i];

        // writing all 1's to the BAR causes it to be
        // replaced with its size.
        base[4+i] = 0xffffffff;
        __sync_synchronize();
        // if we need a dynamic allocation, we can read the base[4+i] again, 
        // get it's one's complement and plus 1 to get it's BAR size (a dma area).
        base[4+i] = old;
      }

      // tell the e1000 to reveal its registers at
      // physical address 0x40000000.
      base[4+0] = e1000_regs;

      e1000_init((uint32*)e1000_regs);
    }
  }
}
