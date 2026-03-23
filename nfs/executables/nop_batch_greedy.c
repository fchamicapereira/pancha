#include "boilerplate.h"

#define LAN 0
#define WAN 1

bool flow_match(uint8_t *pkt0, uint32_t pkt_len0, uint8_t *pkt1, uint32_t pkt_len1) { return true; }

void worker_loop_batched_greedy() {
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

      uint8_t current_batch = 0;

      uint8_t strides[BATCH_SIZE];
      memset(strides, 1, sizeof(strides));

      for (uint16_t n = 0; n < rx_count - 1; n++) {
        pkts[n]         = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        pkts[n + 1]     = rte_pktmbuf_mtod(mbufs[n + 1], uint8_t *);
        pkt_lens[n]     = mbufs[n]->pkt_len;
        pkt_lens[n + 1] = mbufs[n + 1]->pkt_len;
        if (flow_match(pkts[n], pkt_lens[n], pkts[n + 1], pkt_lens[n + 1])) {
          strides[current_batch]++;
        } else {
          current_batch++;
        }
      }

      time_ns_t now = current_time();

      current_batch = 0;
      for (uint16_t n = 0; n < rx_count; current_batch++) {
        uint8_t *pkt        = pkts[n];
        uint32_t pkt_len    = pkt_lens[n];
        uint16_t src_device = mbufs[n]->port;

        packet_state_total_length(pkt, &(mbufs[n]->pkt_len));

        uint16_t dst_device = nf_process(src_device, &pkt, pkt_len, now, mbufs[n]);
        nf_return_all_chunks(pkt);

        if (dst_device == DROP) {
          for (uint8_t i = 0; i < strides[current_batch]; i++) {
            rte_pktmbuf_free(mbufs[n + i]);
          }
        } else {
          for (uint8_t i = 0; i < strides[current_batch]; i++) {
            uint16_t tx_count                             = tx_batch_per_port[dst_device].tx_count;
            tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n + i];
            tx_batch_per_port[dst_device].tx_count++;
          }
        }

        n += strides[current_batch];
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count = rte_eth_tx_burst(dst_device, 0, tx_batch_per_port[dst_device].batch,
                                               tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count; n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(mbufs[n]);
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

int main(int argc, char **argv) {
  nf_setup(argc, argv);
  worker_loop_batched_greedy();
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