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

struct {
  struct spinlock mainlock;
  struct spinlock locks[MAXOPBLOCKS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;
  
  initlock(&bcache.mainlock, "mainbcache");

  for(int i =0;i<MAXOPBLOCKS;i++){
    char name[30];
    snprintf(name,30,"locks[%d]",i);
    initlock(&bcache.locks[i],name);
  }
  //把bcache buf 插入head后面
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    //modify the buf inserted 
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  //这里查询的锁可以优化
  int searchid = blockno%MAXOPBLOCKS;
  acquire(&bcache.locks[searchid]);

  // Is the block already cached?
  for(b = bcache.buf+searchid; b < bcache.buf+NBUF; b += MAXOPBLOCKS){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[searchid]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.buf+searchid; b < bcache.buf+NBUF; b += MAXOPBLOCKS){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[searchid]);
      acquiresleep(&b->lock);
      return b;
    }
  }
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
  int id = b->blockno;
  releasesleep(&b->lock);

  int searchid = id%MAXOPBLOCKS;


  acquire(&bcache.locks[searchid]);
  b->refcnt--;
  release(&bcache.locks[searchid]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno%MAXOPBLOCKS]);
  b->refcnt++;
  release(&bcache.locks[b->blockno%MAXOPBLOCKS]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno%MAXOPBLOCKS]);
  b->refcnt--;
  release(&bcache.locks[b->blockno%MAXOPBLOCKS]);
}


