#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "virtio.h"
#include "defs.h"
#include "buf.h"
#include "memlayout.h"

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

static struct disk {
	// memory for virtio descriptors &c for queue 0.
	// this is a global instead of allocated because it must
	// be multiple contiguous pages, which kalloc()
	// doesn't support, and page aligned.
	char pages[2*PGSIZE];
	struct VRingDesc *desc;
	uint16 *avail;
	struct UsedArea *used;

	// our own book-keeping.
	char free[NUM];  // is a descriptor free?
	uint16 used_idx; // we've looked this far in used[2..NUM].

	// track info about in-flight operations,
	// for use when completion interrupt arrives.
	// indexed by first descriptor index of chain.
	struct {
	struct buf *b;
	char status;
	} info[NUM];

	struct spinlock vdisk_lock;
} __attribute__ ((aligned (PGSIZE))) disk;

void virtio_disk_init(void)
{
	uint32 status = 0;

	initlock(&disk.vdisk_lock, "virtio_disk");

	if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
	    *R(VIRTIO_MMIO_VERSION) != 1 ||
	    *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
	    *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
		panic("could not find virtio disk");
	}

	status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
	*R(VIRTIO_MMIO_STATUS) = status;

	status |= VIRTIO_CONFIG_S_DRIVER;
	*R(VIRTIO_MMIO_STATUS) = status;

	// negotiate features
	uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
	features &= ~(1 << VIRTIO_BLK_F_RO);
	features &= ~(1 << VIRTIO_BLK_F_SCSI);
	features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
	features &= ~(1 << VIRTIO_BLK_F_MQ);
	features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
	features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
	features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
	*R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

	// tell device that feature negotiation is complete.
	status |= VIRTIO_CONFIG_S_FEATURES_OK;
	*R(VIRTIO_MMIO_STATUS) = status;

	// tell device we're completely ready.
	status |= VIRTIO_CONFIG_S_DRIVER_OK;
	*R(VIRTIO_MMIO_STATUS) = status;

	*R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

	// initialize queue 0.
	*R(VIRTIO_MMIO_QUEUE_SEL) = 0;
	uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (max == 0)
		panic("virtio disk has no queue 0");
	if (max < NUM)
		panic("virtio disk max queue too short");
	*R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
	memset(disk.pages, 0, sizeof(disk.pages));
	*R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)disk.pages) >> PGSHIFT;

	// desc = pages -- num * VRingDesc
	// avail = pages + 0x40 -- 2 * uint16, then num * uint16
	// used = pages + 4096 -- 2 * uint16, then num * vRingUsedElem

	disk.desc = (struct VRingDesc *) disk.pages;
	disk.avail = (uint16*)(((char*)disk.desc) + NUM*sizeof(struct VRingDesc));
	disk.used = (struct UsedArea *) (disk.pages + PGSIZE);

	for (int i = 0; i < NUM; i++)
		disk.free[i] = 1;

	// plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}