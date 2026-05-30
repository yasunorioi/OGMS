// usb_ncm.h — USB CDC-NCM netif for RP2350 (dual-netif with W5500)
// TinyUSB NCM callbacks + lwIP netif + minimal DHCP server
// Design: context/rp2350-usb-ncm-design.md (cmd_543)
//
// IMPORTANT: W5500 is ALWAYS netif_set_default(). USB-NCM is link-local only.
//            Never call netif_set_default() on usb_netif.

#ifndef USB_NCM_H
#define USB_NCM_H

#include <Adafruit_TinyUSB.h>
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/dhcp.h>
#include <lwip/ip4_addr.h>
#include <lwip/etharp.h>
#include <netif/ethernet.h>

// MAC address for USB-NCM interface (locally administered)
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

// ========== USB NCM Descriptor (Adafruit_USBD_Interface) ==========
// Registers CDC-NCM as a composite USB interface alongside CDC-ACM (Serial)

// ========== USB NCM Descriptor Registration ==========
// Override TinyUSB_Device_Init (weak) to inject NCM descriptor
// before tud_init() is called. This runs BEFORE setup().

class Adafruit_USBD_NCM : public Adafruit_USBD_Interface {
public:
  Adafruit_USBD_NCM(void) {}

  bool begin(void) {
    setStringDescriptor("USB NCM");
    setMACStringDescriptor();
    if (!TinyUSBDevice.addInterface(*this)) return false;
    return true;
  }

  uint16_t getInterfaceDescriptor(uint8_t itfnum, uint8_t *buf, uint16_t bufsize) override {
    uint16_t const desc_len = TUD_CDC_NCM_DESC_LEN;

    if (buf == NULL) return desc_len;
    if (bufsize < desc_len) return 0;

    uint8_t ep_notif = 0x80 | TinyUSBDevice.allocEndpoint(1);
    uint8_t ep_in    = 0x80 | TinyUSBDevice.allocEndpoint(1);
    uint8_t ep_out   = TinyUSBDevice.allocEndpoint(0);

    uint8_t const desc[] = {
      TUD_CDC_NCM_DESCRIPTOR(itfnum, _strid, _mac_strid,
                             ep_notif, 8, ep_out, ep_in, 64, 1514)
    };

    memcpy(buf, desc, desc_len);

    // NCM uses 2 interfaces (control + data). addInterface() reads
    // _itf_count AFTER this returns to set bNumInterfaces, so we must
    // bump it by 2 here — otherwise bNumInterfaces is short by 1 and
    // the host never binds a driver to the data interface.
    TinyUSBDevice.allocInterface(2);

    return desc_len;
  }

private:
  uint8_t _mac_strid = 0;

  void setMACStringDescriptor(void) {
    static char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             tud_network_mac_address[0], tud_network_mac_address[1],
             tud_network_mac_address[2], tud_network_mac_address[3],
             tud_network_mac_address[4], tud_network_mac_address[5]);
    _mac_strid = TinyUSBDevice.addStringDescriptor(mac_str);
  }
};

// Global NCM interface instance (included only from .ino)
Adafruit_USBD_NCM usb_ncm_dev;

// Trampoline for usb_ncm_init.cpp (avoids including full header there)
bool usb_ncm_dev_begin(void) { return usb_ncm_dev.begin(); }

// ========== Internal State ==========
static struct netif usb_netif;
static bool usb_ncm_initialized = false;

// Raw-byte staging buffer used to hand frames from the USB IRQ to loop()
// without doing pbuf_alloc inside the IRQ. PBUF_POOL was returning NULL
// for every recv (likely starved by mDNS) and PBUF_RAM acquires the lwIP
// memory mutex which deadlocks when called from IRQ while setup holds it.
#define USB_NCM_STAGE_MTU 1600
static uint8_t  usb_rx_stage[USB_NCM_STAGE_MTU];
static volatile uint16_t usb_rx_stage_len = 0;  // 0 = empty

// ========== lwIP netif output → TinyUSB ==========

static err_t usb_ncm_linkoutput(struct netif *nif, struct pbuf *p) {
  (void)nif;
  if (!tud_ready()) return ERR_USE;

  // Non-blocking: if TinyUSB can't accept, drop the packet
  if (!tud_network_can_xmit(p->tot_len)) return ERR_WOULDBLOCK;

  tud_network_xmit(p, 0);
  return ERR_OK;
}

static err_t usb_ncm_output(struct netif *nif, struct pbuf *p, const ip4_addr_t *addr) {
  return etharp_output(nif, p, addr);
}

