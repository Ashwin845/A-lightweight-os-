/* AHCI improved (drop-in candidate) */

#include "../include/kernel.h"

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_COMMAND_SLOTS 32
#define AHCI_MAX_PRDT_ENTRIES 8

#define HBA_PxCMD_ST   (1 << 0)
#define HBA_PxCMD_FRE  (1 << 4)
#define HBA_PxCMD_FR   (1 << 14)
#define HBA_PxCMD_CR   (1 << 15)

#define HBA_PxIS_TFES  (1 << 30)

/* Minimal safe implementation; see drivers/ahci.c (replacement candidate) */

void ahci_initialize(void) {
    KLOG("AHCI: placeholder new file present (ahci_new.c)");
}

int ahci_read_sector(uint32_t lba, uint8_t* buffer) { return -1; }
int ahci_write_sector(uint32_t lba, uint8_t* buffer) { return -1; }
