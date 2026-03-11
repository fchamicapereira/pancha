#include "../lib/state/map.h"
#include "../lib/state/vector.h"
#include "../lib/state/double-chain.h"
#include "../lib/state/cht.h"
#include "../lib/state/cms.h"
#include "../lib/state/bloom-filter.h"
#include "../lib/state/token-bucket.h"

#include "../lib/util/hash.h"
#include "../lib/util/expirator.h"
#include "../lib/util/packet-io.h"
#include "../lib/util/tcpudp_hdr.h"
#include "../lib/util/time.h"
#include "../lib/log.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_random.h>
#include <rte_hash_crc.h>

#define BATCH_SIZE 32

#define DROP ((uint16_t)-1)
#define FLOOD ((uint16_t)-2)

bool nf_init(void);
int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length,
               time_ns_t now, struct rte_mbuf *mbuf);

static const uint16_t RX_QUEUE_SIZE = 1024;
static const uint16_t TX_QUEUE_SIZE = 1024;

// Buffer count for mempools
static const unsigned MEMPOOL_BUFFER_COUNT = 2048;

// Send the given packet to all devices except the packet's own
void flood(struct rte_mbuf *packet, uint16_t nb_devices) {
  rte_mbuf_refcnt_set(packet, nb_devices - 1);
  int total_sent       = 0;
  uint16_t skip_device = packet->port;
  for (uint16_t device = 0; device < nb_devices; device++) {
    if (device != skip_device) {
      total_sent += rte_eth_tx_burst(device, 0, &packet, 1);
    }
  }

  if (total_sent != nb_devices - 1) {
    rte_mbuf_refcnt_set(packet, 1);
    rte_pktmbuf_free(packet);
  }
}

// Initializes the given device using the given memory pool
static int nf_init_device(uint16_t device, struct rte_mempool *mbuf_pool) {
  int retval;

  // device_conf passed to rte_eth_dev_configure cannot be NULL
  struct rte_eth_conf device_conf = {0};
  // device_conf.rxmode.hw_strip_crc = 1;

  // Configure the device (1, 1 == number of RX/TX queues)
  retval = rte_eth_dev_configure(device, 1, 1, &device_conf);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up a TX queue (NULL == default config)
  retval = rte_eth_tx_queue_setup(device, 0, TX_QUEUE_SIZE,
                                  rte_eth_dev_socket_id(device), NULL);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up RX queues (NULL == default config)
  retval = rte_eth_rx_queue_setup(
      device, 0, RX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL, mbuf_pool);
  if (retval != 0) {
    return retval;
  }

  retval = rte_eth_dev_start(device);
  if (retval != 0) {
    return retval;
  }

  rte_eth_promiscuous_enable(device);
  if (rte_eth_promiscuous_get(device) != 1) {
    return retval;
  }

  return 0;
}

static void worker_loop() {
  NF_INFO("Core %u forwarding packets.", rte_lcore_id());

  struct tx_mbuf_batch {
    struct rte_mbuf *batch[BATCH_SIZE];
    uint16_t tx_count;
  };

  uint16_t devices_count                  = rte_eth_dev_count_avail();
  struct tx_mbuf_batch *tx_batch_per_port = (struct tx_mbuf_batch *)calloc(
      devices_count, sizeof(struct tx_mbuf_batch));

  while (1) {
    for (uint16_t device = 0; device < devices_count; device++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];
      uint16_t rx_count = rte_eth_rx_burst(device, 0, mbufs, BATCH_SIZE);

      for (uint16_t n = 0; n < rx_count; n++) {
        uint8_t *data = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        packet_state_total_length(data, &(mbufs[n]->pkt_len));
        time_ns_t now = current_time();
        uint16_t dst_device =
            nf_process(mbufs[n]->port, &data, mbufs[n]->pkt_len, now, mbufs[n]);

        if (dst_device == DROP) {
          rte_pktmbuf_free(mbufs[n]);
        } else {
          uint16_t tx_count = tx_batch_per_port[dst_device].tx_count;
          tx_batch_per_port[dst_device].batch[tx_count] = mbufs[n];
          tx_batch_per_port[dst_device].tx_count++;
        }
      }

      for (uint16_t dst_device = 0; dst_device < devices_count; dst_device++) {
        uint16_t sent_count =
            rte_eth_tx_burst(dst_device, 0, tx_batch_per_port[dst_device].batch,
                             tx_batch_per_port[dst_device].tx_count);
        for (uint16_t n = sent_count;
             n < tx_batch_per_port[dst_device].tx_count; n++) {
          rte_pktmbuf_free(mbufs[n]); // should not happen, but we're in
                                      // the unverified case anyway
        }
        tx_batch_per_port[dst_device].tx_count = 0;
      }
    }
  }
}

static void worker_main(void) {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  worker_loop();
}

int main(int argc, char **argv) {
  // Initialize the DPDK Environment Abstraction Layer (EAL)
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization, ret=%d\n", ret);
  }
  argc -= ret;
  argv += ret;

  unsigned nb_devices = rte_eth_dev_count_avail();

  // cache size (per-core, not useful in a single-threaded app)
  unsigned cache_size = 0;

  // application private area size
  uint16_t priv_size = 0;

  uint16_t data_room_size = RTE_MBUF_DEFAULT_BUF_SIZE;

  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
      "MEMPOOL", MEMPOOL_BUFFER_COUNT * nb_devices, cache_size, priv_size,
      data_room_size, rte_socket_id());
  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot create pool: %s\n", rte_strerror(rte_errno));
  }

  // Initialize all devices
  for (uint16_t device = 0; device < nb_devices; device++) {
    ret = nf_init_device(device, mbuf_pool);
    if (ret == 0) {
      NF_INFO("Initialized device %" PRIu16 ".", device);
    } else {
      rte_exit(EXIT_FAILURE, "Cannot init device %" PRIu16 ": %d", device, ret);
    }
  }

  worker_main();

  return 0;
}

bool nf_init(void) { return true; }

int nf_process(uint16_t device, uint8_t **buffer, uint16_t packet_length,
               time_ns_t now, struct rte_mbuf *mbuf) {
  // Mark now as unused, we don't care about time
  (void)now;

  return device;
}
