#ifndef RUNNER_SD_SDHCI_K1_H
#define RUNNER_SD_SDHCI_K1_H

#include <stdint.h>

/*
 * SpacemiT K1 SDHCI backend used by the controller-neutral SD-tail loader.
 * SPL is expected to have enabled the K1 controller clocks, resets, PHY and
 * card power before entering the runner.
 */
void k1_sdhci_reset_state(uintptr_t base);
int k1_sdhci_attach_from_spl(void);
int k1_sdhci_card_init(void);
int k1_sdhci_get_capacity_blocks(uint32_t *blocks_out);
int k1_sdhci_read_block_words(uint32_t lba, uint32_t *dst_words);
int k1_sdhci_write_block_words(uint32_t lba, const uint32_t *src_words);
void k1_sdhci_quiesce(void);

#endif
