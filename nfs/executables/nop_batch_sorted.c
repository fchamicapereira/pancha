#include "boilerplate.h"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define MAX_FLOWS 65536
#define EXPIRATION_TIME_NS 1000000000 // 1 seconds
#define LAN 0
#define WAN 1

struct pkt_tag_t {
  uint8_t i;
};

bool pkt_tag_lt(struct pkt_tag_t *a, struct pkt_tag_t *b) { return false; }

void sort_tagged_packets(struct pkt_tag_t *pkts, uint16_t n) {
  for (int i = 1; i < n; i++) {
    struct pkt_tag_t curr = pkts[i];
    int j                 = i - 1;

    while (j >= 0 && pkt_tag_lt(&pkts[j], &curr)) {
      pkts[j + 1] = pkts[j];
      j           = j - 1;
    }
    pkts[j + 1] = curr;
  }
}

void worker_loop_batched_sorted() {
  NF_INFO("Core %u forwarding packets.", rte_lcore_id());

  struct tx_mbuf_batch {
    struct rte_mbuf *batch[BATCH_SIZE];
    uint16_t tx_count;
  };

  uint16_t devices_count                  = rte_eth_dev_count_avail();
  struct tx_mbuf_batch *tx_batch_per_port = (struct tx_mbuf_batch *)calloc(devices_count, sizeof(struct tx_mbuf_batch));

  while (1) {
    for (uint16_t device = 0; device < devices_count; device++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];
      uint16_t rx_count = rte_eth_rx_burst(device, 0, mbufs, BATCH_SIZE);

      if (rx_count == 0) {
        continue;
      }

      uint8_t *pkts[BATCH_SIZE];
      uint32_t pkt_lens[BATCH_SIZE];
      struct pkt_tag_t tagged_pkts[BATCH_SIZE];

      for (uint16_t n = 0; n < rx_count; n++) {
        pkts[n]          = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkt_lens[n]      = mbufs[n]->pkt_len;
        tagged_pkts[n].i = n;
      }

      // printf("Before: ");
      // for (int i = 0; i < rx_count; i++) {
      //   printf("%u,", tagged_pkts[i].i);
      // }
      // printf("\n");

      sort_tagged_packets(tagged_pkts, rx_count);

      // printf("Sorted batch of %u packets: ", rx_count);
      // for (int i = 0; i < rx_count; i++) {
      //   printf("%u,", tagged_pkts[i].i);
      // }
      // printf("\n");
      // exit(0);

      uint8_t current_batch = 0;

      uint8_t strides[BATCH_SIZE];
      memset(strides, 1, sizeof(strides));
      for (uint16_t n = 0; n < rx_count - 1; n++) {
        strides[current_batch]++;
      }

      time_ns_t now = current_time();

      current_batch = 0;
      for (uint16_t n = 0; n < rx_count; current_batch++) {
        uint8_t i           = tagged_pkts[n].i;
        uint8_t *pkt        = pkts[i];
        uint32_t pkt_len    = pkt_lens[i];
        uint16_t src_device = mbufs[i]->port;

        packet_state_total_length(pkt, &(mbufs[i]->pkt_len));

        uint16_t dst_device = nf_process(src_device, &pkt, pkt_len, now, mbufs[i]);
        nf_return_all_chunks(pkt);

        if (dst_device == DROP) {
          for (uint8_t stride_size = 0; stride_size < strides[current_batch]; stride_size++) {
            rte_pktmbuf_free(mbufs[tagged_pkts[n + stride_size].i]);
          }
        } else {
          for (uint8_t stride_size = 0; stride_size < strides[current_batch]; stride_size++) {
            uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
            tx_batch_per_port[dst_device].batch[tx_count] = mbufs[tagged_pkts[n + stride_size].i];
            tx_batch_per_port[dst_device].tx_count++;
          }
        }

        n += strides[current_batch];
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count = rte_eth_tx_burst(dst_device, 0, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(mbufs[tagged_pkts[n].i]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_sorted();
  return 0;
}

bool nf_init(void) { return true; }

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length, time_ns_t now, struct rte_mbuf *mbuf) {
  if (device == LAN) {
    return WAN;
  } else {
    return LAN;
  }
}