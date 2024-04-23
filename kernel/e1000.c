#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  acquire(&e1000_lock);
  // Index location of TDT, the tail end of the ring
  uint32 tx_ring_index = regs[E1000_TDT];

  // Creating a pointer to the current descriptor at the index of TDT tail location
  struct tx_desc *descriptor = &tx_ring[tx_ring_index];
  
  // Checking if E1000_TXD_STAT_DD (descriptor done) is set in the descriptor
  // If not, return -1, otherwise free the last mbuf
  if(descriptor->status != E1000_TXD_STAT_DD)
    return -1;
  else 
  {
    //Free the old mbuf transmited from descriptor TODO
    
  }

  // Filling in descriptor head and length from newest mbuf
  descriptor->addr = (uint64)m->head;
  descriptor->length = m->len;

  // Set cmd flags
  descriptor->cmd |= (E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP);

  // Save given mbuf for potential future use
  tx_mbufs[tx_ring_index] = m;

  // Updating ring position by adding one to E1000_TDT mod TX_RING_SIZE
  // Since regs[E1000_TDT] is actually the index position, we set that
  regs[E1000_TDT] = (E1000_TDT + 1) % TX_RING_SIZE;

  // Test print statement
  printf("test test transmit!");
  
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{ 
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  acquire(&e1000_lock);

  // add ring position
  // think index above, but for the receive descriptor (I think)
  // per lab spec, this is (RDT + 1) % RX_RING_SIZE
  uint32 rx_ring_index = (E1000_RDT + 1) % RX_RING_SIZE;

  //Get the descriptor at the current index for checking status
  struct rx_desc *descriptor = &rx_ring[rx_ring_index];

  //Check if a packet is available by checking for the E1000_RXD_STAT_DD bit in the status portion of the descriptor
  if(descriptor->status != E1000_RXD_STAT_DD)
    return; //If there is not a packet available leave


  //Update mbuf's length with the packet length from the descriptor and send to network stack
  rx_mbufs[rx_ring_index]->len = descriptor->length;
  net_rx(rx_mbufs[rx_ring_index]);

  //Alocate a new mbuf to replace the one just given to net_rx(). Look at e1000_init(). TODO

  //Program its data pointer (m->head) into the descriptor and clear the descriptors status bits to zero. TODO
  
  //Update the E1000_RDT register to be the index of the last ring descriptor processed. TODO

  //At some point, the total number of packets that have ever arrived will exceed the ring size (16); make sure your code can handle that TODO

  printf("test test receiving");
  release(&e1000_lock);  
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