// ========== netif init callback ==========

static err_t usb_ncm_netif_init(struct netif *nif) {
  nif->mtu = CFG_TUD_NET_MTU;
  // Do NOT set NETIF_FLAG_LINK_UP or NETIF_FLAG_UP here —
  // use netif_set_up() / netif_set_link_up() after netif_add()
  // so that lwIP sends gratuitous ARP and triggers callbacks.
  nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  nif->state = NULL;
  nif->name[0] = 'U';  // USB
  nif->name[1] = 'N';  // NCM
  nif->linkoutput = usb_ncm_linkoutput;
  nif->output = usb_ncm_output;

  // MAC: device side (toggle LSB from host MAC per TinyUSB convention)
  nif->hwaddr_len = 6;
  memcpy(nif->hwaddr, tud_network_mac_address, 6);
  nif->hwaddr[5] ^= 0x01;

  return ERR_OK;
}

// ========== TinyUSB NCM Callbacks ==========

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  // Drop if previous staging frame not yet consumed by loop().
  if (usb_rx_stage_len != 0) return false;
  if (size == 0 || size > USB_NCM_STAGE_MTU) return true;  // accept-and-drop
  memcpy(usb_rx_stage, src, size);
  usb_rx_stage_len = size;
  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  struct pbuf *p = (struct pbuf *)ref;
  (void)arg;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
  // Reset the staging slot — pbuf path is gone; loop() now allocates.
  usb_rx_stage_len = 0;
}

// ========== Minimal DHCP Server (USB-NCM side only) ==========
// Based on TinyUSB lib/networking/dhserver.c (MIT, Sergey Fetisov)
// Simplified: single client, no DNS, no domain, no router (link-local)

#define DHCP_OP_REQUEST   1
#define DHCP_OP_REPLY     2
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5

#define DHCP_OPT_SUBNET   1
#define DHCP_OPT_IPADDR   50
#define DHCP_OPT_LEASE    51
#define DHCP_OPT_MSGTYPE  53
#define DHCP_OPT_SERVERID 54
#define DHCP_OPT_END      255

typedef struct __attribute__((packed)) {
  uint8_t  op;
  uint8_t  htype;
  uint8_t  hlen;
  uint8_t  hops;
  uint32_t xid;
  uint16_t secs;
  uint16_t flags;
  uint8_t  ciaddr[4];
  uint8_t  yiaddr[4];
  uint8_t  siaddr[4];
  uint8_t  giaddr[4];
  uint8_t  chaddr[16];
  uint8_t  legacy[192];
  uint8_t  magic[4];
  uint8_t  options[312];
} usb_dhcp_msg_t;

static struct udp_pcb *usb_dhcp_pcb = NULL;
static const uint8_t dhcp_magic[4] = {0x63, 0x82, 0x53, 0x63};

static uint8_t *usb_dhcp_find_option(uint8_t *opts, int len, uint8_t type) {
  int i = 0;
  while (i + 1 < len) {
    if (opts[i] == DHCP_OPT_END) break;
    if (opts[i] == 0) { i++; continue; }
    int next = i + opts[i + 1] + 2;
    if (next > len) return NULL;
    if (opts[i] == type) return &opts[i];
    i = next;
  }
  return NULL;
}

static int usb_dhcp_fill_options(uint8_t *opts, uint8_t msg_type, ip4_addr_t server_ip) {
  uint8_t *p = opts;

  *p++ = DHCP_OPT_MSGTYPE; *p++ = 1; *p++ = msg_type;

  *p++ = DHCP_OPT_SERVERID; *p++ = 4;
  memcpy(p, &server_ip.addr, 4); p += 4;

  // Lease time: 86400s (24h)
  *p++ = DHCP_OPT_LEASE; *p++ = 4;
  *p++ = 0; *p++ = 1; *p++ = 0x51; *p++ = 0x80;

  *p++ = DHCP_OPT_SUBNET; *p++ = 4;
  *p++ = 255; *p++ = 255; *p++ = 255; *p++ = 0;

  // NO router option — link-local only, host gets no default route
  // NO DNS option

  *p++ = DHCP_OPT_END;
  return (int)(p - opts);
}

