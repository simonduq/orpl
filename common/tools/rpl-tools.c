#include "contiki.h"
#include "net/rpl/rpl.h"
#include "net/packetbuf.h"
#include "tools/rpl-tools.h"

uip_ipaddr_t my_ipaddr;
uip_ipaddr_t prefix;

int forwarder_set_size = 0;
int neighbor_set_size = 0;
int curr_dio_interval;
uint16_t rank = 0xffff;

rtimer_clock_t start_time;

int time_elapsed() {
  return (RTIMER_NOW()-start_time)/(RTIMER_ARCH_SECOND*60);
}

/*---------------------------------------------------------------------------*/
void app_data_init(struct app_data *dst, struct app_data *src) {
  int i;
  for(i=0; i<sizeof(struct app_data); i++) {
    ((char*)dst)[i] = *(((char*)src)+i);
  }
}

/*---------------------------------------------------------------------------*/
void rpl_trace(struct app_data *dataptr) {

  struct app_data data;

  if(dataptr) {
    app_data_init(&data, dataptr);

    printf(" [%lx %u_%u %u->%u]",
        data.seqno,
        data.hop,
        data.fpcount,
        data.src,
        data.dest
          );

  }

  printf(" {%u/%u %u %u} \n",
        forwarder_set_size,
        neighbor_set_size,
        rank,
        curr_dio_interval
        );

}

/*---------------------------------------------------------------------------*/
struct app_data *rpl_dataptr_from_uip() {
  return (struct app_data *)((char*)uip_buf + ((uip_len - APP_PAYLOAD_LEN - 1)));
}

/*---------------------------------------------------------------------------*/
struct app_data *rpl_dataptr_from_packetbuf() {
  if(packetbuf_datalen() < 64) return 0;
  return (struct app_data *)((char*)packetbuf_dataptr() + ((packetbuf_datalen() - APP_PAYLOAD_LEN - 1)));
}

/*---------------------------------------------------------------------------*/
void
create_rpl_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if;

  root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;

    rpl_set_root(RPL_DEFAULT_INSTANCE, ipaddr);
    dag = rpl_get_any_dag();
    rpl_set_prefix(dag, &prefix, 64);
  }
}

/*---------------------------------------------------------------------------*/
void set_addr_iid(uip_ipaddr_t *ipaddr, uint16_t id) {
  ipaddr->u8[8] = ipaddr->u8[10] = ipaddr->u8[12] = ipaddr->u8[14] = id >> 8;
  ipaddr->u8[9] = ipaddr->u8[11] = ipaddr->u8[13] = ipaddr->u8[15] = id;
}

/*---------------------------------------------------------------------------*/
void node_ip6addr(uip_ipaddr_t *ipaddr, uint16_t id) {
  memcpy(ipaddr, &prefix, 8);
  set_addr_iid(ipaddr, id);
}

/*---------------------------------------------------------------------------*/
void rpl_setup(int is_root, uint16_t id) {
  start_time = RTIMER_NOW();
  uip_ip6addr(&prefix, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  node_ip6addr(&my_ipaddr, id);
  uip_ds6_addr_add(&my_ipaddr, 0, ADDR_AUTOCONF);
  if(is_root) {
    create_rpl_dag(&my_ipaddr);
  }
}
