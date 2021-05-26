// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

uint 
hash(uint dev, uint block_no)
{
  uint key = dev << 16 | block_no;
  uint h = 0x7f7f7f7f;
  key = (key ^ h) ^ ((key >> 20)^h) ^ (key << 12);
  key ^= h;
  key += ~h;
  return key%NBUCKET;
}

struct {
  // struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf bucket_head[NBUCKET];
  struct spinlock bucket_lock[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  //initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  for (int i = 0; i < NBUCKET; i++) {
    bcache.bucket_head[i].prev = &bcache.bucket_head[i];
    bcache.bucket_head[i].next = &bcache.bucket_head[i];
    initlock(&bcache.bucket_lock[i], "bcache.bucket");
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.bucket_head[0].next;
    b->prev = &bcache.bucket_head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.bucket_head[0].next->prev = b;
    bcache.bucket_head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // acquire(&bcache.lock);
  uint buid = hash(dev, blockno);
  // Is the block already cached?
  acquire(&bcache.bucket_lock[buid]);
  for(b = bcache.bucket_head[buid].next; b != &bcache.bucket_head[buid]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      // release(&bcache.lock);
      release(&bcache.bucket_lock[buid]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf *steal = &bcache.bucket_head[buid];
  uint stealid = buid;
  do {
    if(stealid != buid){
      acquire(&bcache.bucket_lock[stealid]);
    }
    for (b = steal->prev; b != steal; b = b->prev) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // release(&bcache.lock);
        if(stealid != buid){
          b->next->prev = b->prev;
          b->prev ->next  = b->next;
          b->next = bcache.bucket_head[buid].next;
          b->prev = &bcache.bucket_head[buid];
          bcache.bucket_head[buid].next->prev = b;
          bcache.bucket_head[buid].next = b;
          release(&bcache.bucket_lock[stealid]);
        }
        release(&bcache.bucket_lock[buid]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    if (stealid != buid) {
      release(&bcache.bucket_lock[stealid]);
    }
    stealid = (stealid + 1)%NBUCKET;
    steal = &bcache.bucket_head[stealid];
  } while (steal != bcache.bucket_head + NBUCKET);

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint buid = hash(b->dev, b->blockno);

  acquire(&bcache.bucket_lock[buid]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.bucket_head[buid].next;
    b->prev = &bcache.bucket_head[buid];
    bcache.bucket_head[buid].next->prev = b;
    bcache.bucket_head[buid].next = b;
  }
  
  release(&bcache.bucket_lock[buid]);
}

void
bpin(struct buf *b) {
  uint buid = hash(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[buid]);
  b->refcnt++;
  release(&bcache.bucket_lock[buid]);
}

void
bunpin(struct buf *b) {
  uint buid = hash(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[buid]);
  b->refcnt--;
  release(&bcache.bucket_lock[buid]);
}


