#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;


extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S


// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0){
      continue;
      //printf("not mapped va: %p\n", a);
      //panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      if(pa!=0)
        kfree((void*)pa);
      //printf("do free pass\n");
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0){
      continue;
      //panic("uvmcopy: page not present");
    }    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    if(flags&PTE_M){
      //if(mappages(new, i, PGSIZE, (uint64)0, flags) != 0)
      //  goto err;
      continue;
    }
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}


void printvma(struct proc* p){
  printf("table of pid=%d\n\n", p->pid);
  struct vma *v = p->vmatable;
  for(int i = 0;i<16;i++){
    printf("vma_%d, addr: %p, length: %p, f_inum: %p, off: %p\n", 
    i, v->addr, v->length, (v->addr)?v->f->ip->inum:0, v->fileoff);
    v++;
  }
  printf("table end\n\n");
}


// lab: mmap
void *
mmap(void *addr, uint64 length, int prot, int flags, int fd, int offset)
{
  struct proc* p = myproc();
  pagetable_t pg = p->pagetable;
  uint64 first_free = PGROUNDUP(p->sz);
  pte_t *pte;
  struct vma *v;
  uint64 i = 0;
  for (; i < length; i += PGSIZE) {
    if ((pte = walk(pg, first_free + i, 1)) == 0) {
      printf("walk\n");
      goto dump;
    }
    if (*pte & PTE_V) {
      printf("remap\n");
      goto dump;
    }
    *pte = PTE_V|PTE_U|PTE_M;
    p->sz += PGSIZE;
  }
  struct file* f = p->ofile[fd];
  printf("mmap: fd: %d\n", fd);
  if(f->readable == 0 && prot&PROT_READ){
    printf("no readable f\n");
    goto dump;
  }
  if(flags&MAP_SHARED && f->writable == 0 && prot&PROT_WRITE){
    printf("no writable f\n");
    goto dump;
  }
  
  v = p->vmatable;
  for(int i =0;i<MAXVMA;i++){
    if(v->addr==0){
      v->f = filedup(f);
      v->length = length;
      v->addr = first_free;
      v->prot = prot;
      v->flags = flags;
      v->fileoff = offset;
      return (void*)first_free;
    }
    v++;
  }
  printf("no slot for vma\n");
dump:
  return (char*)-1;
}

int 
mmapapage(uint64 addr){
  struct proc* p = myproc();
  //printvma(p);
  pagetable_t pg = p->pagetable;
  pte_t *pte;
  struct inode* ip = 0;
  addr = PGROUNDDOWN(addr);
  if((pte = walk(pg, addr, 0))==0 ||((*pte) & PTE_M )==0){
    goto bad;
  }
  uint64 vma_low, vma_high, dst;
  uint map_file_off;
  struct vma *v = p->vmatable;
  for(int i =0;i<MAXVMA;i++){
    vma_low = v->addr;
    if(vma_low==0){
      v++;
      continue;
    }
    vma_high = vma_low + v->length;
    if(vma_low <= addr && vma_high>addr){
      map_file_off = addr - vma_low;
      ip = v->f->ip;
      ilock(ip);
      if((dst = (uint64)kalloc())==0){
        printf("mmap: kalloc\n");
        goto bad;
      }
      memset((void*)dst,0,PGSIZE);
      //printf("pid %d, addr: %p, pa: %p\n", p->pid, addr, dst);
      *pte = PA2PTE(dst)|PTE_U|PTE_V|PTE_M|PROT2FLAG(v->prot);
      if(readi(ip, 0, dst, map_file_off, (uint)PGSIZE)==0){
        goto bad;
      }
      iunlock(ip);
      return 0;
    }
    v++;
  }
  printf("mmap: not found addr %p in vma\n", addr);
bad:
  if(ip){
    iunlock(ip);
  }
  return -1;
}

int 
munmap(void* addr, int length) 
{
  uint64 low = PGROUNDDOWN((uint64)addr);
  uint64 high = PGROUNDUP((uint64)addr + length);
  int n = high - low;
  struct proc* p = myproc();
  pagetable_t pg = p->pagetable;
  struct inode* ip = 0;
  uint64 vma_low, vma_high;
  uint map_file_off;
  struct vma* v = p->vmatable;
  for (int i = 0; i < MAXVMA; i++) {
    //printf("test inf loop\n");
    vma_low = v->addr;
    if (vma_low == 0) {
      v++;
      continue;
    }
    vma_high = vma_low + v->length;
    if ((uint64)addr < vma_low || (uint64)addr >= vma_high) {
      v++;
      continue;
    }
    int r, ret = 0;
    if (v->flags & MAP_SHARED && v->prot & PROT_WRITE) {
      map_file_off = v->fileoff + (low - vma_low);
      ip = v->f->ip;
      // write a few blocks at a time to avoid exceeding
      // the maximum log transaction size, including
      // i-node, indirect block, allocation blocks,
      // and 2 blocks of slop for non-aligned writes.
      // this really belongs lower down, since writei()
      // might be writing a device like the console.
      int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
      int i = 0;
      while (i < n) {
        //printf("test inf loop wh\n");
        int n1 = n - i;
        if (n1 > max)
          n1 = max;
        begin_op();
        ilock(ip);
        if ((r = writei(ip, 1, low + i, map_file_off, n1)) > 0)
          map_file_off += r;
        iunlock(ip);
        end_op();
        if(r==0){
          // ip->size less than spec
          break;
        }
        if (r == -1) {
          // error from writei
          ret = -1;
          break;
        }
        i += r;
      }
    }
    if (low == vma_low && high == vma_high) {
      // unmap whole vma
      v->addr = 0;
      fileclose(v->f);
    } else if (low > vma_low) {
      // unmap [low -> vma_high], leave [vma_low -> low -1]
      v->length = low - vma_low;
    } else if (high < vma_high) {
      // unmap [vma_low -> high], leave [high -> vma_high]
      /* bug code!!!! 
        v->addr = up;
        v->length += v->addr - up; */
      v->length += v->addr - high;
      v->fileoff += high - v->addr;
      v->addr = high;
    } else {
    }
    uvmunmap(pg, low, (high - low) / PGSIZE, 1);
    return ret;
  }
  return 0;
}