static void usb_dhcp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                           const ip_addr_t *addr, u16_t port) {
  (void)arg; (void)addr;

  usb_dhcp_msg_t msg;
  unsigned n = p->len;
  if (n > sizeof(msg)) n = sizeof(msg);
  memcpy(&msg, p->payload, n);
  pbuf_free(p);

  if (msg.op != DHCP_OP_REQUEST) return;
  if (memcmp(msg.magic, dhcp_magic, 4) != 0) return;

  uint8_t *opt = usb_dhcp_find_option(msg.options, sizeof(msg.options), DHCP_OPT_MSGTYPE);
  if (!opt) return;
  uint8_t msg_type = opt[2];

  ip4_addr_t server_ip;
  IP4_ADDR(&server_ip, 192, 168, 7, 1);

  ip4_addr_t offer_ip;
  IP4_ADDR(&offer_ip, 192, 168, 7, 2);

  if (msg_type == DHCP_MSG_DISCOVER || msg_type == DHCP_MSG_REQUEST) {
    msg.op = DHCP_OP_REPLY;
    msg.secs = 0;
    msg.flags = 0;
    memcpy(msg.magic, dhcp_magic, 4);

    memcpy(msg.yiaddr, &offer_ip.addr, 4);
    memset(msg.options, 0, sizeof(msg.options));

    uint8_t reply_type = (msg_type == DHCP_MSG_DISCOVER) ? DHCP_MSG_OFFER : DHCP_MSG_ACK;
    usb_dhcp_fill_options(msg.options, reply_type, server_ip);

    struct pbuf *pp = pbuf_alloc(PBUF_TRANSPORT, sizeof(msg), PBUF_POOL);
    if (pp) {
      memcpy(pp->payload, &msg, sizeof(msg));
      udp_sendto_if(upcb, pp, IP_ADDR_BROADCAST, port, &usb_netif);
      pbuf_free(pp);
    }
  }
}

// ========== Public API ==========

// Plan B 対応: W5500 より先に呼ばれても安全なように lwip_init() を明示。
// arduino-pico の wrap で idempotent なので二度呼びでも問題なし。
void usb_ncm_init(void) {
  if (usb_ncm_initialized) return;

  lwip_init();

  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip,   192, 168, 7, 1);
  IP4_ADDR(&mask,  255, 255, 255, 0);
  IP4_ADDR(&gw,    0, 0, 0, 0);  // No gateway — link-local only

  // Add USB-NCM netif — do NOT call netif_set_default()
  netif_add(&usb_netif, &ip, &mask, &gw, NULL, usb_ncm_netif_init, ethernet_input);

  // Bring up the netif properly (triggers gratuitous ARP + callbacks)
  netif_set_link_up(&usb_netif);
  netif_set_up(&usb_netif);

  // Notify host that USB link is up
  tud_network_link_state(0, true);

  // Start DHCP server. Must bind to IP_ADDR_ANY because the host sends
  // DHCPDISCOVER to the limited broadcast 255.255.255.255; lwIP only
  // delivers broadcast UDP to PCBs bound to ANY, not to a specific IP.
  // udp_bind_netif() restricts delivery to USB-NCM only, so we don't
  // race with the W5500 segment. (No conflict with the W5500 DHCP
  // *client*: that uses port 68, this server uses port 67.)
  usb_dhcp_pcb = udp_new();
  if (usb_dhcp_pcb) {
    udp_bind(usb_dhcp_pcb, IP_ADDR_ANY, 67);
    udp_bind_netif(usb_dhcp_pcb, &usb_netif);
    udp_recv(usb_dhcp_pcb, usb_dhcp_recv, NULL);
  }

  usb_ncm_initialized = true;
  Serial.println("[USB-NCM] netif UP: 192.168.7.1/24 (link-local, no gateway)");
  Serial.println("[USB-NCM] DHCP server: offering 192.168.7.2");
}

// Call from loop() to process received USB packets
void usb_ncm_service(void) {
  if (!usb_ncm_initialized) return;

  uint16_t len = usb_rx_stage_len;
  if (len == 0) return;

  // Allocate pbuf in main-thread context — safe to use heap mutex here.
  // Try PBUF_POOL first (cheap), fall back to PBUF_RAM if the pool is full.
  struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  if (!p) p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
  if (p) {
    memcpy(p->payload, usb_rx_stage, len);
    if (ethernet_input(p, &usb_netif) != ERR_OK) pbuf_free(p);
  }

  // Release the staging slot and tell TinyUSB it can receive the next frame.
  usb_rx_stage_len = 0;
  tud_network_recv_renew();
}

// Get USB-NCM interface IP as string
String usb_ncm_ip_str(void) {
  if (!usb_ncm_initialized) return "N/A";
  return String(ip4addr_ntoa(netif_ip4_addr(&usb_netif)));
}

#endif // USB_NCM_H
